/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2025-2026 SPHARX Ltd. All Rights Reserved.
 *
 * cni_conf.c — NetConf 解析、缺省补齐与 prevResult 取址
 *
 * 先落设计文档的基线（默认拒绝、AgentsIPC 端口、常见出站例外），再
 * 用 stdin 的对象覆盖；任何已声明但取不到的字段都判负，避免把拼写
 * 错误当成「未配置」而放大权限。
 *
 * [IND] 完全独立层（agentrt-linux 专属实现）。
 */

#define _POSIX_C_SOURCE 200809L

#include "cni_api.h"

#include "cn_json.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void cni_err(char *err, size_t sz, const char *fmt, ...)
{
	va_list ap;

	if (!err || sz == 0)
		return;
	va_start(ap, fmt);
	vsnprintf(err, sz, fmt, ap);
	va_end(ap);
}

/* cni_str() - 把字符串节点复制进定长缓冲 */
static int cni_str(char *dst, size_t cap, const struct cn_node *node)
{
	if (!node || node->type != CN_STR || node->len >= cap)
		return -1;
	memcpy(dst, node->str, node->len);
	dst[node->len] = '\0';
	return 0;
}

/* cni_bool() - 布尔字段；缺失返回 0，类型不符返回 -1 */
static int cni_bool(const struct cn_node *node, int *out)
{
	if (!node)
		return 0;
	if (node->type != CN_BOOL)
		return -1;
	*out = node->boolean;
	return 0;
}

/* cni_portv() - 单个端口；只接受 1..65535 */
static int cni_portv(const struct cn_node *node, int *out)
{
	long val;

	if (!node || cn_as_long(node, &val) < 0 || val < 1 || val > 65535)
		return -1;
	*out = (int)val;
	return 0;
}

/* cni_names() - 字符串数组；@cap 为数组容量 */
static int cni_names(const struct cn_node *arr,
		     char dst[][CNI_NAME_MAX], int cap, int *count)
{
	const struct cn_node *it;
	int n = 0;

	if (!arr || arr->type != CN_ARR)
		return -1;
	for (it = arr->child; it; it = it->sibling) {
		if (n >= cap || cni_str(dst[n], CNI_NAME_MAX, it) < 0)
			return -1;
		n++;
	}
	*count = n;
	return 0;
}

/* cni_ports() - 数值数组 */
static int cni_ports(const struct cn_node *arr, int *dst, int cap, int *count)
{
	const struct cn_node *it;
	int n = 0;

	if (!arr || arr->type != CN_ARR)
		return -1;
	for (it = arr->child; it; it = it->sibling) {
		if (n >= cap || cni_portv(it, &dst[n]) < 0)
			return -1;
		n++;
	}
	*count = n;
	return 0;
}

/* cni_cidrs() - 网段数组 */
static int cni_cidrs(const struct cn_node *arr,
		     char dst[][CNI_IP_MAX], int cap, int *count)
{
	const struct cn_node *it;
	int n = 0;

	if (!arr || arr->type != CN_ARR)
		return -1;
	for (it = arr->child; it; it = it->sibling) {
		if (n >= cap || cni_str(dst[n], CNI_IP_MAX, it) < 0)
			return -1;
		n++;
	}
	*count = n;
	return 0;
}

/* cni_span() - 从开括号起返回配平区段长度 */
static size_t cni_span(const char *s, size_t len)
{
	int depth = 0;
	int in_str = 0;
	int esc = 0;
	size_t i;

	for (i = 0; i < len; i++) {
		char ch = s[i];

		if (in_str) {
			if (esc)
				esc = 0;
			else if (ch == '\\')
				esc = 1;
			else if (ch == '"')
				in_str = 0;
			continue;
		}
		if (ch == '"') {
			in_str = 1;
		} else if (ch == '{' || ch == '[') {
			depth++;
		} else if (ch == '}' || ch == ']') {
			depth--;
			if (depth == 0)
				return i + 1;
		}
	}
	return 0;
}

/* cni_rawprev() - 抓取 prevResult 原文，供 ADD 原样回传 */
static int cni_rawprev(const char *text, size_t len, char *out, size_t cap,
		       char *err, size_t errsz)
{
	static const char key[] = "\"prevResult\"";
	const char *end = text + len;
	const char *p;
	size_t i, n;

	out[0] = '\0';
	for (i = 0; i + sizeof(key) - 1 <= len; i++) {
		if (memcmp(text + i, key, sizeof(key) - 1) != 0)
			continue;
		p = text + i + sizeof(key) - 1;
		while (p < end && (*p == ' ' || *p == '\t' ||
				   *p == '\n' || *p == '\r'))
			p++;
		if (p >= end || *p != ':')
			continue;
		p++;
		while (p < end && (*p == ' ' || *p == '\t' ||
				   *p == '\n' || *p == '\r'))
			p++;
		if (p >= end || (*p != '{' && *p != '['))
			return -1;
		n = cni_span(p, (size_t)(end - p));
		if (!n || n >= cap) {
			cni_err(err, errsz, "prevResult: 过大或未配平");
			return -1;
		}
		memcpy(out, p, n);
		out[n] = '\0';
		return 0;
	}
	return 0;
}

static const char *const cni_def_cidr[] = {
	"10.0.0.0/8",
	"169.254.0.0/16",
};

void cni_conf_init(struct cni_conf *c)
{
	strcpy(c->version, CNI_SPEC_VER);
	c->cupolas = 1;
	c->deny = 1;
	c->audit = 1;
	c->peers = 0;
	c->ports = 2;
	c->port[0] = 7766;
	c->port[1] = 7767;
	c->exc = 2;
	strcpy(c->cidr[0], cni_def_cidr[0]);
	strcpy(c->cidr[1], cni_def_cidr[1]);
	c->eports = 1;
	c->eport[0] = 53;
	strcpy(c->state, "/run/airymax/cupolas/net");
	c->ip[0] = '\0';
	c->prev[0] = '\0';
}

/* cni_prev() - 从 prevResult 取首个容器地址 */
static int cni_prev(const struct cn_node *root,
		    char *ip, size_t cap, char *err, size_t errsz)
{
	const struct cn_node *ips = cn_get(root, "prevResult.ips");
	const struct cn_node *first;
	const struct cn_node *addr;

	ip[0] = '\0';
	if (!ips)
		return 0;
	if (ips->type != CN_ARR) {
		cni_err(err, errsz, "prevResult.ips: 非数组");
		return -1;
	}
	first = ips->child;
	if (!first)
		return 0;
	addr = cn_get(first, "address");
	if (cni_str(ip, cap, addr) < 0) {
		cni_err(err, errsz, "prevResult.ips[0].address: 不可用");
		return -1;
	}
	return 0;
}

int cni_cmd_parse(const char *name, enum cni_cmd *out)
{
	static const char *const names[] = {
		"ADD", "DEL", "CHECK", "VERSION",
	};
	int i;

	if (!name)
		return -1;
	for (i = 0; i < (int)(sizeof(names) / sizeof(names[0])); i++) {
		if (strcmp(name, names[i]) == 0) {
			*out = (enum cni_cmd)i;
			return 0;
		}
	}
	return -1;
}

const char *cni_cmd_name(enum cni_cmd cmd)
{
	switch (cmd) {
	case CNI_ADD:		return "ADD";
	case CNI_DEL:		return "DEL";
	case CNI_CHECK:		return "CHECK";
	case CNI_VERSION:	return "VERSION";
	default:		return "bad";
	}
}

int cni_env_load(struct cni_env *out)
{
	const char *cmd = getenv("CNI_COMMAND");

	memset(out, 0, sizeof(*out));
	if (!cmd || cni_cmd_parse(cmd, &out->cmd) < 0) {
		out->cmd = CNI_CMD_BAD;
		return -1;
	}
	snprintf(out->container, sizeof(out->container), "%s",
		 getenv("CNI_CONTAINERID") ? getenv("CNI_CONTAINERID") : "");
	snprintf(out->netns, sizeof(out->netns), "%s",
		 getenv("CNI_NETNS") ? getenv("CNI_NETNS") : "");
	snprintf(out->ifname, sizeof(out->ifname), "%s",
		 getenv("CNI_IFNAME") ? getenv("CNI_IFNAME") : "");
	snprintf(out->args, sizeof(out->args), "%s",
		 getenv("CNI_ARGS") ? getenv("CNI_ARGS") : "");
	return 0;
}

int cni_conf_load(const char *text, size_t len, struct cni_conf *out,
		  char *err, size_t errsz)
{
	struct cn_doc *doc = NULL;
	const struct cn_node *root;
	const struct cn_node *n;

	cni_conf_init(out);
	if (cn_parse(text, len, &doc) < 0) {
		cni_err(err, errsz, "NetConf: JSON 语法错误");
		return -1;
	}
	root = cn_root(doc);
	if (cni_str(out->version, sizeof(out->version),
		    cn_get(root, "cniVersion")) < 0 ||
	    cni_str(out->name, sizeof(out->name),
		    cn_get(root, "name")) < 0 ||
	    cni_str(out->type, sizeof(out->type),
		    cn_get(root, "type")) < 0) {
		cni_err(err, errsz, "NetConf: cniVersion/name/type 缺失");
		goto fail;
	}
	if (strncmp(out->version, "1.", 2) != 0 &&
	    strcmp(out->version, "0.4.0") != 0 &&
	    strcmp(out->version, "0.3.1") != 0) {
		cni_err(err, errsz, "cniVersion 不支持: %s", out->version);
		goto fail;
	}

	n = cn_get(root, "cupolasEnabled");
	if (cni_bool(n, &out->cupolas) < 0) {
		cni_err(err, errsz, "cupolasEnabled: 非布尔");
		goto fail;
	}
	n = cn_get(root, "defaultDeny");
	if (cni_bool(n, &out->deny) < 0) {
		cni_err(err, errsz, "defaultDeny: 非布尔");
		goto fail;
	}
	n = cn_get(root, "auditEnabled");
	if (cni_bool(n, &out->audit) < 0) {
		cni_err(err, errsz, "auditEnabled: 非布尔");
		goto fail;
	}

	if ((n = cn_get(root, "allowedPeers")) &&
	    cni_names(n, out->peer, CNI_PEER_MAX, &out->peers) < 0) {
		cni_err(err, errsz, "allowedPeers: 非法数组");
		goto fail;
	}
	if ((n = cn_get(root, "ingressPorts")) &&
	    cni_ports(n, out->port, CNI_PEER_MAX, &out->ports) < 0) {
		cni_err(err, errsz, "ingressPorts: 非法端口数组");
		goto fail;
	}
	if ((n = cn_get(root, "egressPorts")) &&
	    cni_ports(n, out->eport, CNI_PEER_MAX, &out->eports) < 0) {
		cni_err(err, errsz, "egressPorts: 非法端口数组");
		goto fail;
	}
	if ((n = cn_get(root, "egressExceptions")) &&
	    cni_cidrs(n, out->cidr, CNI_CIDR_MAX, &out->exc) < 0) {
		cni_err(err, errsz, "egressExceptions: 非法网段数组");
		goto fail;
	}
	if ((n = cn_get(root, "stateDir")) &&
	    cni_str(out->state, sizeof(out->state), n) < 0) {
		cni_err(err, errsz, "stateDir: 不可用");
		goto fail;
	}

	if (cni_prev(root, out->ip, sizeof(out->ip), err, errsz) < 0)
		goto fail;
	if (cni_rawprev(text, len, out->prev, sizeof(out->prev),
			err, errsz) < 0)
		goto fail;

	cn_free(doc);
	return 0;
fail:
	cn_free(doc);
	return -1;
}
