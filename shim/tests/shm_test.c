/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2025-2026 SPHARX Ltd. All Rights Reserved.
 *
 * shm_test.c — bundle 解析、注解契约、内核桥映射与进程监管的单元测试
 *
 * 覆盖：完整注解、缺省值、非法层名、eevdf 调度类、规划文本、
 *       退出码折算与一次真实进程启动。
 *
 * [IND] 完全独立层（agentrt-linux 专属实现）。
 */

#define _POSIX_C_SOURCE 200809L

#include "shm_api.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>

static int fails;

#define CHECK(cond, msg)						\
	do {								\
		if (!(cond)) {						\
			printf("FAIL %s\n", msg);			\
			fails++;					\
		}							\
	} while (0)

static void test_load(void)
{
	struct shm_spec spec;
	char err[192];

	memset(&spec, 0, sizeof(spec));
	CHECK(shm_load(SHM_FIXTURE_DIR "/bundle", &spec, err,
		       sizeof(err)) == 0, err);
	CHECK(spec.argc == 3, "argc");
	CHECK(strcmp(spec.argv[0], "/bin/sh") == 0, "argv0");
	CHECK(spec.envc == 1, "envc");
	CHECK(spec.budget.total == 250000, "budget.total");
	CHECK(spec.budget.per_req == 250000, "budget.per_req");
	CHECK(spec.mem.layers == (SHM_L1 | SHM_L2), "layers");
	CHECK(spec.sched == SHM_SCHED_AGENT, "sched");
	CHECK(strcmp(spec.cycle, "CoreLoopThree") == 0, "cycle");
	CHECK(spec.caps == 0x103ull, "caps");
	CHECK(strcmp(spec.root, "/") == 0, "root");
}

static void test_defaults(void)
{
	struct shm_spec spec;
	char err[192];

	CHECK(shm_load(SHM_FIXTURE_DIR "/minimal", &spec, err,
		       sizeof(err)) == 0, err);
	CHECK(spec.budget.total == 1000000, "缺省预算");
	CHECK(spec.budget.policy[0] != '\0', "缺省策略");
	CHECK(spec.mem.layers == 0, "缺省无记忆层");
	CHECK(spec.caps == 1ull, "缺省能力");
	CHECK(spec.envc == 0, "缺省无环境");
}

static void test_eevdf(void)
{
	struct shm_spec spec;
	char err[192];

	CHECK(shm_load(SHM_FIXTURE_DIR "/eevdf", &spec, err,
		       sizeof(err)) == 0, err);
	CHECK(spec.sched == SHM_SCHED_EEVDF, "eevdf 调度类");
	CHECK(spec.mem.layers == 0, "disabled 层");
}

static void test_bad(void)
{
	struct shm_spec spec;
	char err[192];

	CHECK(shm_load(SHM_FIXTURE_DIR "/bad-layer", &spec, err,
		       sizeof(err)) == -2, "非法层名应判负");
	CHECK(strstr(err, "L5") != NULL, "错误信息应指出层名");
	CHECK(shm_load(SHM_FIXTURE_DIR "/missing", &spec, err,
		       sizeof(err)) == -1, "缺失目录应判负");
}

static void test_plan(void)
{
	struct shm_spec spec;
	char err[192];
	char *buf = NULL;
	size_t len = 0;
	FILE *fp;

	CHECK(shm_load(SHM_FIXTURE_DIR "/bundle", &spec, err,
		       sizeof(err)) == 0, err);
	strcpy(spec.id, "local");
	fp = open_memstream(&buf, &len);
	CHECK(fp != NULL, "open_memstream");
	if (!fp)
		return;
	shm_plan(&spec, fp);
	fclose(fp);
	CHECK(strstr(buf, "shim admit id=local args=3") != NULL, "规划摘要");
	CHECK(strstr(buf, "layers=L1,L2") != NULL, "规划层");
	CHECK(strstr(buf, "class=stc_agent") != NULL, "规划调度");
	CHECK(strstr(buf, "caps=0x103") != NULL, "规划能力");
	free(buf);
}

static void test_kern(void)
{
	CHECK(strcmp(shm_kern_name(SHM_KERN_OK), "applied") == 0, "ok");
	CHECK(strcmp(shm_kern_name(SHM_KERN_NOSYS), "unsupported") == 0,
	      "nosys");
	CHECK(strcmp(shm_kern_name(SHM_KERN_DENY), "denied") == 0,
	      "deny");
	CHECK(shm_sched_apply(NULL, "stc_agent") == SHM_KERN_BAD, "空参数");
}

static void test_decode(void)
{
	CHECK(shm_decode(0) == 0, "零状态");
	CHECK(shm_decode(7 << 8) == 7, "正常退出");
	CHECK(shm_decode(9) == 128 + 9, "信号退出");
}

static void test_run(void)
{
	struct shm_spec spec;
	char err[192];
	int code = -1;

	CHECK(shm_load(SHM_FIXTURE_DIR "/minimal", &spec, err,
		       sizeof(err)) == 0, err);
	strcpy(spec.id, "unit");
	CHECK(shm_run(&spec, NULL, stdout, &code) == 0, "启动失败");
	CHECK(code == 0, "退出码应为 0");
}

int main(void)
{
	test_load();
	test_defaults();
	test_eevdf();
	test_bad();
	test_plan();
	test_kern();
	test_decode();
	test_run();
	if (fails) {
		printf("shm_test: %d 项失败\n", fails);
		return 1;
	}
	printf("shm_test: ok\n");
	return 0;
}
