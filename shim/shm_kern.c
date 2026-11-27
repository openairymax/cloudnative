/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2025-2026 SPHARX Ltd. All Rights Reserved.
 *
 * shm_kern.c — 内核调度桥
 *
 * 职责：
 *   以 AIRY_SYS_SCHED_CTL 为唯一入口把目标 cgroup 的策略交给内核。
 *   UAPI 的错误码是紧凑的 Airymax 空间（error.h：11 表示不支持、
 *   5 表示参数无效），syscall 返回负数时该值以 errno 形式出现；本件
 *   按 UAPI 的数值解释，不把标准 errno 名称套到不同含义上。
 *
 * [IND] 完全独立层（agentrt-linux 专属实现）。
 */

#define _GNU_SOURCE

#include "shm_api.h"

#include <errno.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

/* UAPI：kernel/include/uapi/linux/airymax/syscalls.h */
#define SHM_SYS_SCHED	550
/* UAPI：kernel/include/uapi/linux/airymax/sched.h 的 SET_POLICY */
#define SHM_OP_POLICY	1

/* UAPI error.h 的数值原样解释，理由见文件头 */
#define SHM_AIRY_ENOTSUP	11
#define SHM_AIRY_EPERM		12
#define SHM_AIRY_EACCES		1
#define SHM_AIRY_EINVAL		5

enum shm_kern shm_sched_apply(const char *cgroup, const char *policy)
{
	long ret;
	int saved;

	if (!cgroup || !policy)
		return SHM_KERN_BAD;
	errno = 0;
	ret = syscall(SHM_SYS_SCHED, SHM_OP_POLICY, cgroup, policy);
	if (ret == 0)
		return SHM_KERN_OK;
	saved = errno;
	switch (saved) {
	case ENOSYS:
	case EOPNOTSUPP:
	case SHM_AIRY_ENOTSUP:
		return SHM_KERN_NOSYS;
	case SHM_AIRY_EPERM:
	case SHM_AIRY_EACCES:
		return SHM_KERN_DENY;
	case SHM_AIRY_EINVAL:
	default:
		return SHM_KERN_BAD;
	}
}

const char *shm_kern_name(enum shm_kern kern)
{
	switch (kern) {
	case SHM_KERN_OK:
		return "applied";
	case SHM_KERN_NOSYS:
		return "unsupported";
	case SHM_KERN_DENY:
		return "denied";
	case SHM_KERN_BAD:
	default:
		return "error";
	}
}

int shm_decode(int status)
{
	if (!status)
		return 0;
	if (WIFEXITED(status))
		return WEXITSTATUS(status);
	if (WIFSIGNALED(status))
		return 128 + WTERMSIG(status);
	return 255;
}
