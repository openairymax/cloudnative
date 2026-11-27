/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2025-2026 SPHARX Ltd. All Rights Reserved.
 *
 * sn_api.h — 超节点聚合入口的内部契约
 *
 * 职责：
 *   把 sysfs 中的节点、芯片与 CXL 池归纳成一份拓扑，维护跨芯片的令牌
 *   池，并把 Agent 迁移的判定、内核桥调用与审计记录串成一条可回放的
 *   状态机。
 *
 * 设计：
 *   拓扑只读 sysfs，不写内核状态；令牌池落在控制面状态目录，按整数
 *   配额运算；迁移的内核页搬运交给 549 号调用，用户态只记录判定与
 *   结果。所有输出为稳定键值或单行 JSON，可逐字节比对。
 *
 * [IND] 完全独立层（agentrt-linux 专属实现）。
 */

#ifndef SN_API_H
#define SN_API_H

#include <stddef.h>

struct cn_node;

#define SN_NAME_MAX	64
#define SN_PATH_MAX	256
#define SN_MAX_DIES	16

#define SN_EXIT_USE	2	/* 用法错误 */
#define SN_EXIT_BAD	3	/* 拓扑、状态或快照不符 */
#define SN_EXIT_POOL	4	/* 令牌池不足以分配 */
#define SN_EXIT_DENY	6	/* 策略拒绝 */
#define SN_EXIT_KERN	7	/* 内核不支持或执行失败 */

/* 单个芯片的内存节点 */
struct sn_die {
	int id;
	int chip;
	int cores;
	int cxl_pool;
	long mem_kb;
	int dist[SN_MAX_DIES];
};

/* 一次 sysfs 发现的结果 */
struct sn_topo {
	int count;
	int chips;
	int cores;
	int cxl_pools;
	long mem_kb;
	struct sn_die die[SN_MAX_DIES];
};

/* 跨芯片令牌池；budget 为分给该芯片的上限，used 为已占用量 */
struct sn_pool {
	long total;
	long reserve;
	int dies;
	long budget[SN_MAX_DIES];
	long used[SN_MAX_DIES];
};

/* sn_topology.c */
int sn_scan(const char *root, struct sn_topo *t);
int sn_affinity(const char *root, const char *trace, int agent);

/* sn_pool.c */
int sn_atomic(const char *dir, const char *name, const char *text);
int sn_pool_init(const char *dir, long total, long reserve, int dies);
int sn_alloc(const char *dir, int die, long amount);
int sn_borrow(const char *dir, int src, int dst, long amount);
int sn_status(const char *dir);
int sn_pool_load(const char *dir, struct sn_pool *p);
int sn_pool_parse(const struct cn_node *obj, struct sn_pool *p);
int sn_pool_save(const char *dir, const struct sn_pool *p);
long sn_pool_left(const struct sn_pool *p);
char *sn_slurp(const char *path);

/* sn_migrate.c */
int sn_migrate(const char *dir, int agent, int src, int dst,
	       const char *strategy, int cap);

/* sn_snap.c */
int sn_snapshot(const char *root, const char *dir, const char *out);
int sn_restore(const char *root, const char *dir, const char *in);

#endif /* SN_API_H */
