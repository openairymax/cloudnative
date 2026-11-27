/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2025-2026 SPHARX Ltd. All Rights Reserved.
 *
 * shm_spec.c — bundle 读取、注解契约校验与动作规划
 *
 * 职责：
 *   config.json 只取 process.args、process.env、root.path 与
 *   annotations；注解键与取值以设计文档 §6 为准，缺省值在归一阶段
 *   补齐，未识别的层名、调度类与能力名一律判负，避免静默降级。
 *
 * [IND] 完全独立层（agentrt-linux 专属实现）。
 */

#define _POSIX_C_SOURCE 200809L

#include "shm_api.h"
#include "cn_json.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SHM_JSON_MAX	(64 * 1024)
#define SHM_DEF_BUDGET	1000000L

/* shm_err() - 写一条可读原因 */
static void shm_err(char *err, size_t cap, const char *fmt, ...)
{
	va_list ap;

	if (!err || cap == 0)
		return;
	va_start(ap, fmt);
	vsnprintf(err, cap, fmt, ap);
	va_end(ap);
}

/* shm_copy() - 把 JSON 字符串按长度复制到定长缓冲 */
static int shm_copy(char *dst, size_t cap, const struct cn_node *node)
{
	size_t n;

	if (!node || node->type != CN_STR || !node->str)
		return -1;
	n = node->len;
	if (n + 1 > cap)
		return -1;
	memcpy(dst, node->str, n);
	dst[n] = '\0';
	return 0;
}

/* shm_join() - 拼接目录与文件名 */
static int shm_join(char *dst, size_t cap, const char *base,
		    const char *name)
{
	int n;

	n = snprintf(dst, cap, "%s/%s", base, name);
	if (n < 0 || (size_t)n >= cap)
		return -1;
	return 0;
}

/* shm_read() - 读整个文件，成功时回填 malloc 缓冲 */
static int shm_read(const char *path, char **out, size_t *len)
{
	FILE *fp;
	long size;
	char *buf;

	fp = fopen(path, "rb");
	if (!fp)
		return -1;
	if (fseek(fp, 0, SEEK_END) != 0) {
		fclose(fp);
		return -1;
	}
	size = ftell(fp);
	if (size <= 0 || size > SHM_JSON_MAX || fseek(fp, 0, SEEK_SET)) {
		fclose(fp);
		return -1;
	}
	buf = malloc((size_t)size);
	if (!buf) {
		fclose(fp);
		return -1;
	}
	if (fread(buf, 1, (size_t)size, fp) != (size_t)size) {
		free(buf);
		fclose(fp);
		return -1;
	}
	fclose(fp);
	*out = buf;
	*len = (size_t)size;
	return 0;
}

/* shm_memb() - 按对象键取成员 */
static const struct cn_node *shm_memb(const struct cn_node *obj,
				      const char *key)
{
	const struct cn_node *it;

	if (!obj || obj->type != CN_OBJ)
		return NULL;
	for (it = obj->child; it; it = it->sibling) {
		if (it->key && strcmp(it->key, key) == 0)
			return it;
	}
	return NULL;
}

/* shm_layers() - 解析逗号分隔的记忆层名 */
static int shm_layers(const char *text, unsigned *out, char *err,
		      size_t errsz)
{
	static const struct {
		const char *name;
		unsigned bit;
	} map[] = {
		{ "L1", SHM_L1 }, { "L2", SHM_L2 },
		{ "L3", SHM_L3 }, { "L4", SHM_L4 },
	};
	char buf[SHM_TEXT_MAX];
	char *tok, *save = NULL;
	unsigned bits = 0;
	size_t i;

	if (strcmp(text, "disabled") == 0) {
		*out = 0;
		return 0;
	}
	if (strlen(text) >= sizeof(buf)) {
		shm_err(err, errsz, "memory-rovol: 取值过长");
		return -1;
	}
	strcpy(buf, text);
	for (tok = strtok_r(buf, ",", &save); tok;
	     tok = strtok_r(NULL, ",", &save)) {
		for (i = 0; i < sizeof(map) / sizeof(map[0]); i++) {
			if (strcmp(tok, map[i].name) == 0)
				break;
		}
		if (i == sizeof(map) / sizeof(map[0])) {
			shm_err(err, errsz, "memory-rovol: 未知层 %s", tok);
			return -1;
		}
		bits |= map[i].bit;
	}
	if (bits == 0) {
		shm_err(err, errsz, "memory-rovol: 空列表");
		return -1;
	}
	*out = bits;
	return 0;
}

/* shm_caps() - 解析逗号分隔的能力名 */
static int shm_caps(const char *text, unsigned long long *out, char *err,
		    size_t errsz)
{
	static const char *const names[] = {
		"MEMORY_READ", "MEMORY_WRITE", "IPC_SEND", "IPC_RECV",
		"SCHED_SUBMIT", "NET_ACCESS", "FILE_READ", "FILE_WRITE",
		"TOOL_EXEC", "ADMIN",
	};
	unsigned long long bits = 0;
	char buf[SHM_ARG_LEN];
	char *tok, *save = NULL;
	size_t i;

	if (strlen(text) >= sizeof(buf)) {
		shm_err(err, errsz, "capabilities: 取值过长");
		return -1;
	}
	strcpy(buf, text);
	for (tok = strtok_r(buf, ",", &save); tok;
	     tok = strtok_r(NULL, ",", &save)) {
		for (i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
			if (strcmp(tok, names[i]) == 0)
				break;
		}
		if (i == sizeof(names) / sizeof(names[0])) {
			shm_err(err, errsz, "capabilities: 未知能力 %s", tok);
			return -1;
		}
		bits |= (i == 9) ? (1ull << 63) : (1ull << i);
	}
	*out = bits;
	return 0;
}

/* shm_args() - 复制 process.args */
static int shm_args(struct shm_spec *spec, const struct cn_node *arr,
		    char *err, size_t errsz)
{
	const struct cn_node *it;
	int n = 0;

	if (!arr || arr->type != CN_ARR || !arr->child) {
		shm_err(err, errsz, "process.args: 缺失或为空");
		return -1;
	}
	for (it = arr->child; it; it = it->sibling) {
		if (n >= SHM_ARG_MAX) {
			shm_err(err, errsz, "process.args: 超过 %d 项",
				SHM_ARG_MAX);
			return -1;
		}
		if (shm_copy(spec->argv[n], SHM_ARG_LEN, it) < 0) {
			shm_err(err, errsz, "process.args: 第 %d 项无效",
				n);
			return -1;
		}
		n++;
	}
	spec->argc = n;
	return 0;
}

/* shm_envs() - 复制 process.env */
static int shm_envs(struct shm_spec *spec, const struct cn_node *arr,
		    char *err, size_t errsz)
{
	const struct cn_node *it;
	int n = 0;

	spec->envc = 0;
	if (!arr)
		return 0;
	if (arr->type != CN_ARR) {
		shm_err(err, errsz, "process.env: 非数组");
		return -1;
	}
	for (it = arr->child; it; it = it->sibling) {
		if (n >= SHM_ENV_MAX) {
			shm_err(err, errsz, "process.env: 超过 %d 项",
				SHM_ENV_MAX);
			return -1;
		}
		if (shm_copy(spec->env[n], SHM_ENV_LEN, it) < 0) {
			shm_err(err, errsz, "process.env: 第 %d 项无效",
				n);
			return -1;
		}
		n++;
	}
	spec->envc = n;
	return 0;
}

/* shm_tonum() - 注解取值转正整数，接受 JSON 数字或十进制字符串 */
static int shm_tonum(const struct cn_node *node, long *out)
{
	char buf[SHM_TEXT_MAX];
	char *end;
	long val;

	if (cn_as_long(node, out) == 0)
		return 0;
	if (shm_copy(buf, sizeof(buf), node) < 0)
		return -1;
	errno = 0;
	val = strtol(buf, &end, 10);
	if (errno || end == buf || *end != '\0' || val <= 0)
		return -1;
	*out = val;
	return 0;
}

/* shm_budget() - 预算注解与缺省值 */
static int shm_budget(const struct cn_node *ann, struct shm_spec *spec,
		      char *err, size_t errsz)
{
	const struct cn_node *n;
	long val;

	spec->budget.total = SHM_DEF_BUDGET;
	spec->budget.per_req = SHM_DEF_BUDGET;
	spec->budget.refill = 0;
	strcpy(spec->budget.policy, "throttle");

	n = shm_memb(ann, "airymaxos.agent.token-budget");
	if (!n)
		return 0;
	if (shm_tonum(n, &val) < 0) {
		shm_err(err, errsz, "token-budget: 非正整数");
		return -1;
	}
	spec->budget.total = val;
	spec->budget.per_req = val;
	return 0;
}

/* shm_annload() - 逐项读取注解 */
static int shm_annload(const struct cn_node *ann, struct shm_spec *spec,
		       char *err, size_t errsz)
{
	const struct cn_node *n;
	char text[SHM_PATH_MAX];

	spec->mem.layers = 0;
	spec->mem.capacity[0] = '\0';
	spec->sched = SHM_SCHED_AGENT;
	strcpy(spec->cycle, "CoreLoopThree");
	spec->caps = 0;

	if (shm_budget(ann, spec, err, errsz) < 0)
		return -1;

	n = shm_memb(ann, "airymaxos.agent.memory-rovol");
	if (n) {
		if (shm_copy(text, sizeof(text), n) < 0) {
			shm_err(err, errsz, "memory-rovol: 取值无效");
			return -1;
		}
		if (shm_layers(text, &spec->mem.layers, err, errsz) < 0)
			return -1;
	}

	n = shm_memb(ann, "airymaxos.agent.scheduler");
	if (n) {
		if (shm_copy(text, sizeof(text), n) < 0) {
			shm_err(err, errsz, "scheduler: 取值无效");
			return -1;
		}
		if (strcmp(text, "stc_agent") == 0)
			spec->sched = SHM_SCHED_AGENT;
		else if (strcmp(text, "eevdf") == 0)
			spec->sched = SHM_SCHED_EEVDF;
		else {
			shm_err(err, errsz, "scheduler: 未知调度类 %s",
				text);
			return -1;
		}
	}

	n = shm_memb(ann, "airymaxos.agent.cognition-cycle");
	if (n) {
		if (shm_copy(spec->cycle, sizeof(spec->cycle), n) < 0 ||
		    strcmp(spec->cycle, "CoreLoopThree") != 0) {
			shm_err(err, errsz, "cognition-cycle: 仅支持 "
				"CoreLoopThree");
			return -1;
		}
	}

	n = shm_memb(ann, "airymaxos.agent.capabilities");
	if (n) {
		if (shm_copy(text, sizeof(text), n) < 0) {
			shm_err(err, errsz, "capabilities: 取值无效");
			return -1;
		}
		if (shm_caps(text, &spec->caps, err, errsz) < 0)
			return -1;
	} else {
		spec->caps = 1ull;
	}
	return 0;
}

/* shm_root() - 解析 root.path，相对路径按 bundle 展开 */
static int shm_root(const struct cn_node *root, struct shm_spec *spec,
		    char *err, size_t errsz)
{
	const struct cn_node *n;
	char rel[SHM_PATH_MAX];

	n = shm_memb(shm_memb(root, "root"), "path");
	if (!n)
		return 0;
	if (shm_copy(rel, sizeof(rel), n) < 0) {
		shm_err(err, errsz, "root.path: 取值无效");
		return -1;
	}
	if (rel[0] == '/') {
		strcpy(spec->root, rel);
		return 0;
	}
	if (shm_join(spec->root, sizeof(spec->root), spec->bundle,
		     rel) < 0) {
		shm_err(err, errsz, "root.path: 路径过长");
		return -1;
	}
	return 0;
}

int shm_load(const char *bundle, struct shm_spec *out, char *err,
	     size_t errsz)
{
	struct cn_doc *doc = NULL;
	const struct cn_node *root, *proc, *ann;
	char path[SHM_PATH_MAX];
	char *text = NULL;
	size_t len;
	int ret = -1;

	if (!bundle || !out) {
		shm_err(err, errsz, "bundle: 空参数");
		return -1;
	}
	memset(out, 0, sizeof(*out));
	if (strlen(bundle) >= sizeof(out->bundle) ||
	    shm_join(path, sizeof(path), bundle, "config.json") < 0) {
		shm_err(err, errsz, "bundle: 路径过长");
		return -1;
	}
	strcpy(out->bundle, bundle);
	strcpy(out->root, bundle);
	if (shm_read(path, &text, &len) < 0) {
		shm_err(err, errsz, "config.json: 不可读");
		return -1;
	}
	if (cn_parse(text, len, &doc) < 0) {
		shm_err(err, errsz, "config.json: 语法错误");
		goto out;
	}
	free(text);
	text = NULL;

	root = cn_root(doc);
	proc = shm_memb(root, "process");
	if (!proc) {
		shm_err(err, errsz, "process: 缺失");
		ret = -2;
		goto out;
	}
	if (shm_args(out, shm_memb(proc, "args"), err, errsz) < 0) {
		ret = -2;
		goto out;
	}
	if (shm_envs(out, shm_memb(proc, "env"), err, errsz) < 0) {
		ret = -2;
		goto out;
	}

	ann = shm_memb(root, "annotations");
	if (shm_annload(ann, out, err, errsz) < 0) {
		ret = -2;
		goto out;
	}

	if (shm_root(root, out, err, errsz) < 0) {
		ret = -2;
		goto out;
	}
	ret = 0;
out:
	free(text);
	cn_free(doc);
	return ret;
}

/* shm_layerstr() - 位图转层名列表 */
static void shm_layerstr(unsigned layers, char *buf, size_t cap)
{
	static const char *const names[] = { "L1", "L2", "L3", "L4" };
	size_t used = 0;
	int i;

	buf[0] = '\0';
	if (layers == 0) {
		snprintf(buf, cap, "none");
		return;
	}
	for (i = 0; i < 4; i++) {
		if (!(layers & (1u << i)))
			continue;
		used += (size_t)snprintf(buf + used, cap - used, "%s%s",
					 used ? "," : "", names[i]);
		if (used >= cap)
			break;
	}
}

void shm_plan(const struct shm_spec *spec, FILE *out)
{
	char layers[SHM_TEXT_MAX];

	shm_layerstr(spec->mem.layers, layers, sizeof(layers));
	fprintf(out, "shim admit id=%s args=%d\n", spec->id, spec->argc);
	fprintf(out, "shim budget total=%ld per_req=%ld refill=%ld "
		"policy=%s\n", spec->budget.total, spec->budget.per_req,
		spec->budget.refill, spec->budget.policy);
	fprintf(out, "shim memory layers=%s capacity=%s\n", layers,
		spec->mem.capacity[0] ? spec->mem.capacity : "default");
	fprintf(out, "shim sched class=%s cycle=%s\n",
		spec->sched == SHM_SCHED_EEVDF ? "eevdf" : "stc_agent",
		spec->cycle);
	fprintf(out, "shim security caps=0x%llx\n", spec->caps);
	fprintf(out, "shim exec argv0=%s env=%d\n", spec->argv[0],
		spec->envc);
}
