/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2025-2026 SPHARX Ltd. All Rights Reserved.
 *
 * crd_api.h — Agent 自定义资源的校验、归一与动作规划接口
 *
 * 职责：
 *   kubectl 以 JSON 呈现的 Agent 对象进入本件后，先按设计文档声明的
 *   模式校验，再归一成定长结构，最后由 crd_plan() 产出确定的动作序列。
 *   状态机 crd_next() 与对象解析相互独立，便于单独验证转移表。
 *
 * 数据来源：docs-linux/150-cloudnative/01-k8s-crd-design.md。
 *
 * [IND] 完全独立层（agentrt-linux 专属实现）。
 */

#ifndef CRD_API_H
#define CRD_API_H

#include <stddef.h>
#include <stdio.h>

#include "cn_json.h"

#define CRD_NAME_MAX	64
#define CRD_IMAGE_MAX	192
#define CRD_TEXT_MAX	40
#define CRD_LIST_MAX	64

/* 记忆层位；取值即 spec.memoryRovol.layers 的位图表示 */
#define CRD_L1	0x1u
#define CRD_L2	0x2u
#define CRD_L3	0x4u
#define CRD_L4	0x8u

/* Agent 状态；顺序与设计文档的八态机一致 */
enum crd_state {
	CRD_CREATING,
	CRD_REGISTERED,
	CRD_READY,
	CRD_RUNNING,
	CRD_BLOCKED,
	CRD_SUSPENDED,
	CRD_TERMINATING,
	CRD_DEAD,
	CRD_STATE_BAD,
};

/* 驱动状态机的事件 */
enum crd_event {
	CRD_EV_REGISTER,
	CRD_EV_READY,
	CRD_EV_RUN,
	CRD_EV_BLOCK,
	CRD_EV_SUSPEND,
	CRD_EV_RESUME,
	CRD_EV_STOP,
	CRD_EV_DIE,
	CRD_EV_BAD,
};

struct crd_budget {
	long total;		/* 总预算，必须 > 0 */
	long per_req;		/* 单次上限，缺省等于 total */
	long refill;		/* 补充速率，缺省 0 */
	const char *policy;	/* throttle / suspend / hardFail */
};

struct crd_agent {
	char name[CRD_NAME_MAX];
	char ns[CRD_NAME_MAX];
	char image[CRD_IMAGE_MAX];
	const char *pull;
	struct crd_budget budget;
	unsigned layers;
	char capacity[CRD_TEXT_MAX];
	const char *forget;
	const char *cycle;
	const char *sched;
	const char *think;
	char seccomp[CRD_NAME_MAX];
	unsigned caps;
	unsigned paths;
	long cpu_milli;
	long mem_mib;
	long pids;
};

/*
 * crd_check() - 按 Agent 模式校验一个已解析的对象
 * @root: 对象根节点
 * @err:  失败时写入可读原因，可为 NULL
 * @errsz: 原因缓冲长度
 *
 * 返回值：0 通过；-1 不通过。
 */
int crd_check(const struct cn_node *root, char *err, size_t errsz);

/*
 * crd_fill() - 把已校验对象归一成定长结构
 * @root: 已通过 crd_check() 的对象根节点
 * @out:  归一结果
 *
 * 枚举字段回填为静态字面量，缺省字段按设计文档取值。
 * 返回值：0 成功；-1 取值为空导致无法归一。
 */
int crd_fill(const struct cn_node *root, struct crd_agent *out);

/*
 * crd_load() - 读文件、解析、校验并归一
 * @path: JSON 文件路径
 * @out:  归一结果
 * @err:  失败原因，可为 NULL
 * @errsz: 原因缓冲长度
 *
 * 返回值：0 成功；-1 文件不可读、语法错误或模式不符。
 */
int crd_load(const char *path, struct crd_agent *out, char *err,
	     size_t errsz);

/* crd_plan() - 输出确定顺序的动作序列，每行一个动作 */
void crd_plan(const struct crd_agent *agent, FILE *out);

/*
 * crd_cpu_milli() - 把 k8s CPU 字符串折算成毫核
 * @node: spec.resources.cpu 节点
 * @ok:   成功置 1，失败置 0
 *
 * 支持 "4"、"500m" 两种写法；返回值即毫核数。
 */
long crd_cpu_milli(const struct cn_node *node, int *ok);

/*
 * crd_mem_mib() - 把 k8s 内存字符串折算成 MiB
 * @node: spec.resources.memory 节点
 * @ok:   成功置 1，失败置 0
 *
 * 支持 Ki/Mi/Gi/Ti 与 K/M/G/T；无后缀按 MiB 解释。
 */
long crd_mem_mib(const struct cn_node *node, int *ok);

/*
 * crd_next() - 状态转移
 * @state: 当前状态
 * @event: 观测到的事件
 *
 * 返回值：下一状态；不允许的转移返回 CRD_STATE_BAD。
 */
enum crd_state crd_next(enum crd_state state, enum crd_event event);

/* crd_state_name() - 状态名；越界返回 "bad" */
const char *crd_state_name(enum crd_state state);

/* crd_state_parse() - 状态名转枚举；0 成功，-1 未知 */
int crd_state_parse(const char *name, enum crd_state *out);

/* crd_event_parse() - 事件名转枚举；0 成功，-1 未知 */
int crd_event_parse(const char *name, enum crd_event *out);

#endif /* CRD_API_H */
