/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2025-2026 SPHARX Ltd. All Rights Reserved.
 *
 * crd_schema.c — Agent 对象的模式表、校验与归一
 *
 * 职责：
 *   模式以静态表声明，校验按表逐行执行，归一按同一批枚举表回填
 *   静态字面量。新增字段只改表与 crd_fill() 两处，避免出现第二套
 *   事实；表内不写默认值的字段由 crd_fill() 按设计文档取缺省。
 *
 * 数据来源：docs-linux/150-cloudnative/01-k8s-crd-design.md §2.1。
 *
 * [IND] 完全独立层（agentrt-linux 专属实现）。
 */

#include "crd_api.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CRD_FILE_MAX	(64u * 1024u)
#define CRD_REQ		0x1u

enum crd_kind {
	CRD_K_STR,
	CRD_K_ENUM,
	CRD_K_INT,
	CRD_K_BOOL,
	CRD_K_IMAGE,
	CRD_K_STRLIST,
	CRD_K_LAYERS,
	CRD_K_OBJ,
};

struct crd_rule {
	const char *path;
	enum crd_kind kind;
	long min;
	long max;
	const char *const *names;
	unsigned flags;
};

static const char *const crd_kinds[] = {
	"Agent", NULL,
};

static const char *const crd_apis[] = {
	"agent.airymaxos.dev/v1", NULL,
};

static const char *const crd_pull[] = {
	"Always", "IfNotPresent", "Never", NULL,
};

static const char *const crd_policy[] = {
	"throttle", "suspend", "hardFail", NULL,
};

static const char *const crd_forget[] = {
	"NONE", "EBBINGHAUS", "LINEAR", "ACCESS_BASED", NULL,
};

static const char *const crd_cycle[] = {
	"CoreLoopThree", NULL,
};

static const char *const crd_sched[] = {
	"stc_agent", "SCHED_NORMAL", NULL,
};

static const char *const crd_think[] = {
	"t2", "t1-f", "t1-p", "dual", NULL,
};

static const char *const crd_layer[] = {
	"L1", "L2", "L3", "L4", NULL,
};

static const struct crd_rule crd_rules[] = {
	{ "spec.image", CRD_K_IMAGE, 1, CRD_IMAGE_MAX - 1, NULL, CRD_REQ },
	{ "spec.imagePullPolicy", CRD_K_ENUM, 0, 0, crd_pull, 0 },
	{ "spec.tokenBudget.total", CRD_K_INT, 1, 0, NULL, CRD_REQ },
	{ "spec.tokenBudget.perRequestLimit", CRD_K_INT, 1, 0, NULL, 0 },
	{ "spec.tokenBudget.refillRate", CRD_K_INT, 0, 0, NULL, 0 },
	{ "spec.tokenBudget.policy", CRD_K_ENUM, 0, 0, crd_policy, 0 },
	{ "spec.memoryRovol.enabled", CRD_K_BOOL, 0, 0, NULL, 0 },
	{ "spec.memoryRovol.layers", CRD_K_LAYERS, 1, 4, NULL, 0 },
	{ "spec.memoryRovol.capacity", CRD_K_STR, 1, CRD_TEXT_MAX - 1,
	  NULL, 0 },
	{ "spec.memoryRovol.forgettingStrategy", CRD_K_ENUM, 0, 0,
	  crd_forget, 0 },
	{ "spec.cognition.cycle", CRD_K_ENUM, 0, 0, crd_cycle, 0 },
	{ "spec.cognition.scheduler", CRD_K_ENUM, 0, 0, crd_sched, 0 },
	{ "spec.cognition.thinkMode", CRD_K_ENUM, 0, 0, crd_think, 0 },
	{ "spec.security.capabilities", CRD_K_STRLIST, 0, CRD_LIST_MAX,
	  NULL, 0 },
	{ "spec.security.landlockPaths", CRD_K_STRLIST, 0, CRD_LIST_MAX,
	  NULL, 0 },
	{ "spec.security.seccompProfile", CRD_K_STR, 1, CRD_NAME_MAX - 1,
	  NULL, 0 },
	{ "spec.resources.cpu", CRD_K_STR, 1, CRD_TEXT_MAX - 1, NULL, 0 },
	{ "spec.resources.memory", CRD_K_STR, 1, CRD_TEXT_MAX - 1, NULL, 0 },
	{ "spec.resources.pids", CRD_K_INT, 1, 0, NULL, 0 },
	{ "spec.nodeSelector", CRD_K_OBJ, 0, 0, NULL, 0 },
};

/* crd_err() - 记录失败原因，恒返回 -1 */
static int crd_err(char *err, size_t cap, const char *fmt, ...)
{
	va_list ap;

	if (err != NULL && cap > 0) {
		va_start(ap, fmt);
		vsnprintf(err, cap, fmt, ap);
		va_end(ap);
	}
	return -1;
}

/* crd_eq() - 节点是否为指定字符串 */
static int crd_eq(const struct cn_node *node, const char *text)
{
	size_t len = strlen(text);

	return node != NULL && node->type == CN_STR && node->len == len &&
	       memcmp(node->str, text, len) == 0;
}

/* crd_in() - 节点是否命中枚举表 */
static int crd_in(const struct cn_node *node, const char *const *table)
{
	for (; node != NULL && *table != NULL; table++)
		if (crd_eq(node, *table))
			return 1;
	return 0;
}

/* crd_str_ok() - 字符串长度区间 */
static int crd_str_ok(const struct cn_node *node, long min, long max)
{
	if (node->type != CN_STR || (long)node->len < min)
		return 0;
	return max <= 0 || (long)node->len <= max;
}

/* crd_image_ok() - 镜像串仅允许字符集内字符 */
static int crd_image_ok(const struct cn_node *node)
{
	size_t i;

	if (!crd_str_ok(node, 1, CRD_IMAGE_MAX - 1))
		return 0;
	for (i = 0; i < node->len; i++) {
		char ch = node->str[i];

		if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
		    (ch >= '0' && ch <= '9') || ch == '.' || ch == '/' ||
		    ch == ':' || ch == '_' || ch == '-')
			continue;
		return 0;
	}
	return 1;
}

/* crd_layer_bit() - 层名对应的位；未知层返回 0 */
static unsigned crd_layer_bit(const struct cn_node *node)
{
	size_t i;

	for (i = 0; crd_layer[i] != NULL; i++)
		if (crd_eq(node, crd_layer[i]))
			return 1u << i;
	return 0;
}

/* crd_layers_ok() - 层数组：1~4 项、取值合法且不重复 */
static int crd_layers_ok(const struct cn_node *node)
{
	const struct cn_node *it;
	unsigned seen = 0;
	long count = 0;

	if (node->type != CN_ARR)
		return 0;
	for (it = node->child; it != NULL; it = it->sibling) {
		unsigned bit = crd_layer_bit(it);

		if (bit == 0 || (seen & bit) != 0)
			return 0;
		seen |= bit;
		if (++count > 4)
			return 0;
	}
	return count >= 1;
}

/* crd_list_ok() - 字符串数组：项数区间内且每项非空 */
static int crd_list_ok(const struct cn_node *node, long max)
{
	const struct cn_node *it;
	long count = 0;

	if (node->type != CN_ARR)
		return 0;
	for (it = node->child; it != NULL; it = it->sibling) {
		if (!crd_str_ok(it, 1, CRD_NAME_MAX - 1))
			return 0;
		if (++count > max)
			return 0;
	}
	return 1;
}

/* crd_rule_ok() - 单字段按模式判定 */
static int crd_rule_ok(const struct cn_node *node,
		       const struct crd_rule *rule)
{
	long val;

	switch (rule->kind) {
	case CRD_K_STR:
		return crd_str_ok(node, rule->min, rule->max);
	case CRD_K_ENUM:
		return crd_in(node, rule->names);
	case CRD_K_INT:
		return cn_as_long(node, &val) == 0 && val >= rule->min;
	case CRD_K_BOOL:
		return node->type == CN_BOOL;
	case CRD_K_IMAGE:
		return crd_image_ok(node);
	case CRD_K_STRLIST:
		return crd_list_ok(node, rule->max);
	case CRD_K_LAYERS:
		return crd_layers_ok(node);
	case CRD_K_OBJ:
		return node->type == CN_OBJ;
	}
	return 0;
}

/* crd_rule_one() - 缺省与判定合一，失败时写出字段名 */
static int crd_rule_one(const struct cn_node *root,
			const struct crd_rule *rule, char *err, size_t cap)
{
	const struct cn_node *node = cn_get(root, rule->path);

	if (node == NULL) {
		if ((rule->flags & CRD_REQ) == 0)
			return 0;
		return crd_err(err, cap, "%s: 缺必需字段", rule->path);
	}
	if (crd_rule_ok(node, rule))
		return 0;
	return crd_err(err, cap, "%s: 取值不符（%s）", rule->path,
		       cn_type_name(node->type));
}

/* crd_head_ok() - 对象头：类型、API 组与名称 */
static int crd_head_ok(const struct cn_node *root, char *err, size_t cap)
{
	const struct cn_node *name = cn_get(root, "metadata.name");

	if (!crd_in(cn_get(root, "kind"), crd_kinds))
		return crd_err(err, cap, "kind: 非 Agent");
	if (!crd_in(cn_get(root, "apiVersion"), crd_apis))
		return crd_err(err, cap, "apiVersion: 非 agent.airymaxos.dev/v1");
	if (!crd_str_ok(name, 1, CRD_NAME_MAX - 1))
		return crd_err(err, cap, "metadata.name: 缺失或过长");
	return 0;
}

/* crd_cross_ok() - 跨字段约束：单次上限不超过总预算 */
static int crd_cross_ok(const struct cn_node *root, char *err, size_t cap)
{
	const struct cn_node *budget = cn_get(root, "spec.tokenBudget");
	long total = 0;
	long per;

	if (budget == NULL || budget->type != CN_OBJ)
		return 0;
	if (cn_as_long(cn_get(budget, "total"), &total) < 0)
		return 0;
	if (cn_as_long(cn_get(budget, "perRequestLimit"), &per) < 0)
		return 0;
	if (per > total)
		return crd_err(err, cap,
			       "spec.tokenBudget.perRequestLimit: 超过 total");
	return 0;
}

int crd_check(const struct cn_node *root, char *err, size_t cap)
{
	size_t i;

	if (root == NULL || root->type != CN_OBJ)
		return crd_err(err, cap, "根节点不是对象");
	if (crd_head_ok(root, err, cap) < 0)
		return -1;
	for (i = 0; i < sizeof(crd_rules) / sizeof(crd_rules[0]); i++)
		if (crd_rule_one(root, &crd_rules[i], err, cap) < 0)
			return -1;
	return crd_cross_ok(root, err, cap);
}

/* crd_pick() - 取枚举字段的静态字面量，缺失或越界取缺省 */
static const char *crd_pick(const struct cn_node *root, const char *path,
			    const char *const *table, const char *dflt)
{
	const struct cn_node *node = cn_get(root, path);

	for (; node != NULL && *table != NULL; table++)
		if (crd_eq(node, *table))
			return *table;
	return dflt;
}

/* crd_text() - 复制字符串字段，截断到缓冲上限 */
static void crd_text(char *dst, size_t cap, const struct cn_node *node)
{
	size_t len;

	if (node == NULL || node->type != CN_STR)
		return;
	len = node->len < cap - 1 ? node->len : cap - 1;
	memcpy(dst, node->str, len);
	dst[len] = '\0';
}

/* crd_count() - 数组项数；非数组记 0 */
static unsigned crd_count(const struct cn_node *node)
{
	const struct cn_node *it;
	unsigned n = 0;

	if (node == NULL || node->type != CN_ARR)
		return 0;
	for (it = node->child; it != NULL; it = it->sibling)
		n++;
	return n;
}

/* crd_budget() - 预算字段归一 */
static void crd_budget(struct crd_agent *out, const struct cn_node *node)
{
	long val;

	if (node == NULL || node->type != CN_OBJ)
		return;
	if (cn_as_long(cn_get(node, "total"), &val) == 0)
		out->budget.total = val;
	if (cn_as_long(cn_get(node, "perRequestLimit"), &val) == 0)
		out->budget.per_req = val;
	if (cn_as_long(cn_get(node, "refillRate"), &val) == 0)
		out->budget.refill = val;
	out->budget.policy = crd_pick(node, "policy", crd_policy,
				      "throttle");
	if (out->budget.per_req == 0)
		out->budget.per_req = out->budget.total;
}

/* crd_layers() - 层数组折算成位图 */
static unsigned crd_layers(const struct cn_node *node)
{
	const struct cn_node *it;
	unsigned mask = 0;

	if (node == NULL || node->type != CN_ARR)
		return 0;
	for (it = node->child; it != NULL; it = it->sibling)
		mask |= crd_layer_bit(it);
	return mask;
}

int crd_fill(const struct cn_node *root, struct crd_agent *out)
{
	const struct cn_node *res;
	int ok;

	if (root == NULL || out == NULL)
		return -1;
	memset(out, 0, sizeof(*out));
	crd_text(out->name, sizeof(out->name), cn_get(root, "metadata.name"));
	crd_text(out->ns, sizeof(out->ns), cn_get(root, "metadata.namespace"));
	crd_text(out->image, sizeof(out->image),
		 cn_get(root, "spec.image"));
	if (out->image[0] == '\0')
		return -1;
	out->pull = crd_pick(root, "spec.imagePullPolicy", crd_pull,
			     "IfNotPresent");
	crd_budget(out, cn_get(root, "spec.tokenBudget"));
	out->layers = crd_layers(cn_get(root, "spec.memoryRovol.layers"));
	crd_text(out->capacity, sizeof(out->capacity),
		 cn_get(root, "spec.memoryRovol.capacity"));
	if (out->capacity[0] == '\0')
		strcpy(out->capacity, "100Gi");
	out->forget = crd_pick(root, "spec.memoryRovol.forgettingStrategy",
			       crd_forget, "EBBINGHAUS");
	out->cycle = crd_pick(root, "spec.cognition.cycle", crd_cycle,
			      "CoreLoopThree");
	out->sched = crd_pick(root, "spec.cognition.scheduler", crd_sched,
			      "stc_agent");
	out->think = crd_pick(root, "spec.cognition.thinkMode", crd_think,
			      "dual");
	crd_text(out->seccomp, sizeof(out->seccomp),
		 cn_get(root, "spec.security.seccompProfile"));
	if (out->seccomp[0] == '\0')
		strcpy(out->seccomp, "agentrt-default");
	out->caps = crd_count(cn_get(root, "spec.security.capabilities"));
	out->paths = crd_count(cn_get(root, "spec.security.landlockPaths"));
	res = cn_get(root, "spec.resources");
	out->cpu_milli = crd_cpu_milli(cn_get(res, "cpu"), &ok);
	if (ok == 0)
		out->cpu_milli = 2000;
	out->mem_mib = crd_mem_mib(cn_get(res, "memory"), &ok);
	if (ok == 0)
		out->mem_mib = 4096;
	out->pids = 100;
	if (cn_as_long(cn_get(res, "pids"), &out->pids) < 0)
		out->pids = 100;
	return 0;
}

int crd_load(const char *path, struct crd_agent *out, char *err,
	     size_t cap)
{
	struct cn_doc *doc = NULL;
	char *buf;
	FILE *fp;
	size_t len;
	int rc, bad;

	if (path == NULL || out == NULL)
		return crd_err(err, cap, "参数为空");
	buf = malloc(CRD_FILE_MAX + 1);
	if (buf == NULL)
		return crd_err(err, cap, "内存不足");
	fp = fopen(path, "rb");
	if (fp == NULL) {
		free(buf);
		return crd_err(err, cap, "%s: 无法打开", path);
	}
	len = fread(buf, 1, CRD_FILE_MAX, fp);
	bad = ferror(fp);
	fclose(fp);
	if (bad != 0) {
		free(buf);
		return crd_err(err, cap, "%s: 读取失败", path);
	}
	buf[len] = '\0';
	rc = cn_parse(buf, len, &doc);
	free(buf);
	if (rc < 0)
		return crd_err(err, cap, "JSON 语法错误");
	if (crd_check(cn_root(doc), err, cap) < 0) {
		cn_free(doc);
		return -1;
	}
	rc = crd_fill(cn_root(doc), out);
	cn_free(doc);
	if (rc < 0)
		return crd_err(err, cap, "image 缺失");
	return 0;
}
