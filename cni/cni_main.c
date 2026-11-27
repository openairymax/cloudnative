/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2025-2026 SPHARX Ltd. All Rights Reserved.
 *
 * cni_main.c — airymaxos-cni 的 CNI 协议入口与离线判定入口
 *
 * CNI 模式下由 CNI_COMMAND 决定动作：ADD 校验链式装配、落注册记录并
 * 原样回传 prevResult；CHECK 核对记录仍在；DEL 幂等清理。离线模式
 * eval 用同一份 NetConf 与判定件复算单条流，供门禁与审计回放使用。
 *
 * [IND] 完全独立层（agentrt-linux 专属实现）。
 */

#define _POSIX_C_SOURCE 200809L

#include "cni_api.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CNI_EXIT_USE	2
#define CNI_EXIT_BAD	3
#define CNI_EXIT_DENY	6

static int cni_read(char *buf, size_t cap, size_t *len)
{
	size_t n = 0;
	size_t got;

	while (n < cap - 1) {
		got = fread(buf + n, 1, cap - 1 - n, stdin);
		if (got == 0) {
			if (ferror(stdin))
				return -1;
			break;
		}
		n += got;
	}
	buf[n] = '\0';
	*len = n;
	return 0;
}

static int cni_file(const char *path, char *buf, size_t cap, size_t *len)
{
	FILE *fp = fopen(path, "r");
	size_t n;

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

static void cni_version_out(void)
{
	printf("{\"cniVersion\":\"%s\",\"supportedVersions\":"
	       "[\"%s\",\"0.4.0\",\"0.3.1\"]}\n", CNI_SPEC_VER, CNI_SPEC_VER);
}

static int cni_usage(const char *msg)
{
	if (msg)
		fprintf(stderr, "airy-cni: %s\n", msg);
	fprintf(stderr,
		"用法: airy-cni <CNI_COMMAND 环境驱动>\n"
		"      airy-cni version\n"
		"      airy-cni eval --dir ingress|egress --conf FILE"
		" [--peer NAME] [--port N] [--dst ADDR]\n");
	return CNI_EXIT_USE;
}

static int cni_eval(int argc, char **argv)
{
	char buf[CNI_STDIN_MAX];
	char err[256];
	char why[32] = "";
	const char *conf_path = NULL;
	const char *peer = NULL;
	const char *dst = NULL;
	struct cni_conf conf;
	size_t len = 0;
	int egress = -1;
	int port = 0;
	int i;

	for (i = 2; i < argc; i++) {
		if (strcmp(argv[i], "--dir") == 0 && i + 1 < argc) {
			const char *dir = argv[++i];

			if (strcmp(dir, "egress") == 0)
				egress = 1;
			else if (strcmp(dir, "ingress") == 0)
				egress = 0;
			else
				return cni_usage("--dir 只接受 ingress/egress");
		} else if (strcmp(argv[i], "--conf") == 0 && i + 1 < argc) {
			conf_path = argv[++i];
		} else if (strcmp(argv[i], "--peer") == 0 && i + 1 < argc) {
			peer = argv[++i];
		} else if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
			port = atoi(argv[++i]);
		} else if (strcmp(argv[i], "--dst") == 0 && i + 1 < argc) {
			dst = argv[++i];
		} else {
			return cni_usage("未知参数");
		}
	}
	if (egress < 0 || !conf_path)
		return cni_usage("eval 需要 --dir 与 --conf");
	if (cni_file(conf_path, buf, sizeof(buf), &len) < 0) {
		fprintf(stderr, "airy-cni: NetConf 不可读: %s\n", conf_path);
		return CNI_EXIT_USE;
	}
	if (cni_conf_load(buf, len, &conf, err, sizeof(err)) < 0) {
		fprintf(stderr, "airy-cni: %s\n", err);
		return CNI_EXIT_BAD;
	}
	if (cni_verdict(&conf, egress, peer, port, dst, why, sizeof(why)) ==
	    CNI_ALLOW) {
		printf("verdict=allow reason=%s\n", why);
		return 0;
	}
	printf("verdict=deny reason=%s\n", why);
	return CNI_EXIT_DENY;
}

static int cni_proto(struct cni_env *env)
{
	char buf[CNI_STDIN_MAX];
	char err[256];
	struct cni_conf conf;
	size_t len = 0;

	if (env->cmd == CNI_VERSION) {
		cni_version_out();
		return 0;
	}
	if (cni_read(buf, sizeof(buf), &len) < 0) {
		fprintf(stderr, "airy-cni: 读取 stdin 失败\n");
		return CNI_EXIT_USE;
	}
	if (env->cmd == CNI_DEL) {
		cni_conf_init(&conf);
		(void)cni_conf_load(buf, len, &conf, err, sizeof(err));
		if (cni_state_del(&conf, env) < 0) {
			fprintf(stderr, "airy-cni: DEL 记录清理失败\n");
			return CNI_EXIT_BAD;
		}
		return 0;
	}
	if (cni_conf_load(buf, len, &conf, err, sizeof(err)) < 0) {
		fprintf(stderr, "airy-cni: %s\n", err);
		return CNI_EXIT_BAD;
	}
	if (env->cmd == CNI_ADD) {
		if (conf.prev[0] == '\0') {
			fprintf(stderr,
				"airy-cni: 缺少 prevResult，须作为链式插件运行\n");
			return CNI_EXIT_BAD;
		}
		if (conf.cupolas &&
		    cni_state_add(&conf, env, err, sizeof(err)) < 0) {
			fprintf(stderr, "airy-cni: %s\n", err);
			return CNI_EXIT_BAD;
		}
		printf("%s\n", conf.prev);
		return 0;
	}
	if (env->cmd == CNI_CHECK) {
		int has;

		if (!conf.cupolas)
			return 0;
		has = cni_state_has(&conf, env);
		if (has != 1) {
			fprintf(stderr, "airy-cni: CHECK 未找到注册记录\n");
			return CNI_EXIT_BAD;
		}
		return 0;
	}
	return cni_usage("未知 CNI 命令");
}

int main(int argc, char **argv)
{
	struct cni_env env;

	if (argc > 1) {
		if (strcmp(argv[1], "version") == 0) {
			cni_version_out();
			return 0;
		}
		if (strcmp(argv[1], "eval") == 0)
			return cni_eval(argc, argv);
		return cni_usage("未知子命令");
	}
	if (cni_env_load(&env) < 0) {
		fprintf(stderr, "airy-cni: CNI_COMMAND 缺失或非法\n");
		return CNI_EXIT_USE;
	}
	return cni_proto(&env);
}
