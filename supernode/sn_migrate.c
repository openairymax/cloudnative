/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2025-2026 SPHARX Ltd. All Rights Reserved.
 *
 * sn_migrate.c — Agent 跨芯片迁移的判定、内核桥与审计
 *
 * 职责：
 *   依次核对迁移策略、源与目的芯片、能力令牌与目的芯片余量，通过后以
 *   549 号调用请内核搬运页面，并把每一步判定写入 migrate.json。
 *
 * 设计：
 *   用户态只做准入与记录，页搬运属于内核；内核返回的紧凑错误码按
 *   UAPI 原值解释（11 为不支持，1 与 12 为拒绝），与标准 errno 数值
 *   重合但不按 errno 语义处理。内核不支持时状态回滚为 rolled-back，
 *   不把未搬运的迁移记成完成。
 */

#define _GNU_SOURCE

#include "sn_api.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/syscall.h>
#include <unistd.h>

#define SN_SYS_ROVOL_CTL	549
#define SN_ROVOL_MIGRATE	0x03

/* sn_strategy() - 迁移策略白名单 */
static int sn_strategy(const char *name)
{
	return name && (strcmp(name, "hot") == 0 ||
			strcmp(name, "cold") == 0 ||
			strcmp(name, "increment") == 0);
}

/* sn_journal() - 记录一次迁移判定，原子覆盖 */
static int sn_journal(const char *dir, int agent, int src, int dst,
		      const char *strat, const char *state,
		      const char *kern, const char *reason)
{
	char text[SN_PATH_MAX * 4];

	snprintf(text, sizeof(text),
		 "{\"agent\":%d,\"src\":%d,\"dst\":%d,\"strategy\":\"%s\","
		 "\"state\":\"%s\",\"kernel\":\"%s\",\"reason\":\"%s\"}\n",
		 agent, src, dst, strat, state, kern, reason);
	return sn_atomic(dir, "migrate.json", text);
}

/* sn_kern() - 请内核搬运页面；返回 0 成功、1 不支持、2 拒绝、3 失败 */
static int sn_kern(int agent)
{
	long ret;

	ret = syscall(SN_SYS_ROVOL_CTL, (unsigned int)SN_ROVOL_MIGRATE,
		      (unsigned int)agent, 0ULL);
	if (ret == 0)
		return 0;
	if (errno == ENOSYS || errno == EAGAIN)
		return 1;
	if (errno == EPERM || errno == ENOMEM)
		return 2;
	return 3;
}

/* sn_reject() - 准入失败时的统一记录与输出 */
static int sn_reject(const char *dir, int agent, int src, int dst,
		     const char *strat, const char *reason)
{
	sn_journal(dir, agent, src, dst, strat, "rejected", "none", reason);
	printf("agent=%d src=%d dst=%d strategy=%s\n", agent, src, dst, strat);
	printf("kernel=none\nstate=rejected\nreason=%s\n", reason);
	return SN_EXIT_DENY;
}

int sn_migrate(const char *dir, int agent, int src, int dst,
	       const char *strategy, int cap)
{
	struct sn_pool p;
	long free;
	int rc;

	if (!sn_strategy(strategy))
		return sn_reject(dir, agent, src, dst, "invalid", "strategy");
	if (src < 0 || src >= SN_MAX_DIES ||
	    dst < 0 || dst >= SN_MAX_DIES || src == dst)
		return sn_reject(dir, agent, src, dst, strategy, "range");
	if (!cap)
		return sn_reject(dir, agent, src, dst, strategy, "capability");
	if (sn_pool_load(dir, &p) < 0) {
		fprintf(stderr, "sn: 无令牌池状态\n");
		return SN_EXIT_BAD;
	}
	if (dst >= p.dies)
		return sn_reject(dir, agent, src, dst, strategy, "range");
	free = p.budget[dst] - p.used[dst];
	if (free <= 0)
		return sn_reject(dir, agent, src, dst, strategy, "budget");
	rc = sn_kern(agent);
	if (rc == 0) {
		sn_journal(dir, agent, src, dst, strategy, "active",
			   "applied", "ok");
		printf("agent=%d src=%d dst=%d strategy=%s\n",
		       agent, src, dst, strategy);
		printf("kernel=applied\nstate=active\n");
		return 0;
	}
	if (rc == 1) {
		sn_journal(dir, agent, src, dst, strategy, "rolled-back",
			   "unsupported", "enotsup");
		printf("agent=%d src=%d dst=%d strategy=%s\n",
		       agent, src, dst, strategy);
		printf("kernel=unsupported\nstate=rolled-back\n");
		return SN_EXIT_KERN;
	}
	if (rc == 2) {
		sn_journal(dir, agent, src, dst, strategy, "rejected",
			   "denied", "eacces");
		printf("agent=%d src=%d dst=%d strategy=%s\n",
		       agent, src, dst, strategy);
		printf("kernel=denied\nstate=rejected\n");
		return SN_EXIT_DENY;
	}
	sn_journal(dir, agent, src, dst, strategy, "rolled-back",
		   "error", "fail");
	printf("agent=%d src=%d dst=%d strategy=%s\n", agent, src, dst, strategy);
	printf("kernel=error\nstate=rolled-back\n");
	return SN_EXIT_KERN;
}
