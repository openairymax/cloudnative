/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2025-2026 SPHARX Ltd. All Rights Reserved.
 *
 * sn_test.c — 拓扑发现、令牌池配额、迁移准入与快照恢复
 *
 * [IND] 完全独立层（agentrt-linux 专属实现）。
 */

#define _POSIX_C_SOURCE 200809L

#include "sn_api.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int fails;

#define CHECK(cond, msg) do {						\
	if (!(cond)) {							\
		fprintf(stderr, "FAIL %s\n", msg);			\
		fails++;						\
	}								\
} while (0)

#define FIX	SN_FIXTURE_DIR

static int put(const char *path, const char *text)
{
	FILE *fp = fopen(path, "w");
	size_t len = strlen(text);

	if (!fp)
		return -1;
	if (fwrite(text, 1, len, fp) != len) {
		fclose(fp);
		return -1;
	}
	return fclose(fp);
}

static int has(const char *path, const char *want)
{
	char buf[1024];
	FILE *fp = fopen(path, "r");
	size_t n;

	if (!fp)
		return 0;
	n = fread(buf, 1, sizeof(buf) - 1, fp);
	buf[n] = '\0';
	fclose(fp);
	return strstr(buf, want) != NULL;
}

static void test_scan(void)
{
	struct sn_topo t;

	CHECK(sn_scan(FIX "/sys", &t) == 0, "拓扑发现");
	CHECK(t.count == 2, "节点数");
	CHECK(t.chips == 2, "芯片数");
	CHECK(t.cores == 6, "逻辑核总数");
	CHECK(t.mem_kb == 98304, "内存总量");
	CHECK(t.cxl_pools == 1, "CXL 池数量");
	CHECK(t.die[0].id == 0 && t.die[0].chip == 0, "节点 0 归属");
	CHECK(t.die[0].cores == 3 && t.die[0].mem_kb == 65536,
	      "节点 0 规模");
	CHECK(t.die[0].dist[0] == 10 && t.die[0].dist[1] == 21, "节点 0 距离");
	CHECK(t.die[1].id == 1 && t.die[1].chip == 1, "节点 1 归属");
	CHECK(t.die[1].cores == 3 && t.die[1].mem_kb == 32768,
	      "节点 1 规模");
	CHECK(t.die[1].dist[0] == 21 && t.die[1].dist[1] == 10, "节点 1 距离");

	CHECK(sn_scan("/nonexistent-sn", &t) < 0, "缺目录应失败");
}

static void test_affinity(void)
{
	CHECK(sn_affinity(FIX "/sys", FIX "/trace.txt", 7) == 0, "亲和度计算");
	CHECK(sn_affinity(FIX "/sys", "/nonexistent-trace", 7) == SN_EXIT_BAD,
	      "缺轨迹应失败");
}

static void test_pool(void)
{
	char dir[] = "/tmp/snpoolXXXXXX";
	char path[256];
	struct sn_pool p;

	CHECK(mkdtemp(dir) != NULL, "建立临时状态目录");
	snprintf(path, sizeof(path), "%s/pool.json", dir);
	CHECK(sn_status(dir) == SN_EXIT_BAD, "无状态时拒绝");
	CHECK(sn_pool_init(dir, 100, 100, 2) == SN_EXIT_USE, "保留量不得吞掉全部");
	CHECK(sn_pool_init(dir, 1000, 100, 2) == 0, "令牌池初始化");
	CHECK(sn_pool_load(dir, &p) == 0, "令牌池可读回");
	CHECK(p.total == 1000 && p.reserve == 100 && p.dies == 2, "令牌池字段");
	CHECK(p.budget[0] == 450 && p.budget[1] == 450, "均分上限");
	CHECK(sn_pool_left(&p) == 1000, "全局余量");

	CHECK(sn_alloc(dir, 0, 0) == SN_EXIT_USE, "拒绝零分配");
	CHECK(sn_alloc(dir, 5, 10) == SN_EXIT_USE, "拒绝越界芯片");
	CHECK(sn_alloc(dir, 0, 100) == 0, "芯片 0 分配");
	CHECK(sn_pool_load(dir, &p) == 0 && p.used[0] == 100, "记录占用");
	CHECK(sn_borrow(dir, 1, 0, 50) == 0, "芯片 1 借给芯片 0");
	CHECK(sn_pool_load(dir, &p) == 0, "借用后读回");
	CHECK(p.budget[0] == 500 && p.budget[1] == 400, "上限移动");
	CHECK(sn_pool_left(&p) == 900, "借用不改全局余量");
	CHECK(sn_alloc(dir, 0, 10000) == SN_EXIT_POOL, "全局不足应拒绝");
	CHECK(sn_borrow(dir, 0, 1, 10000) == SN_EXIT_DENY, "可借不足应拒绝");
	CHECK(sn_status(dir) == 0, "状态输出");

	unlink(path);
	rmdir(dir);
}

static void test_migrate(void)
{
	char dir[] = "/tmp/snmigXXXXXX";
	char path[256];
	char pool[256];
	int rc;

	CHECK(mkdtemp(dir) != NULL, "建立迁移状态目录");
	snprintf(pool, sizeof(pool), "%s/pool.json", dir);
	CHECK(sn_pool_init(dir, 1000, 100, 2) == 0, "迁移用令牌池");
	CHECK(sn_migrate(dir, 1, 0, 1, "hot", 0) == SN_EXIT_DENY,
	      "缺能力令牌应拒绝");
	CHECK(sn_migrate(dir, 1, 0, 1, "bogus", 1) == SN_EXIT_DENY,
	      "未知策略应拒绝");
	CHECK(sn_migrate(dir, 1, 0, 0, "hot", 1) == SN_EXIT_DENY,
	      "同源同目的应拒绝");
	CHECK(sn_migrate(dir, 1, 0, 9, "hot", 1) == SN_EXIT_DENY,
	      "目的越界应拒绝");

	rc = sn_migrate(dir, 1, 0, 1, "hot", 1);
	CHECK(rc == SN_EXIT_KERN || rc == SN_EXIT_DENY, "内核桥返回判定码");
	snprintf(path, sizeof(path), "%s/migrate.json", dir);
	CHECK(has(path, "\"agent\":1") && has(path, "\"dst\":1"), "迁移审计");

	CHECK(sn_alloc(dir, 1, 450) == 0, "占满芯片 1");
	CHECK(sn_migrate(dir, 1, 0, 1, "cold", 1) == SN_EXIT_DENY,
	      "目的余量不足应拒绝");
	CHECK(has(path, "\"reason\":\"budget\""), "预算拒绝原因");

	unlink(path);
	unlink(pool);
	rmdir(dir);
}

static void test_snapshot(void)
{
	char dir[] = "/tmp/snsnapXXXXXX";
	char out[256];
	char bad[256];
	char pool[256];
	struct sn_pool p;

	CHECK(mkdtemp(dir) != NULL, "建立快照状态目录");
	snprintf(out, sizeof(out), "%s/snap.json", dir);
	snprintf(bad, sizeof(bad), "%s/bad.json", dir);
	snprintf(pool, sizeof(pool), "%s/pool.json", dir);

	CHECK(sn_snapshot(FIX "/sys", dir, out) == SN_EXIT_BAD,
	      "缺令牌池时拒绝快照");
	CHECK(sn_pool_init(dir, 1000, 100, 2) == 0, "快照用令牌池");
	CHECK(sn_snapshot(FIX "/sys", dir, out) == 0, "生成快照");
	CHECK(has(out, "\"dies\":2") && has(out, "\"cores\":6"), "快照摘要");
	CHECK(has(out, "\"pool\":{\"total\":1000"), "快照含令牌池");

	CHECK(sn_alloc(dir, 0, 200) == 0, "快照前占用");
	CHECK(sn_snapshot(FIX "/sys", dir, out) == 0, "再生成快照");
	CHECK(sn_alloc(dir, 0, 300) == 0, "恢复前再占用");
	CHECK(sn_restore(FIX "/sys", dir, out) == 0, "恢复快照");
	CHECK(sn_pool_load(dir, &p) == 0 && p.used[0] == 200,
	      "恢复回写占用");
	CHECK(sn_pool_left(&p) == 800, "恢复余量");

	CHECK(put(bad, "{\"version\":1,\"dies\":3,\"chips\":2,\"cores\":6,"
			"\"memoryKiB\":98304,\"cxlPools\":1,\"pool\":"
			"{\"total\":1000,\"reserve\":100,\"dies\":"
			"[{\"budget\":450,\"used\":0},{\"budget\":450,"
			"\"used\":0}]}}\n") == 0, "写入不符快照");
	CHECK(sn_restore(FIX "/sys", dir, bad) == SN_EXIT_BAD,
	      "硬件不符应拒绝恢复");

	CHECK(put(bad, "{\"version\":1,\"dies\":2,\"chips\":2,\"cores\":6,"
			"\"memoryKiB\":98304,\"cxlPools\":1,\"pool\":"
			"{\"total\":1000}}\n") == 0, "写入坏池快照");
	CHECK(sn_restore(FIX "/sys", dir, bad) == SN_EXIT_BAD,
	      "令牌池不符应拒绝恢复");

	unlink(out);
	unlink(bad);
	unlink(pool);
	rmdir(dir);
}

int main(void)
{
	test_scan();
	test_affinity();
	test_pool();
	test_migrate();
	test_snapshot();
	if (fails) {
		fprintf(stderr, "sn_test: %d 项失败\n", fails);
		return 1;
	}
	printf("sn_test: ok\n");
	return 0;
}
