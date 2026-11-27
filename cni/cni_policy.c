/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2025-2026 SPHARX Ltd. All Rights Reserved.
 *
 * cni_policy.c — 入站对端、AgentsIPC 端口与出站例外的确定性判定
 *
 * 判定不读取任何运行时状态，只依赖 NetConf 与传入的流五元组，这让
 * 同一条策略在门禁、审计回放与执行侧得到同一答案。出站例外先于放行
 * 判断，避免 10/8 与链路本地地址被端口白名单意外打通。
 *
 * [IND] 完全独立层（agentrt-linux 专属实现）。
 */

#include "cni_api.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* cni_num() - 取一个 0..255 的十进制段 */
static int cni_num(const char **p, unsigned *out)
{
	unsigned long val;
	char *end;

	if (**p < '0' || **p > '9')
		return -1;
	val = strtoul(*p, &end, 10);
	if (end == *p || val > 255)
		return -1;
	*p = end;
	*out = (unsigned)val;
	return 0;
}

/* cni_ipv4() - 解析点分地址与前缀，只接受完整四段 */
static int cni_ipv4(const char *s, unsigned *addr, int *bits)
{
	unsigned a, b, c, d;
	unsigned long val;
	char *end;

	if (cni_num(&s, &a) < 0 || *s++ != '.' ||
	    cni_num(&s, &b) < 0 || *s++ != '.' ||
	    cni_num(&s, &c) < 0 || *s++ != '.' ||
	    cni_num(&s, &d) < 0)
		return -1;
	*addr = (a << 24) | (b << 16) | (c << 8) | d;
	*bits = 32;
	if (*s == '/') {
		s++;
		if (*s < '0' || *s > '9')
			return -1;
		val = strtoul(s, &end, 10);
		if (end == s || val > 32)
			return -1;
		*bits = (int)val;
		s = end;
	}
	return *s == '\0' ? 0 : -1;
}

int cni_net_match(const char *addr, const char *cidr)
{
	unsigned a, n;
	int abits, nbits;
	unsigned mask;

	if (!addr || !cidr)
		return 0;
	if (cni_ipv4(addr, &a, &abits) < 0 || cni_ipv4(cidr, &n, &nbits) < 0)
		return 0;
	(void)abits;
	mask = nbits ? ~0u << (32 - nbits) : 0;
	return (a & mask) == (n & mask);
}

static int cni_peer_ok(const struct cni_conf *c, const char *peer)
{
	int i;

	if (!peer)
		return 0;
	for (i = 0; i < c->peers; i++)
		if (strcmp(c->peer[i], peer) == 0)
			return 1;
	return 0;
}

static int cni_port_ok(const int *list, int count, int port)
{
	int i;

	for (i = 0; i < count; i++)
		if (list[i] == port)
			return 1;
	return 0;
}

static int cni_exc_ok(const struct cni_conf *c, const char *dst)
{
	int i;

	if (!dst)
		return 0;
	for (i = 0; i < c->exc; i++)
		if (cni_net_match(dst, c->cidr[i]))
			return 1;
	return 0;
}

static void cni_why(char *why, size_t sz, const char *tag)
{
	if (why && sz)
		snprintf(why, sz, "%s", tag);
}

enum cni_verdict cni_verdict(const struct cni_conf *c, int egress,
			     const char *peer, int port, const char *dst,
			     char *why, size_t whysz)
{
	if (egress) {
		if (cni_exc_ok(c, dst)) {
			cni_why(why, whysz, "egress-exception");
			return CNI_DENY;
		}
		if (cni_port_ok(c->eport, c->eports, port)) {
			cni_why(why, whysz, "egress-port");
			return CNI_ALLOW;
		}
		cni_why(why, whysz, "egress-denied");
		return CNI_DENY;
	}
	if (!c->deny) {
		cni_why(why, whysz, "open");
		return CNI_ALLOW;
	}
	if (!cni_peer_ok(c, peer)) {
		cni_why(why, whysz, "peer-denied");
		return CNI_DENY;
	}
	if (!cni_port_ok(c->port, c->ports, port)) {
		cni_why(why, whysz, "port-denied");
		return CNI_DENY;
	}
	cni_why(why, whysz, "peer-port");
	return CNI_ALLOW;
}
