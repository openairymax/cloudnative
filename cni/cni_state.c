/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2025-2026 SPHARX Ltd. All Rights Reserved.
 *
 * cni_state.c — 注册记录的原子落盘、查询与删除
 *
 * 记录是插件交给执行面的唯一产物，必须整份可见或整份消失，因此先写
 * 临时文件、同步后再改名。容器标识只允许可安全用于文件名的字符，避免
 * 环境变量把路径带到目录之外。
 *
 * [IND] 完全独立层（agentrt-linux 专属实现）。
 */

#define _POSIX_C_SOURCE 200809L

#include "cni_api.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void cni_err(char *err, size_t sz, const char *fmt, ...)
{
	va_list ap;

	if (!err || sz == 0)
		return;
	va_start(ap, fmt);
	vsnprintf(err, sz, fmt, ap);
	va_end(ap);
}

/* cni_sane() - 容器标识只保留文件名安全字符 */
static int cni_sane(const char *id)
{
	if (!id || !*id)
		return -1;
	for (; *id; id++) {
		unsigned char ch = (unsigned char)*id;

		if (!isalnum(ch) && ch != '-' && ch != '_' && ch != '.')
			return -1;
	}
	return 0;
}

static int cni_path(const struct cni_conf *c, const struct cni_env *e,
		    char *out, size_t cap, const char *suffix)
{
	int n;

	if (cni_sane(e->container) < 0)
		return -1;
	n = snprintf(out, cap, "%s/%s%s", c->state, e->container, suffix);
	return n > 0 && (size_t)n < cap ? 0 : -1;
}

/* cni_esc() - 写出 JSON 字符串内容，转义引号、反斜杠与控制字符 */
static void cni_esc(FILE *fp, const char *s)
{
	const unsigned char *p = (const unsigned char *)s;

	for (; *p; p++) {
		if (*p == '"' || *p == '\\')
			fprintf(fp, "\\%c", *p);
		else if (*p < 0x20)
			fprintf(fp, "\\u%04x", *p);
		else
			fputc(*p, fp);
	}
}

static void cni_list(FILE *fp, const char *key,
		     const char (*list)[CNI_NAME_MAX], int count)
{
	int i;

	fprintf(fp, ",\"%s\":[", key);
	for (i = 0; i < count; i++) {
		fprintf(fp, "%s\"", i ? "," : "");
		cni_esc(fp, list[i]);
		fputc('"', fp);
	}
	fputc(']', fp);
}

static void cni_nums(FILE *fp, const char *key, const int *list, int count)
{
	int i;

	fprintf(fp, ",\"%s\":[", key);
	for (i = 0; i < count; i++)
		fprintf(fp, "%s%d", i ? "," : "", list[i]);
	fputc(']', fp);
}

int cni_state_add(const struct cni_conf *c, const struct cni_env *e,
		  char *err, size_t errsz)
{
	char path[CNI_PATH_MAX];
	char tmp[CNI_PATH_MAX];
	FILE *fp;
	int i;

	if (cni_path(c, e, tmp, sizeof(tmp), ".tmp") < 0 ||
	    cni_path(c, e, path, sizeof(path), ".json") < 0) {
		cni_err(err, errsz, "容器标识不可用于记录路径");
		return -1;
	}
	if (mkdir(c->state, 0755) < 0 && errno != EEXIST) {
		cni_err(err, errsz, "状态目录不可用: %s", strerror(errno));
		return -1;
	}
	fp = fopen(tmp, "w");
	if (!fp) {
		cni_err(err, errsz, "记录不可写: %s", strerror(errno));
		return -1;
	}
	fprintf(fp, "{\"containerId\":\"");
	cni_esc(fp, e->container);
	fprintf(fp, "\",\"netns\":\"");
	cni_esc(fp, e->netns);
	fprintf(fp, "\",\"ifName\":\"");
	cni_esc(fp, e->ifname);
	fprintf(fp, "\",\"ip\":\"");
	cni_esc(fp, c->ip);
	fprintf(fp, "\",\"defaultDeny\":%s,\"audit\":%s",
		c->deny ? "true" : "false", c->audit ? "true" : "false");
	cni_list(fp, "allowedPeers", c->peer, c->peers);
	cni_nums(fp, "ingressPorts", c->port, c->ports);
	cni_nums(fp, "egressPorts", c->eport, c->eports);
	fprintf(fp, ",\"egressExceptions\":[");
	for (i = 0; i < c->exc; i++) {
		fprintf(fp, "%s\"", i ? "," : "");
		cni_esc(fp, c->cidr[i]);
		fputc('"', fp);
	}
	fprintf(fp, "]}\n");
	if (fflush(fp) != 0)
		i = 1;
	else if (fsync(fileno(fp)) != 0)
		i = 1;
	else
		i = 0;
	if (fclose(fp) != 0)
		i = 1;
	if (i) {
		cni_err(err, errsz, "记录同步失败: %s", strerror(errno));
		unlink(tmp);
		return -1;
	}
	if (rename(tmp, path) < 0) {
		cni_err(err, errsz, "记录改名失败: %s", strerror(errno));
		unlink(tmp);
		return -1;
	}
	return 0;
}

int cni_state_del(const struct cni_conf *c, const struct cni_env *e)
{
	char path[CNI_PATH_MAX];

	if (cni_path(c, e, path, sizeof(path), ".json") < 0)
		return -1;
	if (unlink(path) < 0 && errno != ENOENT)
		return -1;
	return 0;
}

int cni_state_has(const struct cni_conf *c, const struct cni_env *e)
{
	char path[CNI_PATH_MAX];
	struct stat st;

	if (cni_path(c, e, path, sizeof(path), ".json") < 0)
		return -1;
	return stat(path, &st) == 0 ? 1 : 0;
}
