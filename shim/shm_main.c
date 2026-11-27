/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2025-2026 SPHARX Ltd. All Rights Reserved.
 *
 * shm_main.c — airy-shim 命令行入口
 *
 * 职责：
 *   check 只做 bundle 读取、注解校验与动作规划；run 在同一份校验
 *   结果上拉起工作负载并监管至退出。两者共用 shm_load()，不在命令行
 *   里另立第二套判定。
 *
 * 退出码：0 成功；2 用法错误；3 bundle 不通过；4 无法启动。
 *
 * [IND] 完全独立层（agentrt-linux 专属实现）。
 */

#include "shm_api.h"

#include <stdio.h>
#include <string.h>

#define SHM_EXIT_BAD	3
#define SHM_EXIT_RUN	4

struct shm_opts {
	const char *bundle;
	const char *id;
	const char *cgroup;
};

/* shm_usage() - 用法说明，写到标准错误 */
static void shm_usage(void)
{
	fprintf(stderr,
		"usage: airy-shim check --bundle DIR [--id NAME]\n"
		"       airy-shim run --bundle DIR [--id NAME] "
		"[--cgroup PATH]\n");
}

/* shm_parse() - 解析命令行 */
static int shm_parse(int argc, char **argv, struct shm_opts *opts)
{
	int i;

	opts->bundle = NULL;
	opts->id = "local";
	opts->cgroup = NULL;
	for (i = 2; i < argc; i += 2) {
		if (i + 1 >= argc)
			return -1;
		if (strcmp(argv[i], "--bundle") == 0)
			opts->bundle = argv[i + 1];
		else if (strcmp(argv[i], "--id") == 0)
			opts->id = argv[i + 1];
		else if (strcmp(argv[i], "--cgroup") == 0)
			opts->cgroup = argv[i + 1];
		else
			return -1;
	}
	if (!opts->bundle || strlen(opts->id) >= SHM_ID_MAX)
		return -1;
	return 0;
}

/* shm_fill() - 读取并回填标识 */
static int shm_fill(const struct shm_opts *opts, struct shm_spec *spec)
{
	char err[192];

	if (shm_load(opts->bundle, spec, err, sizeof(err)) < 0) {
		fprintf(stderr, "invalid: %s\n", err);
		return -1;
	}
	strcpy(spec->id, opts->id);
	return 0;
}

int main(int argc, char **argv)
{
	struct shm_opts opts;
	struct shm_spec spec;
	int code, rc;

	if (argc < 3 ||
	    (strcmp(argv[1], "check") && strcmp(argv[1], "run"))) {
		shm_usage();
		return 2;
	}
	if (shm_parse(argc, argv, &opts) < 0) {
		shm_usage();
		return 2;
	}
	if (shm_fill(&opts, &spec) < 0)
		return SHM_EXIT_BAD;
	shm_plan(&spec, stdout);
	if (strcmp(argv[1], "check") == 0)
		return 0;
	if (shm_run(&spec, opts.cgroup, stdout, &code) < 0) {
		fprintf(stderr, "shim: 无法启动 %s\n", spec.argv[0]);
		return SHM_EXIT_RUN;
	}
	rc = code;
	return rc;
}
