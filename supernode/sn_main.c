/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2025-2026 SPHARX Ltd. All Rights Reserved.
 *
 * sn_main.c — 超节点聚合入口的命令行
 *
 * 职责：
 *   把 discover、affinity、pool、migrate、snapshot、restore 六个动作分派
 *   到各自实现；负责选项解析、缺省根目录与状态目录，以及用法提示。
 *
 * 设计：
 *   选项只接受 "--键 值" 与布尔 "--cap"；数值走整串校验，拒绝尾随字符。
 *   缺省根目录为 /sys，状态目录为 /run/airymax/supernode；退出码由下层
 *   实现返回，本层不重写。
 */

#define _POSIX_C_SOURCE 200809L

#include "sn_api.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SN_ROOT_DEF	"/sys"
#define SN_STATE_DEF	"/run/airymax/supernode"

/* sn_opt() - 取 --键 后面的值 */
static const char *sn_opt(int argc, char **argv, const char *key)
{
	int i;

	for (i = 2; i + 1 < argc; i++)
		if (strcmp(argv[i], key) == 0)
			return argv[i + 1];
	return NULL;
}

/* sn_flag() - 布尔开关是否存在 */
static int sn_flag(int argc, char **argv, const char *key)
{
	int i;

	for (i = 2; i < argc; i++)
		if (strcmp(argv[i], key) == 0)
			return 1;
	return 0;
}

/* sn_num() - 取整数选项，要求整串完整 */
static int sn_num(int argc, char **argv, const char *key, long *out)
{
	const char *text = sn_opt(argc, argv, key);
	char *end;

	if (!text)
		return -1;
	errno = 0;
	*out = strtol(text, &end, 10);
	if (errno || end == text || *end != '\0')
		return -1;
	return 0;
}

/* sn_show() - 输出单行拓扑 JSON */
static int sn_show(const struct sn_topo *t)
{
	int i, d, first = 1;

	printf("{\"supernode\":0,\"dies\":%d,\"chips\":%d,\"cores\":%d,"
	       "\"memoryKiB\":%ld,\"cxlPools\":%d,\"die\":[",
	       t->count, t->chips, t->cores, t->mem_kb, t->cxl_pools);
	for (i = 0; i < SN_MAX_DIES; i++) {
		if (t->die[i].id < 0)
			continue;
		printf("%s{\"id\":%d,\"chip\":%d,\"cores\":%d,"
		       "\"memoryKiB\":%ld,\"distance\":[",
		       first ? "" : ",", t->die[i].id, t->die[i].chip,
		       t->die[i].cores, t->die[i].mem_kb);
		first = 0;
		for (d = 0; d < SN_MAX_DIES; d++) {
			if (t->die[d].id < 0)
				continue;
			printf("%s%d", d ? "," : "", t->die[i].dist[d]);
		}
		printf("]}");
	}
	printf("]}\n");
	return 0;
}

static void sn_usage(void)
{
	fprintf(stderr,
		"用法: airy-supernode <动作>\n"
		"  discover [--root DIR]\n"
		"  affinity --trace FILE [--agent N] [--root DIR]\n"
		"  pool init --total N --reserve N --dies N [--state DIR]\n"
		"  pool alloc --die N --amount N [--state DIR]\n"
		"  pool borrow --src N --dst N --amount N [--state DIR]\n"
		"  pool status [--state DIR]\n"
		"  migrate --agent N --src N --dst N [--strategy S] [--cap]\n"
		"          [--state DIR]\n"
		"  snapshot --output FILE [--root DIR] [--state DIR]\n"
		"  restore --input FILE [--root DIR] [--state DIR]\n");
}

/* sn_pool_cmd() - 令牌池子命令 */
static int sn_pool_cmd(int argc, char **argv, const char *state)
{
	const char *sub = argc > 2 ? argv[2] : "";
	long total, reserve, dies, die, amount, src, dst;

	if (strcmp(sub, "init") == 0) {
		if (sn_num(argc, argv, "--total", &total) < 0 ||
		    sn_num(argc, argv, "--reserve", &reserve) < 0 ||
		    sn_num(argc, argv, "--dies", &dies) < 0)
			return SN_EXIT_USE;
		return sn_pool_init(state, total, reserve, (int)dies);
	}
	if (strcmp(sub, "alloc") == 0) {
		if (sn_num(argc, argv, "--die", &die) < 0 ||
		    sn_num(argc, argv, "--amount", &amount) < 0)
			return SN_EXIT_USE;
		return sn_alloc(state, (int)die, amount);
	}
	if (strcmp(sub, "borrow") == 0) {
		if (sn_num(argc, argv, "--src", &src) < 0 ||
		    sn_num(argc, argv, "--dst", &dst) < 0 ||
		    sn_num(argc, argv, "--amount", &amount) < 0)
			return SN_EXIT_USE;
		return sn_borrow(state, (int)src, (int)dst, amount);
	}
	if (strcmp(sub, "status") == 0)
		return sn_status(state);
	return SN_EXIT_USE;
}

int main(int argc, char **argv)
{
	const char *root, *state, *trace, *file, *strat;
	struct sn_topo topo;
	long agent = 0, src, dst;
	int cap;

	if (argc < 2 || strcmp(argv[1], "--help") == 0) {
		sn_usage();
		return SN_EXIT_USE;
	}
	root = sn_opt(argc, argv, "--root");
	state = sn_opt(argc, argv, "--state");
	if (!root)
		root = SN_ROOT_DEF;
	if (!state)
		state = SN_STATE_DEF;

	if (strcmp(argv[1], "discover") == 0) {
		if (sn_scan(root, &topo) < 0) {
			fprintf(stderr, "sn: 拓扑发现失败\n");
			return SN_EXIT_BAD;
		}
		return sn_show(&topo);
	}
	if (strcmp(argv[1], "affinity") == 0) {
		trace = sn_opt(argc, argv, "--trace");
		if (!trace)
			return SN_EXIT_USE;
		sn_num(argc, argv, "--agent", &agent);
		return sn_affinity(root, trace, (int)agent);
	}
	if (strcmp(argv[1], "pool") == 0)
		return sn_pool_cmd(argc, argv, state);
	if (strcmp(argv[1], "migrate") == 0) {
		strat = sn_opt(argc, argv, "--strategy");
		if (sn_num(argc, argv, "--agent", &agent) < 0 ||
		    sn_num(argc, argv, "--src", &src) < 0 ||
		    sn_num(argc, argv, "--dst", &dst) < 0)
			return SN_EXIT_USE;
		if (!strat)
			strat = "hot";
		cap = sn_flag(argc, argv, "--cap");
		return sn_migrate(state, (int)agent, (int)src, (int)dst,
				  strat, cap);
	}
	if (strcmp(argv[1], "snapshot") == 0) {
		file = sn_opt(argc, argv, "--output");
		if (!file)
			return SN_EXIT_USE;
		return sn_snapshot(root, state, file);
	}
	if (strcmp(argv[1], "restore") == 0) {
		file = sn_opt(argc, argv, "--input");
		if (!file)
			return SN_EXIT_USE;
		return sn_restore(root, state, file);
	}
	sn_usage();
	return SN_EXIT_USE;
}
