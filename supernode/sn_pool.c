/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2025-2026 SPHARX Ltd. All Rights Reserved.
 *
 * sn_pool.c — 跨芯片令牌池的配额运算与持久化
 *
 * 职责：
 *   按芯片分配令牌上限，记录已占用量，支持跨芯片借用与余量分级；状态
 *   落在控制面目录的 pool.json，写入走临时文件、fsync、改名三步。
 *
 * 设计：
 *   total 分为 reserve 与按芯片均分的可配额，余数归 0 号芯片；分配只
 *   扣全局余量，借用只在两芯片之间移动上限，不改全局余量。余量低于
 *   total 的 20% 记 low，低于 5% 记 critical。
 */

#define _POSIX_C_SOURCE 200809L

#include "sn_api.h"
#include "cn_json.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define SN_LOW_PCT	20
#define SN_CRIT_PCT	5

/* sn_atomic() - 建目录后写临时文件再改名，保证读者只见完整状态 */
int sn_atomic(const char *dir, const char *name, const char *text)
{
	char tmp[SN_PATH_MAX];
	char dst[SN_PATH_MAX];
	size_t len = strlen(text);
	FILE *fp;
	int fd, ret = -1;

	if (mkdir(dir, 0755) < 0 && errno != EEXIST)
		return -1;
	snprintf(tmp, sizeof(tmp), "%s/%s.tmp", dir, name);
	snprintf(dst, sizeof(dst), "%s/%s", dir, name);
	fp = fopen(tmp, "w");
	if (!fp)
		return -1;
	if (fwrite(text, 1, len, fp) != len)
		goto out;
	if (fflush(fp) != 0)
		goto out;
	fd = fileno(fp);
	if (fd < 0 || fsync(fd) < 0)
		goto out;
	if (fclose(fp) != 0) {
		fp = NULL;
		goto out;
	}
	fp = NULL;
	if (rename(tmp, dst) < 0)
		goto out;
	return 0;
out:
	if (fp)
		fclose(fp);
	unlink(tmp);
	return ret;
}

/* sn_pool_text() - 序列化令牌池为单行 JSON */
static char *sn_pool_text(const struct sn_pool *p)
{
	char *buf = NULL;
	size_t len = 0;
	FILE *fp;
	int i;

	fp = open_memstream(&buf, &len);
	if (!fp)
		return NULL;
	fprintf(fp, "{\"total\":%ld,\"reserve\":%ld,\"dies\":[",
		p->total, p->reserve);
	for (i = 0; i < p->dies; i++)
		fprintf(fp, "%s{\"budget\":%ld,\"used\":%ld}",
			i ? "," : "", p->budget[i], p->used[i]);
	fprintf(fp, "]}\n");
	if (fclose(fp) != 0) {
		free(buf);
		return NULL;
	}
	return buf;
}

int sn_pool_save(const char *dir, const struct sn_pool *p)
{
	char *text;
	int ret;

	text = sn_pool_text(p);
	if (!text)
		return -1;
	ret = sn_atomic(dir, "pool.json", text);
	free(text);
	return ret;
}

/* sn_slurp() - 读入整份文本文件 */
char *sn_slurp(const char *path)
{
	FILE *fp;
	char *buf;
	long size;

	fp = fopen(path, "r");
	if (!fp)
		return NULL;
	if (fseek(fp, 0, SEEK_END) < 0) {
		fclose(fp);
		return NULL;
	}
	size = ftell(fp);
	if (size < 0 || fseek(fp, 0, SEEK_SET) < 0) {
		fclose(fp);
		return NULL;
	}
	buf = malloc((size_t)size + 1);
	if (!buf) {
		fclose(fp);
		return NULL;
	}
	if (fread(buf, 1, (size_t)size, fp) != (size_t)size) {
		free(buf);
		fclose(fp);
		return NULL;
	}
	buf[size] = '\0';
	fclose(fp);
	return buf;
}

int sn_pool_parse(const struct cn_node *obj, struct sn_pool *p)
{
	const struct cn_node *node;
	long val;

	memset(p, 0, sizeof(*p));
	if (!obj || obj->type != CN_OBJ)
		return -1;
	if (cn_as_long(cn_get(obj, "total"), &p->total) < 0)
		return -1;
	if (cn_as_long(cn_get(obj, "reserve"), &p->reserve) < 0)
		return -1;
	node = cn_get(obj, "dies");
	if (!node || node->type != CN_ARR)
		return -1;
	for (node = node->child; node; node = node->sibling) {
		if (p->dies >= SN_MAX_DIES)
			return -1;
		if (cn_as_long(cn_get(node, "budget"), &val) < 0)
			return -1;
		p->budget[p->dies] = val;
		if (cn_as_long(cn_get(node, "used"), &val) < 0)
			return -1;
		p->used[p->dies] = val;
		p->dies++;
	}
	return p->dies > 0 ? 0 : -1;
}

int sn_pool_load(const char *dir, struct sn_pool *p)
{
	char path[SN_PATH_MAX];
	struct cn_doc *doc;
	char *text;
	int ret;

	snprintf(path, sizeof(path), "%s/pool.json", dir);
	text = sn_slurp(path);
	if (!text)
		return -1;
	ret = cn_parse(text, strlen(text), &doc);
	free(text);
	if (ret < 0)
		return -1;
	ret = sn_pool_parse(cn_root(doc), p);
	cn_free(doc);
	return ret;
}

/* sn_pool_left() - 全局未分配量 */
long sn_pool_left(const struct sn_pool *p)
{
	long used = 0;
	int i;

	for (i = 0; i < p->dies; i++)
		used += p->used[i];
	return p->total - used;
}

/* sn_level() - 余量分级 */
static const char *sn_level(long left, long total)
{
	if (left * 100 < total * SN_CRIT_PCT)
		return "critical";
	if (left * 100 < total * SN_LOW_PCT)
		return "low";
	return "ok";
}

int sn_pool_init(const char *dir, long total, long reserve, int dies)
{
	struct sn_pool p;
	long share, rest;
	int i;

	if (dies <= 0 || dies > SN_MAX_DIES || total <= 0 || reserve < 0 ||
	    reserve >= total)
		return SN_EXIT_USE;
	share = (total - reserve) / dies;
	if (share <= 0)
		return SN_EXIT_USE;
	rest = (total - reserve) % dies;
	memset(&p, 0, sizeof(p));
	p.total = total;
	p.reserve = reserve;
	p.dies = dies;
	for (i = 0; i < dies; i++)
		p.budget[i] = share + (i == 0 ? rest : 0);
	if (sn_pool_save(dir, &p) < 0) {
		fprintf(stderr, "sn: 无法写入令牌池状态\n");
		return SN_EXIT_BAD;
	}
	printf("pool=initialized dies=%d total=%ld reserve=%ld\n",
	       dies, total, reserve);
	return 0;
}

int sn_alloc(const char *dir, int die, long amount)
{
	struct sn_pool p;
	long left, free;

	if (amount <= 0)
		return SN_EXIT_USE;
	if (sn_pool_load(dir, &p) < 0) {
		fprintf(stderr, "sn: 无令牌池状态\n");
		return SN_EXIT_BAD;
	}
	if (die < 0 || die >= p.dies) {
		fprintf(stderr, "sn: 芯片编号越界\n");
		return SN_EXIT_USE;
	}
	left = sn_pool_left(&p);
	if (left < amount) {
		fprintf(stderr, "sn: 余量 %ld 不足以分配 %ld\n", left, amount);
		return SN_EXIT_POOL;
	}
	p.used[die] += amount;
	if (sn_pool_save(dir, &p) < 0) {
		fprintf(stderr, "sn: 无法写入令牌池状态\n");
		return SN_EXIT_BAD;
	}
	free = p.budget[die] - p.used[die];
	left -= amount;
	printf("die=%d used=%ld free=%ld remaining=%ld level=%s\n",
	       die, p.used[die], free, left, sn_level(left, p.total));
	return 0;
}

int sn_borrow(const char *dir, int src, int dst, long amount)
{
	struct sn_pool p;
	long free;

	if (amount <= 0 || src == dst)
		return SN_EXIT_USE;
	if (sn_pool_load(dir, &p) < 0) {
		fprintf(stderr, "sn: 无令牌池状态\n");
		return SN_EXIT_BAD;
	}
	if (src < 0 || src >= p.dies || dst < 0 || dst >= p.dies) {
		fprintf(stderr, "sn: 芯片编号越界\n");
		return SN_EXIT_USE;
	}
	free = p.budget[src] - p.used[src];
	if (free < amount) {
		fprintf(stderr, "sn: 芯片 %d 可借 %ld，不足 %ld\n",
			src, free, amount);
		return SN_EXIT_DENY;
	}
	p.budget[src] -= amount;
	p.budget[dst] += amount;
	if (sn_pool_save(dir, &p) < 0) {
		fprintf(stderr, "sn: 无法写入令牌池状态\n");
		return SN_EXIT_BAD;
	}
	printf("borrow=ok src=%d dst=%d amount=%ld remaining=%ld\n",
	       src, dst, amount, sn_pool_left(&p));
	return 0;
}

int sn_status(const char *dir)
{
	struct sn_pool p;
	long left, distributed = 0;
	int i;

	if (sn_pool_load(dir, &p) < 0) {
		fprintf(stderr, "sn: 无令牌池状态\n");
		return SN_EXIT_BAD;
	}
	left = sn_pool_left(&p);
	for (i = 0; i < p.dies; i++)
		distributed += p.budget[i];
	printf("total=%ld reserve=%ld distributed=%ld remaining=%ld\n",
	       p.total, p.reserve, distributed, left);
	for (i = 0; i < p.dies; i++)
		printf("die=%d budget=%ld used=%ld free=%ld\n",
		       i, p.budget[i], p.used[i],
		       p.budget[i] - p.used[i]);
	printf("level=%s\n", sn_level(left, p.total));
	return 0;
}
