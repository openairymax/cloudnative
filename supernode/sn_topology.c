/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2025-2026 SPHARX Ltd. All Rights Reserved.
 *
 * sn_topology.c — sysfs 拓扑发现与亲和度计算
 *
 * 职责：
 *   从 /sys/devices/system/node 与 /sys/bus/cxl/devices 读出节点、CPU
 *   归属、内存量、芯片标识与 CXL 池数量；按访问轨迹计算各节点的加权
 *   得分并给出建议节点。
 *
 * 设计：
 *   节点按 id 直接落位，缺项保留零值；距离缺失按 10 计，避免除零；芯
 *   片以该节点最低编号 CPU 的 physical_package_id 代表。发现只读，
 *   不触碰内核状态。
 */

#define _POSIX_C_SOURCE 200809L

#include "sn_api.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SN_TRACE_MAX	64

/* sn_readlong() - 读取单行整数文件 */
static int sn_readlong(const char *path, long *out)
{
	FILE *fp;
	int ret;

	fp = fopen(path, "r");
	if (!fp)
		return -1;
	ret = fscanf(fp, "%ld", out) == 1 ? 0 : -1;
	fclose(fp);
	return ret;
}

/* sn_readtext() - 读取单行文本，去掉行尾换行 */
static int sn_readtext(const char *path, char *buf, size_t size)
{
	FILE *fp;
	size_t len;

	fp = fopen(path, "r");
	if (!fp)
		return -1;
	if (!fgets(buf, (int)size, fp)) {
		fclose(fp);
		return -1;
	}
	fclose(fp);
	len = strlen(buf);
	while (len && (buf[len - 1] == '\n' || buf[len - 1] == '\r'))
		buf[--len] = '\0';
	return 0;
}

/* sn_cores() - 统计 cpulist 覆盖的 CPU 个数，形如 "0-2,5" */
static int sn_cores(const char *list)
{
	const char *p = list;
	long lo, hi, sum = 0;
	char *end;

	while (*p) {
		lo = strtol(p, &end, 10);
		if (end == p)
			return -1;
		p = end;
		hi = lo;
		if (*p == '-') {
			hi = strtol(p + 1, &end, 10);
			if (end == p + 1)
				return -1;
			p = end;
		}
		if (hi < lo || hi - lo > 4096)
			return -1;
		sum += hi - lo + 1;
		if (sum > 65536)
			return -1;
		if (*p == ',')
			p++;
		else if (*p != '\0')
			return -1;
	}
	return (int)sum;
}

/* sn_dist() - 解析距离向量，缺项由调用方补 10 */
static int sn_dist(const char *path, int *dist, int want)
{
	FILE *fp;
	char line[SN_PATH_MAX];
	const char *p;
	char *end;
	int i = 0;

	fp = fopen(path, "r");
	if (!fp)
		return -1;
	if (!fgets(line, sizeof(line), fp)) {
		fclose(fp);
		return -1;
	}
	fclose(fp);
	p = line;
	while (i < want && *p) {
		long val = strtol(p, &end, 10);

		if (end == p)
			break;
		dist[i++] = (int)val;
		p = end;
	}
	return 0;
}

/* sn_chip() - 取节点内最低编号 CPU 的芯片标识 */
static int sn_chip(const char *root, const char *list)
{
	char path[SN_PATH_MAX];
	char *end;
	long cpu, pkg;

	cpu = strtol(list, &end, 10);
	if (end == list)
		return -1;
	snprintf(path, sizeof(path),
		 "%s/devices/system/cpu/cpu%ld/topology/"
		 "physical_package_id", root, cpu);
	if (sn_readlong(path, &pkg) < 0)
		return -1;
	return (int)pkg;
}

/* sn_cxl() - 统计 CXL 内存设备数量 */
static int sn_cxl(const char *root)
{
	char dir[SN_PATH_MAX];
	struct dirent *de;
	DIR *dp;
	int n = 0;

	snprintf(dir, sizeof(dir), "%s/bus/cxl/devices", root);
	dp = opendir(dir);
	if (!dp)
		return 0;
	while ((de = readdir(dp)) != NULL)
		if (strncmp(de->d_name, "mem", 3) == 0 &&
		    de->d_name[3] != '\0')
			n++;
	closedir(dp);
	return n;
}

/* sn_meminfo() - 从 meminfo 取该节点 MemTotal（KiB） */
static int sn_meminfo(const char *path, int id, long *kb)
{
	FILE *fp;
	char line[SN_NAME_MAX * 2];
	int node;
	long val;

	fp = fopen(path, "r");
	if (!fp)
		return -1;
	while (fgets(line, sizeof(line), fp))
		if (sscanf(line, "Node %d MemTotal: %ld kB", &node, &val) == 2 &&
		    node == id) {
			fclose(fp);
			*kb = val;
			return 0;
		}
	fclose(fp);
	return -1;
}

/* sn_node() - 读一个节点的内存、CPU 数、芯片与距离 */
static int sn_node(const char *root, int id, struct sn_die *die)
{
	char path[SN_PATH_MAX];
	char list[SN_NAME_MAX];
	int i;

	memset(die, 0, sizeof(*die));
	die->id = id;
	die->chip = -1;
	die->cxl_pool = -1;
	for (i = 0; i < SN_MAX_DIES; i++)
		die->dist[i] = 10;

	snprintf(path, sizeof(path), "%s/devices/system/node/node%d/meminfo",
		 root, id);
	if (sn_meminfo(path, id, &die->mem_kb) < 0)
		return -1;
	snprintf(path, sizeof(path), "%s/devices/system/node/node%d/cpulist",
		 root, id);
	if (sn_readtext(path, list, sizeof(list)) < 0)
		return -1;
	die->cores = sn_cores(list);
	if (die->cores < 0)
		return -1;
	die->chip = sn_chip(root, list);
	snprintf(path, sizeof(path), "%s/devices/system/node/node%d/distance",
		 root, id);
	sn_dist(path, die->dist, SN_MAX_DIES);
	return 0;
}

int sn_scan(const char *root, struct sn_topo *t)
{
	char dir[SN_PATH_MAX];
	struct dirent *de;
	DIR *dp;
	int id, i;

	memset(t, 0, sizeof(*t));
	for (i = 0; i < SN_MAX_DIES; i++)
		t->die[i].id = -1;
	snprintf(dir, sizeof(dir), "%s/devices/system/node", root);
	dp = opendir(dir);
	if (!dp)
		return -1;
	while ((de = readdir(dp)) != NULL) {
		if (sscanf(de->d_name, "node%d", &id) != 1)
			continue;
		if (id < 0 || id >= SN_MAX_DIES)
			continue;
		if (sn_node(root, id, &t->die[id]) < 0)
			continue;
		t->count++;
		t->cores += t->die[id].cores;
		t->mem_kb += t->die[id].mem_kb;
	}
	closedir(dp);
	if (!t->count)
		return -1;
	for (i = 0; i < SN_MAX_DIES; i++) {
		int j, seen = 0;

		if (t->die[i].id < 0 || t->die[i].chip < 0)
			continue;
		for (j = 0; j < i; j++)
			if (t->die[j].id >= 0 &&
			    t->die[j].chip == t->die[i].chip)
				seen = 1;
		if (!seen)
			t->chips++;
	}
	t->cxl_pools = sn_cxl(root);
	return 0;
}

/* sn_index() - 按节点 id 找落位下标 */
static int sn_index(const struct sn_topo *t, int id)
{
	int i;

	for (i = 0; i < SN_MAX_DIES; i++)
		if (t->die[i].id == id)
			return i;
	return -1;
}

int sn_affinity(const char *root, const char *trace, int agent)
{
	struct sn_topo topo;
	long pages[SN_MAX_DIES];
	long long score[SN_MAX_DIES];
	char line[SN_NAME_MAX * 2];
	FILE *fp;
	int i, id, best = -1, have = 0;
	long n;

	if (sn_scan(root, &topo) < 0) {
		fprintf(stderr, "sn: 拓扑发现失败\n");
		return SN_EXIT_BAD;
	}
	memset(pages, 0, sizeof(pages));
	fp = fopen(trace, "r");
	if (!fp) {
		fprintf(stderr, "sn: 无法读取轨迹 %s\n", trace);
		return SN_EXIT_BAD;
	}
	while (fgets(line, sizeof(line), fp))
		if (sscanf(line, "node=%d pages=%ld", &id, &n) == 2 &&
		    id >= 0 && id < SN_MAX_DIES && n > 0) {
			pages[id] += n;
			have = 1;
		}
	fclose(fp);
	if (!have) {
		fprintf(stderr, "sn: 轨迹无有效访问记录\n");
		return SN_EXIT_BAD;
	}
	memset(score, 0, sizeof(score));
	for (i = 0; i < SN_MAX_DIES; i++) {
		int from = sn_index(&topo, i), to;
		int dist;

		if (from < 0 || !pages[i])
			continue;
		for (to = 0; to < SN_MAX_DIES; to++) {
			if (topo.die[to].id < 0)
				continue;
			dist = topo.die[to].dist[i];
			if (dist <= 0)
				dist = 10;
			score[to] += pages[i] * 10000 / dist;
		}
	}
	printf("agent=%d\n", agent);
	for (i = 0; i < SN_MAX_DIES; i++) {
		if (topo.die[i].id < 0)
			continue;
		printf("node=%d score=%lld\n", topo.die[i].id, score[i]);
		if (best < 0 || score[i] > score[best])
			best = i;
	}
	if (best < 0) {
		fprintf(stderr, "sn: 无可达节点\n");
		return SN_EXIT_BAD;
	}
	printf("recommend=%d\n", topo.die[best].id);
	return 0;
}
