/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2025-2026 SPHARX Ltd. All Rights Reserved.
 *
 * crd_units.c — 资源配额的字符串折算
 *
 * 职责：
 *   spec.resources.cpu / memory 是 k8s 数量串，规划动作需要可比较的
 *   毫核与 MiB。本件只做折算，不判断是否超过节点容量——容量属调度侧。
 *
 * [IND] 完全独立层（agentrt-linux 专属实现）。
 */

#include "crd_api.h"

#include <stdlib.h>
#include <string.h>

/* 内存单位到字节的倍率；两套后缀按 k8s 语义区分 */
static const struct crd_unit_ {
	const char *tag;
	double mul;
} crd_unit[] = {
	{ "Ki", 1024.0 },
	{ "Mi", 1048576.0 },
	{ "Gi", 1073741824.0 },
	{ "Ti", 1099511627776.0 },
	{ "K", 1000.0 },
	{ "M", 1000000.0 },
	{ "G", 1000000000.0 },
	{ "T", 1000000000000.0 },
};

/*
 * crd_text_copy() - 把字符串节点拷进定长缓冲
 *
 * 返回节点字节数；类型不符或超长返回 0。
 */
static size_t crd_str_ncpy(const struct cn_node *node, char *buf,
			   size_t cap)
{
	if (node == NULL || node->type != CN_STR || node->len == 0 ||
	    node->len >= cap)
		return 0;
	memcpy(buf, node->str, node->len);
	buf[node->len] = '\0';
	return node->len;
}

long crd_cpu_milli(const struct cn_node *node, int *ok)
{
	char buf[CRD_TEXT_MAX];
	char *end;
	long val;
	size_t len;

	if (ok != NULL)
		*ok = 0;
	len = crd_str_ncpy(node, buf, sizeof(buf));
	if (len == 0)
		return 0;
	if (buf[len - 1] == 'm') {
		if (len == 1)
			return 0;
		buf[len - 1] = '\0';
		val = strtol(buf, &end, 10);
		if (*end != '\0' || val < 0)
			return 0;
		if (ok != NULL)
			*ok = 1;
		return val;
	}
	val = strtol(buf, &end, 10);
	if (*end != '\0' || val < 0 || val > 100000)
		return 0;
	if (ok != NULL)
		*ok = 1;
	return val * 1000;
}

long crd_mem_mib(const struct cn_node *node, int *ok)
{
	char buf[CRD_TEXT_MAX];
	const struct crd_unit_ *unit = NULL;
	char *end;
	double bytes;
	size_t len, i;

	if (ok != NULL)
		*ok = 0;
	len = crd_str_ncpy(node, buf, sizeof(buf));
	if (len == 0)
		return 0;
	for (i = 0; i < sizeof(crd_unit) / sizeof(crd_unit[0]); i++) {
		size_t tag = strlen(crd_unit[i].tag);

		if (len > tag &&
		    strcmp(buf + len - tag, crd_unit[i].tag) == 0) {
			unit = &crd_unit[i];
			buf[len - tag] = '\0';
			break;
		}
	}
	bytes = strtod(buf, &end);
	if (*end != '\0' || bytes < 0)
		return 0;
	bytes = unit != NULL ? bytes * unit->mul : bytes * 1048576.0;
	if (bytes > 1e15)
		return 0;
	if (ok != NULL)
		*ok = 1;
	return (long)((bytes + 1048575.0) / 1048576.0);
}
