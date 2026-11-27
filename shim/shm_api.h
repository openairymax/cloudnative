/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2025-2026 SPHARX Ltd. All Rights Reserved.
 *
 * shm_api.h — Agent 容器的 bundle 解析与运行时引擎接口
 *
 * 职责：
 *   OCI bundle 的 config.json 进入本件后，先按设计文档声明的注解契约
 *   提取 Agent 预算、记忆层、调度类与能力集，再归一成定长结构；
 *   shm_run() 负责拉起工作负载并监管其生命周期，内核桥按调度 syscall
 *   的返回区分「生效」与「后端未内置」。
 *
 * 边界：
 *   containerd 的 ttrpc 传输层由集群侧适配，不在本件内；本件只保证
 *   给定 bundle 时的准入、启动、监管与内核交互可独立复算。
 *
 * 数据来源：docs-linux/150-cloudnative/02-containerd-shim.md。
 *
 * [IND] 完全独立层（agentrt-linux 专属实现）。
 */

#ifndef SHM_API_H
#define SHM_API_H

#include <stddef.h>
#include <stdio.h>

#define SHM_ID_MAX	64
#define SHM_PATH_MAX	192
#define SHM_ARG_MAX	12
#define SHM_ARG_LEN	128
#define SHM_ENV_MAX	16
#define SHM_ENV_LEN	160
#define SHM_TEXT_MAX	24

/* 记忆层位；取值与注解 airymaxos.agent.memory-rovol 的层名对应 */
#define SHM_L1	0x1u
#define SHM_L2	0x2u
#define SHM_L3	0x4u
#define SHM_L4	0x8u

/* 调度类；stc_agent 由内核后端承载，未内置时回落 eevdf 并显式标记 */
enum shm_sched {
	SHM_SCHED_AGENT,
	SHM_SCHED_EEVDF,
	SHM_SCHED_BAD,
};

/* 内核桥结果 */
enum shm_kern {
	SHM_KERN_OK,
	SHM_KERN_NOSYS,
	SHM_KERN_DENY,
	SHM_KERN_BAD,
};

struct shm_budget {
	long total;
	long per_req;
	long refill;
	char policy[SHM_TEXT_MAX];
};

struct shm_mem {
	unsigned layers;
	char capacity[SHM_TEXT_MAX];
};

struct shm_spec {
	char id[SHM_ID_MAX];
	char bundle[SHM_PATH_MAX];
	char root[SHM_PATH_MAX];
	int argc;
	char argv[SHM_ARG_MAX][SHM_ARG_LEN];
	int envc;
	char env[SHM_ENV_MAX][SHM_ENV_LEN];
	struct shm_budget budget;
	struct shm_mem mem;
	enum shm_sched sched;
	char cycle[SHM_TEXT_MAX];
	unsigned long long caps;
};

/*
 * shm_load() - 读取并校验 bundle
 * @bundle: 含 config.json 的目录
 * @out:    归一结果；id 由调用方另行填写
 * @err:    失败原因，可为 NULL
 * @errsz:  原因缓冲长度
 *
 * 返回值：0 成功；-1 读取或语法错误；-2 注解契约不符。
 */
int shm_load(const char *bundle, struct shm_spec *out, char *err,
	     size_t errsz);

/* shm_plan() - 输出准入与启动的动作序列，每行一个动作 */
void shm_plan(const struct shm_spec *spec, FILE *out);

/*
 * shm_sched_apply() - 通过调度 syscall 为目标 cgroup 设策略
 * @cgroup: 目标 cgroup 路径
 * @policy: 策略名，如 "stc_agent"
 *
 * 返回值：SHM_KERN_OK 生效；SHM_KERN_NOSYS 内核后端未内置；
 *         SHM_KERN_DENY 拒绝；SHM_KERN_BAD 参数或内核错误。
 */
enum shm_kern shm_sched_apply(const char *cgroup, const char *policy);

/* shm_kern_name() - 内核桥结果的英文名 */
const char *shm_kern_name(enum shm_kern kern);

/* shm_decode() - 把 wait 状态折算成进程退出码 */
int shm_decode(int status);

/*
 * shm_run() - 拉起工作负载并监管至退出
 * @spec:   已校验的 bundle 描述
 * @cgroup: 调度 syscall 的目标 cgroup，可为 NULL 表示不切换
 * @out:    动作与状态输出
 * @code:   回填退出码
 *
 * 返回值：0 进程已退出；-1 无法启动。
 */
int shm_run(const struct shm_spec *spec, const char *cgroup, FILE *out,
	    int *code);

#endif /* SHM_API_H */
