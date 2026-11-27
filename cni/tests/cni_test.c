/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2025-2026 SPHARX Ltd. All Rights Reserved.
 *
 * cni_test.c — NetConf 归一、判定矩阵、IPv4 匹配、注册记录与环境解析
 *
 * [IND] 完全独立层（agentrt-linux 专属实现）。
 */

#define _POSIX_C_SOURCE 200809L

#include "cni_api.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int fails;

#define CHECK(cond, msg) do {						\
	if (!(cond)) {							\
		fprintf(stderr, "FAIL %s\n", msg);			\
		fails++;						\
	}								\
} while (0)

static int slurp(const char *name, char *buf, size_t cap, size_t *len)
{
	char path[512];
	FILE *fp;
	size_t n;

	snprintf(path, sizeof(path), "%s/%s", CNI_FIXTURE_DIR, name);
	fp = fopen(path, "r");
	if (!fp)
		return -1;
	n = fread(buf, 1, cap - 1, fp);
	if (ferror(fp)) {
		fclose(fp);
		return -1;
	}
	buf[n] = '\0';
	*len = n;
	fclose(fp);
	return 0;
}

static void load(const char *name, struct cni_conf *conf, char *err,
		 size_t errsz, int *rc)
{
	static char buf[CNI_STDIN_MAX];
	size_t len = 0;

	*rc = -1;
	if (slurp(name, buf, sizeof(buf), &len) < 0)
		return;
	*rc = cni_conf_load(buf, len, conf, err, errsz);
}

static void test_conf(void)
{
	struct cni_conf conf;
	char err[256] = "";
	int rc;

	load("netconf.json", &conf, err, sizeof(err), &rc);
	CHECK(rc == 0, err[0] ? err : "netconf 解析失败");
	CHECK(strcmp(conf.version, "1.0.0") == 0, "版本归一");
	CHECK(conf.deny == 1 && conf.audit == 1, "布尔字段");
	CHECK(conf.peers == 2, "放行对端数量");
	CHECK(strcmp(conf.peer[0], "cognition-agent") == 0, "对端名");
	CHECK(conf.ports == 2 && conf.port[0] == 7766 &&
	      conf.port[1] == 7767, "AgentsIPC 端口");
	CHECK(conf.eports == 2 && conf.eport[1] == 443, "出站端口");
	CHECK(conf.exc == 2, "出站例外数量");
	CHECK(strcmp(conf.ip, "10.1.0.5/24") == 0, "prevResult 取址");
	CHECK(conf.prev[0] == '{' && strstr(conf.prev, "10.1.0.5/24"),
	      "prevResult 原文");
	CHECK(strstr(conf.prev, "routes") != NULL, "prevResult 保留路由");
}

static void test_defaults(void)
{
	struct cni_conf conf;
	char err[256] = "";
	int rc;

	load("minimal.json", &conf, err, sizeof(err), &rc);
	CHECK(rc == 0, "minimal 解析失败");
	CHECK(conf.deny == 1 && conf.cupolas == 1, "缺省拒绝与登记");
	CHECK(conf.peers == 0, "缺省无对端");
	CHECK(conf.ports == 2 && conf.port[0] == 7766, "缺省入站端口");
	CHECK(conf.eports == 1 && conf.eport[0] == 53, "缺省出站端口");
	CHECK(conf.exc == 2, "缺省出站例外");
	CHECK(strcmp(conf.state, "/run/airymax/cupolas/net") == 0,
	      "缺省状态目录");
	CHECK(strcmp(conf.ip, "10.9.0.7/24") == 0, "缺省取址");
}

static void test_bad(void)
{
	struct cni_conf conf;
	char err[256] = "";
	int rc;

	load("bad-version.json", &conf, err, sizeof(err), &rc);
	CHECK(rc == -1 && strstr(err, "cniVersion"), "拒绝过旧版本");

	load("no-prev.json", &conf, err, sizeof(err), &rc);
	CHECK(rc == 0 && conf.ip[0] == '\0' && conf.prev[0] == '\0',
	      "缺 prevResult 不阻断解析");

	rc = cni_conf_load("{", 1, &conf, err, sizeof(err));
	CHECK(rc == -1, "拒绝语法错误");
}

static void test_verdict(void)
{
	struct cni_conf conf;
	char why[32] = "";
	char err[256] = "";
	int rc;

	load("netconf.json", &conf, err, sizeof(err), &rc);
	CHECK(rc == 0, "判定夹具加载");

	CHECK(cni_verdict(&conf, 0, "cognition-agent", 7766, NULL,
			  why, sizeof(why)) == CNI_ALLOW &&
	      strcmp(why, "peer-port") == 0, "放行对端与端口");
	CHECK(cni_verdict(&conf, 0, "unknown-agent", 7766, NULL,
			  why, sizeof(why)) == CNI_DENY &&
	      strcmp(why, "peer-denied") == 0, "拒绝未知对端");
	CHECK(cni_verdict(&conf, 0, "cognition-agent", 80, NULL,
			  why, sizeof(why)) == CNI_DENY &&
	      strcmp(why, "port-denied") == 0, "拒绝非 AgentsIPC 端口");

	conf.deny = 0;
	CHECK(cni_verdict(&conf, 0, NULL, 80, NULL, why, sizeof(why)) ==
	      CNI_ALLOW && strcmp(why, "open") == 0, "关闭默认拒绝");
	conf.deny = 1;

	CHECK(cni_verdict(&conf, 1, NULL, 443, "8.8.8.8",
			  why, sizeof(why)) == CNI_ALLOW &&
	      strcmp(why, "egress-port") == 0, "放行出站白名单");
	CHECK(cni_verdict(&conf, 1, NULL, 443, "10.2.3.4",
			  why, sizeof(why)) == CNI_DENY &&
	      strcmp(why, "egress-exception") == 0, "例外优先于放行");
	CHECK(cni_verdict(&conf, 1, NULL, 22, "8.8.8.8",
			  why, sizeof(why)) == CNI_DENY &&
	      strcmp(why, "egress-denied") == 0, "拒绝未声明出站端口");
}

static void test_net(void)
{
	CHECK(cni_net_match("10.1.0.5/24", "10.0.0.0/8") == 1, "落于 10/8");
	CHECK(cni_net_match("11.0.0.1", "10.0.0.0/8") == 0, "不在 10/8");
	CHECK(cni_net_match("169.254.3.4", "169.254.0.0/16") == 1,
	      "落于链路本地");
	CHECK(cni_net_match("8.8.8.8", "0.0.0.0/0") == 1, "全零前缀");
	CHECK(cni_net_match("999.1.1.1", "10.0.0.0/8") == 0, "非法地址");
	CHECK(cni_net_match("10.1.0.5", "10.1.0.5/32") == 1, "/32 精确");
	CHECK(cni_net_match("10.1.0.6", "10.1.0.5/32") == 0, "/32 不匹配");
	CHECK(cni_net_match("10.1.0.5", "10.1.0") == 0, "残缺网段");
}

static void test_state(void)
{
	struct cni_conf conf;
	struct cni_env env;
	char err[256] = "";
	char dir[64];
	char path[256];
	char buf[1024];
	FILE *fp;
	size_t n;

	snprintf(dir, sizeof(dir), "cni-state-%ld", (long)getpid());
	memset(&conf, 0, sizeof(conf));
	cni_conf_init(&conf);
	snprintf(conf.state, sizeof(conf.state), "%s", dir);
	strcpy(conf.ip, "10.1.0.5/24");
	memset(&env, 0, sizeof(env));
	strcpy(env.container, "agent-1");
	strcpy(env.netns, "/var/run/netns/agent");
	strcpy(env.ifname, "eth0");

	CHECK(cni_state_add(&conf, &env, err, sizeof(err)) == 0,
	      err[0] ? err : "记录写入");
	CHECK(cni_state_has(&conf, &env) == 1, "记录可查");

	snprintf(path, sizeof(path), "%s/agent-1.json", dir);
	fp = fopen(path, "r");
	CHECK(fp != NULL, "记录文件存在");
	if (fp) {
		n = fread(buf, 1, sizeof(buf) - 1, fp);
		buf[n] = '\0';
		fclose(fp);
		CHECK(strstr(buf, "\"defaultDeny\":true") != NULL, "记录策略位");
		CHECK(strstr(buf, "10.1.0.5/24") != NULL, "记录地址");
		CHECK(strstr(buf, "\"allowedPeers\":[]") != NULL, "记录对端");
	}

	CHECK(cni_state_del(&conf, &env) == 0, "记录删除");
	CHECK(cni_state_has(&conf, &env) == 0, "删除后不可查");
	CHECK(cni_state_del(&conf, &env) == 0, "删除幂等");

	strcpy(env.container, "../escape");
	CHECK(cni_state_add(&conf, &env, err, sizeof(err)) == -1,
	      "拒绝越界标识");
	unlink(path);
	rmdir(dir);
}

static void test_env(void)
{
	struct cni_env env;

	setenv("CNI_COMMAND", "ADD", 1);
	setenv("CNI_CONTAINERID", "abc", 1);
	CHECK(cni_env_load(&env) == 0 && env.cmd == CNI_ADD, "ADD 命令");
	CHECK(strcmp(env.container, "abc") == 0, "容器标识");
	setenv("CNI_COMMAND", "NOPE", 1);
	CHECK(cni_env_load(&env) == -1 && env.cmd == CNI_CMD_BAD,
	      "拒绝未知命令");
	unsetenv("CNI_COMMAND");
	CHECK(cni_env_load(&env) == -1, "缺失命令");
}

int main(void)
{
	test_conf();
	test_defaults();
	test_bad();
	test_verdict();
	test_net();
	test_state();
	test_env();
	if (fails) {
		fprintf(stderr, "cni_test: %d 项失败\n", fails);
		return 1;
	}
	printf("cni_test: ok\n");
	return 0;
}
