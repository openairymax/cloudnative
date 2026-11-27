/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2025-2026 SPHARX Ltd. All Rights Reserved.
 *
 * crd_main.c — airy-crdctl 命令行入口
 *
 * 职责：
 *   提供 validate / plan / state 三个子命令。validate 与 plan 消费
 *   kubectl 以 JSON 输出的 Agent 对象；state 单独验证转移表。
 *
 * 退出码：0 成功；2 用法错误；3 校验或转移不通过。
 *
 * [IND] 完全独立层（agentrt-linux 专属实现）。
 */

#include "crd_api.h"

#include <stdio.h>
#include <string.h>

#define CRD_EXIT_BAD	3

/* crd_do_check() - validate 子命令 */
static int crd_do_check(const char *path)
{
	struct crd_agent agent;
	char err[192];

	if (crd_load(path, &agent, err, sizeof(err)) < 0) {
		fprintf(stderr, "invalid: %s\n", err);
		return CRD_EXIT_BAD;
	}
	printf("ok kind=Agent name=%s ns=%s\n", agent.name,
	       agent.ns[0] != '\0' ? agent.ns : "default");
	return 0;
}

/* crd_do_plan() - plan 子命令 */
static int crd_do_plan(const char *path)
{
	struct crd_agent agent;
	char err[192];

	if (crd_load(path, &agent, err, sizeof(err)) < 0) {
		fprintf(stderr, "invalid: %s\n", err);
		return CRD_EXIT_BAD;
	}
	crd_plan(&agent, stdout);
	return 0;
}

/* crd_do_state() - state 子命令 */
static int crd_do_state(const char *from, const char *event)
{
	enum crd_state state;
	enum crd_event ev;
	enum crd_state next;

	if (crd_state_parse(from, &state) < 0 ||
	    crd_event_parse(event, &ev) < 0) {
		fprintf(stderr, "invalid: 未知状态或事件\n");
		return CRD_EXIT_BAD;
	}
	next = crd_next(state, ev);
	if (next == CRD_STATE_BAD) {
		fprintf(stderr, "invalid: %s -%s-> 不允许\n",
			crd_state_name(state), event);
		return CRD_EXIT_BAD;
	}
	printf("state %s\n", crd_state_name(next));
	return 0;
}

/* crd_usage() - 用法说明，写到标准错误 */
static void crd_usage(void)
{
	fprintf(stderr, "usage: airy-crdctl validate FILE\n"
			"       airy-crdctl plan FILE\n"
			"       airy-crdctl state FROM EVENT\n");
}

int main(int argc, char **argv)
{
	if (argc == 3 && strcmp(argv[1], "validate") == 0)
		return crd_do_check(argv[2]);
	if (argc == 3 && strcmp(argv[1], "plan") == 0)
		return crd_do_plan(argv[2]);
	if (argc == 4 && strcmp(argv[1], "state") == 0)
		return crd_do_state(argv[2], argv[3]);
	crd_usage();
	return 2;
}
