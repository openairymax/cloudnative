/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2025-2026 SPHARX Ltd. All Rights Reserved.
 *
 * crd_plan.c — 归一后的 Agent 到动作序列
 *
 * 职责：
 *   一段 Agent 声明不足以直接落地：内核注册、记忆卷载、安全注入与
 *   调度切换四步之间有固定次序。本件把归一结构展开成确定的行序，
 *   每行一个动词加键值对；顺序即执行顺序，控制器按行消费即可。
 *
 * 约束：
 *   输出只依赖输入结构，不含时间、进程号与主机名，便于逐字节比对。
 *
 * [IND] 完全独立层（agentrt-linux 专属实现）。
 */

#include "crd_api.h"

#include <stdio.h>

/* crd_layer_text() - 层位图转 "L1,L2" 形式；空位图写 "none" */
static void crd_layer_text(unsigned mask, char *buf, size_t cap)
{
	static const char *const names[] = { "L1", "L2", "L3", "L4" };
	size_t used = 0;
	unsigned i;

	if (cap == 0)
		return;
	buf[0] = '\0';
	for (i = 0; i < 4; i++) {
		int n;

		if ((mask & (1u << i)) == 0)
			continue;
		n = snprintf(buf + used, cap - used, "%s%s",
			     used == 0 ? "" : ",", names[i]);
		if (n < 0 || (size_t)n >= cap - used)
			break;
		used += (size_t)n;
	}
	if (used == 0)
		snprintf(buf, cap, "none");
}

void crd_plan(const struct crd_agent *agent, FILE *out)
{
	char layers[16];

	if (agent == NULL || out == NULL)
		return;
	crd_layer_text(agent->layers, layers, sizeof(layers));
	fprintf(out, "plan admit cpu_milli=%ld mem_mib=%ld pids=%ld\n",
		agent->cpu_milli, agent->mem_mib, agent->pids);
	fprintf(out,
		"plan register image=%s pull=%s budget=%ld per_req=%ld "
		"refill=%ld policy=%s\n",
		agent->image, agent->pull, agent->budget.total,
		agent->budget.per_req, agent->budget.refill,
		agent->budget.policy);
	fprintf(out, "plan memory layers=%s capacity=%s strategy=%s\n",
		layers, agent->capacity, agent->forget);
	fprintf(out, "plan security caps=%u landlock=%u seccomp=%s\n",
		agent->caps, agent->paths, agent->seccomp);
	fprintf(out, "plan sched class=%s cycle=%s think=%s\n",
		agent->sched, agent->cycle, agent->think);
	fprintf(out, "plan summary actions=5\n");
}
