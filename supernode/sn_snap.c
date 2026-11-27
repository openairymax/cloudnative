/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2025-2026 SPHARX Ltd. All Rights Reserved.
 *
 * sn_snap.c — 超节点控制面快照与恢复
 *
 * 职责：
 *   把拓扑摘要与令牌池写成一份 JSON 快照；恢复时重新发现硬件并逐项比对，
 *   一致才回写令牌池，避免把上一台机器的配额灌到另一台。
 *
 * 设计：
 *   快照只覆盖控制面状态，不包含页数据；恢复的比较项是节点数、芯片数、
 *   CPU 数、内存量与 CXL 池数，任一不符即拒绝。写入走临时文件再改名。
 */

#define _POSIX_C_SOURCE 200809L

#include "sn_api.h"
#include "cn_json.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* sn_put() - 把文本原子写入任意路径 */
static int sn_put(const char *path, const char *text)
{
	char tmp[SN_PATH_MAX];
	size_t len = strlen(text);
	FILE *fp;
	int bad = 0, fd;

	snprintf(tmp, sizeof(tmp), "%s.tmp", path);
	fp = fopen(tmp, "w");
	if (!fp)
		return -1;
	if (fwrite(text, 1, len, fp) != len)
		bad = 1;
	else if (fflush(fp) != 0)
		bad = 1;
	else {
		fd = fileno(fp);
		if (fd < 0 || fsync(fd) < 0)
			bad = 1;
	}
	if (fclose(fp) != 0)
		bad = 1;
	if (!bad && rename(tmp, path) < 0)
		bad = 1;
	if (bad) {
		unlink(tmp);
		return -1;
	}
	return 0;
}

/* sn_eq() - 快照字段与实时值一致 */
static int sn_eq(const struct cn_node *root, const char *key, long want)
{
	long val;

	if (cn_as_long(cn_get(root, key), &val) < 0)
		return 0;
	return val == want;
}

int sn_snapshot(const char *root, const char *dir, const char *out)
{
	struct sn_topo topo;
	struct sn_pool pool;
	char *buf = NULL;
	size_t len = 0;
	FILE *fp;
	int i, rc;

	if (sn_scan(root, &topo) < 0) {
		fprintf(stderr, "sn: 拓扑发现失败\n");
		return SN_EXIT_BAD;
	}
	if (sn_pool_load(dir, &pool) < 0) {
		fprintf(stderr, "sn: 无令牌池状态\n");
		return SN_EXIT_BAD;
	}
	fp = open_memstream(&buf, &len);
	if (!fp)
		return SN_EXIT_BAD;
	fprintf(fp, "{\"version\":1,\"dies\":%d,\"chips\":%d,\"cores\":%d,"
		"\"memoryKiB\":%ld,\"cxlPools\":%d,\"pool\":{\"total\":%ld,"
		"\"reserve\":%ld,\"dies\":[",
		topo.count, topo.chips, topo.cores, topo.mem_kb,
		topo.cxl_pools, pool.total, pool.reserve);
	for (i = 0; i < pool.dies; i++)
		fprintf(fp, "%s{\"budget\":%ld,\"used\":%ld}",
			i ? "," : "", pool.budget[i], pool.used[i]);
	fprintf(fp, "]}}\n");
	if (fclose(fp) != 0) {
		free(buf);
		return SN_EXIT_BAD;
	}
	rc = sn_put(out, buf);
	free(buf);
	if (rc < 0) {
		fprintf(stderr, "sn: 无法写入快照 %s\n", out);
		return SN_EXIT_BAD;
	}
	printf("snapshot=ok dies=%d cores=%d pools=%d file=%s\n",
	       topo.count, topo.cores, topo.cxl_pools, out);
	return 0;
}

int sn_restore(const char *root, const char *dir, const char *in)
{
	struct sn_topo topo;
	struct sn_pool pool;
	struct cn_doc *doc;
	const struct cn_node *obj;
	char *text;
	int ret = SN_EXIT_BAD;

	if (sn_scan(root, &topo) < 0) {
		fprintf(stderr, "sn: 拓扑发现失败\n");
		return SN_EXIT_BAD;
	}
	text = sn_slurp(in);
	if (!text) {
		fprintf(stderr, "sn: 无法读取快照 %s\n", in);
		return SN_EXIT_BAD;
	}
	if (cn_parse(text, strlen(text), &doc) < 0) {
		free(text);
		fprintf(stderr, "sn: 快照不是合法 JSON\n");
		return SN_EXIT_BAD;
	}
	free(text);
	obj = cn_root(doc);
	if (!sn_eq(obj, "version", 1) || !sn_eq(obj, "dies", topo.count) ||
	    !sn_eq(obj, "chips", topo.chips) ||
	    !sn_eq(obj, "cores", topo.cores) ||
	    !sn_eq(obj, "memoryKiB", topo.mem_kb) ||
	    !sn_eq(obj, "cxlPools", topo.cxl_pools)) {
		printf("restore=bad reason=hardware\n");
		goto out;
	}
	if (sn_pool_parse(cn_get(obj, "pool"), &pool) < 0) {
		printf("restore=bad reason=pool\n");
		goto out;
	}
	if (sn_pool_save(dir, &pool) < 0) {
		fprintf(stderr, "sn: 无法写入令牌池状态\n");
		goto out;
	}
	printf("restore=ok dies=%d cores=%d remaining=%ld\n",
	       topo.count, topo.cores, sn_pool_left(&pool));
	ret = 0;
out:
	cn_free(doc);
	return ret;
}
