/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2025-2026 SPHARX Ltd. All Rights Reserved.
 *
 * cni_api.h — Agent 容器的 CNI 链式插件与网络策略判定接口
 *
 * 职责：
 *   kubelet 按 conflist 依次调用插件，前序主插件产出 prevResult 后，
 *   本插件从其中取容器地址，把默认拒绝、放行对端、AgentsIPC 端口与
 *   出站例外写成一份运行时注册记录，供执行侧消费；同时提供可离线
 *   复算的判定入口，让门禁能在没有集群的环境里验证每条策略语义。
 *
 * 边界：
 *   本件不创建 veth、不配置地址、不下发 eBPF；这些由主插件与内核
 *   执行面承担。注册记录是插件对策略的唯一产物，路径取自配置的
 *   stateDir，缺省为运行目录下的 cupolas/net。
 *
 * 数据来源：docs-linux/150-cloudnative/03-cni-network-policy.md。
 *
 * [IND] 完全独立层（agentrt-linux 专属实现）。
 */

#ifndef CNI_API_H
#define CNI_API_H

#include <stddef.h>

#define CNI_SPEC_VER	"1.0.0"
#define CNI_VER_MAX	16
#define CNI_NAME_MAX	64
#define CNI_PATH_MAX	256
#define CNI_ID_MAX	128
#define CNI_NS_MAX	256
#define CNI_IF_MAX	16
#define CNI_PEER_MAX	16
#define CNI_CIDR_MAX	16
#define CNI_IP_MAX	48
#define CNI_DIR_MAX	256
#define CNI_PREV_MAX	4096
#define CNI_STDIN_MAX	65536

/* CNI 命令；取值对应 CNI_COMMAND 环境变量 */
enum cni_cmd {
	CNI_ADD,
	CNI_DEL,
	CNI_CHECK,
	CNI_VERSION,
	CNI_CMD_BAD,
};

/* 判定结果 */
enum cni_verdict {
	CNI_ALLOW,
	CNI_DENY,
};

struct cni_env {
	enum cni_cmd cmd;
	char container[CNI_ID_MAX];
	char netns[CNI_NS_MAX];
	char ifname[CNI_IF_MAX];
	char args[CNI_PATH_MAX];
};

struct cni_conf {
	char version[CNI_VER_MAX];
	char name[CNI_NAME_MAX];
	char type[CNI_NAME_MAX];
	int cupolas;		/* 是否登记到 Cupolas 过滤面 */
	int deny;		/* 默认拒绝；缺省 1 */
	int audit;		/* 审计开关；缺省 1 */
	int peers;
	char peer[CNI_PEER_MAX][CNI_NAME_MAX];
	int ports;		/* 入站放行端口 */
	int port[CNI_PEER_MAX];
	int exc;		/* 出站禁止的地址段 */
	char cidr[CNI_CIDR_MAX][CNI_IP_MAX];
	int eports;		/* 出站放行端口 */
	int eport[CNI_PEER_MAX];
	char state[CNI_DIR_MAX];
	char ip[CNI_IP_MAX];	/* prevResult 中的容器地址 */
	char prev[CNI_PREV_MAX];	/* prevResult 原文，ADD 时原样返回 */
};

/*
 * cni_cmd_parse() - 环境变量命令名转枚举
 *
 * 返回值：0 成功；-1 未知命令。
 */
int cni_cmd_parse(const char *name, enum cni_cmd *out);

/* cni_cmd_name() - 命令名；越界返回 "bad" */
const char *cni_cmd_name(enum cni_cmd cmd);

/*
 * cni_env_load() - 从 CNI_* 环境变量取一次调用的上下文
 * @out: 回填结构
 *
 * 返回值：0 成功；-1 CNI_COMMAND 缺失或非法。
 */
int cni_env_load(struct cni_env *out);

/* cni_conf_init() - 填入设计文档的基线缺省值 */
void cni_conf_init(struct cni_conf *out);

/*
 * cni_conf_load() - 解析并校验 stdin 的 NetConf
 * @text: JSON 文本
 * @len:  字节数
 * @out:  回填配置
 * @err:  失败原因，可为 NULL
 * @errsz: 原因缓冲长度
 *
 * 先填缺省值再覆盖，缺省拒绝、AgentsIPC 端口与常见出站例外是设计
 * 文档的基线；prevResult 缺失时容器地址为空，由 ADD 判定为链式装配
 * 错误而不是静默通过。
 *
 * 返回值：0 成功；-1 语法或契约不符。
 */
int cni_conf_load(const char *text, size_t len, struct cni_conf *out,
		  char *err, size_t errsz);

/*
 * cni_verdict() - 判定一条流
 * @c:    配置
 * @egress: 1 出站，0 入站
 * @peer: 入站来源 Agent 名，可为 NULL
 * @port: 端口
 * @dst:  出站目的地址，可为 NULL
 * @why:  判定理由，可为 NULL
 * @whysz: 理由缓冲长度
 *
 * 返回 CNI_ALLOW 或 CNI_DENY；理由为稳定的英文标记，供审计比对。
 */
enum cni_verdict cni_verdict(const struct cni_conf *c, int egress,
			     const char *peer, int port, const char *dst,
			     char *why, size_t whysz);

/*
 * cni_net_match() - 判断地址是否落在 CIDR 内
 * @addr: 点分十进制地址，可带前缀
 * @cidr: 点分十进制网段
 *
 * 只接受 IPv4；任一解析失败返回 0。
 */
int cni_net_match(const char *addr, const char *cidr);

/*
 * cni_state_add() - 原子写入注册记录
 * @c: 配置
 * @e: 本次调用上下文
 * @err/errsz: 失败原因
 *
 * 返回值：0 成功；-1 目录不可用、标识非法或写入失败。
 */
int cni_state_add(const struct cni_conf *c, const struct cni_env *e,
		  char *err, size_t errsz);

/*
 * cni_state_del() - 删除注册记录
 *
 * 记录不存在视为成功，满足 CNI DEL 的幂等要求。
 * 返回值：0 成功；-1 标识非法或删除失败。
 */
int cni_state_del(const struct cni_conf *c, const struct cni_env *e);

/*
 * cni_state_has() - 注册记录是否存在
 *
 * 返回值：1 存在；0 不存在；-1 标识非法。
 */
int cni_state_has(const struct cni_conf *c, const struct cni_env *e);

#endif /* CNI_API_H */
