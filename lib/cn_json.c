/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2025-2026 SPHARX Ltd. All Rights Reserved.
 *
 * cn_json.c — 云原生模块的最小 JSON 文档读取件实现
 *
 * 职责：
 *   递归下降解析 JSON，节点与字符串落进同一个文档对象。解析不分配
 *   小块内存，失败时整体释放，调用方只需在成功路径上 cn_free()。
 *
 * 约束：
 *   嵌套深度上限 32 层，节点数上限 1024 个，字符串缓冲 32 KiB。
 *   这些上限覆盖 CRD 清单、shim 的 OCI bundle 与 CNI 的策略对象；
 *   超出即判语法错误，不做部分接受。
 *
 * [IND] 完全独立层（agentrt-linux 专属实现）。
 */

#include "cn_json.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#define CN_NODES	1024
#define CN_ARENA	(32 * 1024)
#define CN_DEPTH	32
#define CN_NUM_MAX	64

struct cn_doc {
	struct cn_node nodes[CN_NODES];
	size_t used;
	char arena[CN_ARENA];
	size_t alen;
	int full;		/* 节点数用尽 */
};

struct cn_cur {
	const char *p;
	const char *end;
	struct cn_doc *doc;
	int depth;
};

/* cn_doc_new() - 分配零填充的文档对象 */
static struct cn_doc *cn_doc_new(void)
{
	return calloc(1, sizeof(struct cn_doc));
}

/* cn_new_node() - 从文档节点池取一个节点 */
static struct cn_node *cn_new_node(struct cn_cur *c)
{
	struct cn_doc *d = c->doc;

	if (d->used >= CN_NODES) {
		d->full = 1;
		return NULL;
	}
	return &d->nodes[d->used++];
}

/* cn_put() - 向文档字符串缓冲追加一个字节 */
static int cn_put(struct cn_doc *d, unsigned char ch)
{
	if (d->alen >= CN_ARENA)
		return -1;
	d->arena[d->alen++] = (char)ch;
	return 0;
}

/* cn_is_ws() - JSON 允许的四种空白 */
static int cn_is_ws(char ch)
{
	return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r';
}

/* cn_skip() - 跳过空白；不改变语义，故不判错 */
static void cn_skip(struct cn_cur *c)
{
	while (c->p < c->end && cn_is_ws(*c->p))
		c->p++;
}

/* cn_hex4() - 读取四位十六进制码元 */
static int cn_hex4(const char *p, unsigned *out)
{
	unsigned val = 0;
	int i;

	for (i = 0; i < 4; i++) {
		char ch = p[i];

		val <<= 4;
		if (ch >= '0' && ch <= '9')
			val |= (unsigned)(ch - '0');
		else if (ch >= 'a' && ch <= 'f')
			val |= (unsigned)(ch - 'a' + 10);
		else if (ch >= 'A' && ch <= 'F')
			val |= (unsigned)(ch - 'A' + 10);
		else
			return -1;
	}
	*out = val;
	return 0;
}

/* cn_utf8() - 把一个码点写成 UTF-8 字节序列 */
static int cn_utf8(struct cn_doc *d, unsigned cp)
{
	if (cp < 0x80)
		return cn_put(d, (unsigned char)cp);
	if (cp < 0x800) {
		if (cn_put(d, (unsigned char)(0xc0 | (cp >> 6))) < 0)
			return -1;
		return cn_put(d, (unsigned char)(0x80 | (cp & 0x3f)));
	}
	if (cp < 0x10000) {
		if (cn_put(d, (unsigned char)(0xe0 | (cp >> 12))) < 0)
			return -1;
		if (cn_put(d, (unsigned char)(0x80 | ((cp >> 6) & 0x3f))) < 0)
			return -1;
		return cn_put(d, (unsigned char)(0x80 | (cp & 0x3f)));
	}
	if (cp > 0x10ffff)
		return -1;
	if (cn_put(d, (unsigned char)(0xf0 | (cp >> 18))) < 0)
		return -1;
	if (cn_put(d, (unsigned char)(0x80 | ((cp >> 12) & 0x3f))) < 0)
		return -1;
	if (cn_put(d, (unsigned char)(0x80 | ((cp >> 6) & 0x3f))) < 0)
		return -1;
	return cn_put(d, (unsigned char)(0x80 | (cp & 0x3f)));
}

/* cn_esc_pair() - 组合代理对；c->p 已越过高位的四位码元 */
static int cn_esc_pair(struct cn_cur *c, unsigned hi);

/* cn_esc() - 展开转义序列；c->p 指向反斜杠之后 */
static int cn_esc(struct cn_cur *c)
{
	static const char plain[] = "\"\\/bfnrt";
	static const char value[] = "\"\\/\b\f\n\r\t";
	const char *hit;

	if (c->p >= c->end)
		return -1;
	if (*c->p == 'u') {
		unsigned cp;

		if (c->end - c->p < 5)
			return -1;
		if (cn_hex4(c->p + 1, &cp) < 0)
			return -1;
		c->p += 5;
		if (cp >= 0xd800 && cp <= 0xdbff)
			return cn_esc_pair(c, cp);
		if (cp >= 0xdc00 && cp <= 0xdfff)
			return -1;
		return cn_utf8(c->doc, cp);
	}
	hit = strchr(plain, *c->p);
	if (hit == NULL || *c->p == '\0')
		return -1;
	c->p++;
	return cn_put(c->doc, (unsigned char)value[hit - plain]);
}

/* cn_esc_pair() - 组合代理对；c->p 已越过高位的四位码元 */
static int cn_esc_pair(struct cn_cur *c, unsigned hi)
{
	unsigned lo;

	if (c->end - c->p < 6 || c->p[0] != '\\' || c->p[1] != 'u')
		return -1;
	if (cn_hex4(c->p + 2, &lo) < 0)
		return -1;
	if (lo < 0xdc00 || lo > 0xdfff)
		return -1;
	c->p += 6;
	return cn_utf8(c->doc, 0x10000 + ((hi - 0xd800) << 10) + (lo - 0xdc00));
}

/* cn_parse_str() - 解析字符串，回填起址与字节数 */
static int cn_parse_str(struct cn_cur *c, const char **start, size_t *len)
{
	struct cn_doc *d = c->doc;
	size_t at;

	if (c->p >= c->end || *c->p != '"')
		return -1;
	c->p++;
	at = d->alen;
	while (c->p < c->end) {
		unsigned char ch = (unsigned char)*c->p;

		if (ch == '"') {
			c->p++;
			*start = d->arena + at;
			*len = d->alen - at;
			return 0;
		}
		if (ch < 0x20)
			return -1;
		if (ch == '\\') {
			c->p++;
			if (cn_esc(c) < 0)
				return -1;
			continue;
		}
		if (cn_put(d, ch) < 0)
			return -1;
		c->p++;
	}
	return -1;
}

/* cn_num_char() - 数字令牌允许的字符 */
static int cn_num_char(char ch)
{
	return (ch >= '0' && ch <= '9') || ch == '-' || ch == '+' ||
	       ch == '.' || ch == 'e' || ch == 'E';
}

/* cn_parse_num() - 解析数字；令牌拷进定长缓冲后交给 strtod */
static int cn_parse_num(struct cn_cur *c, double *out)
{
	char buf[CN_NUM_MAX];
	size_t n = 0;
	char *end;

	while (c->p < c->end && cn_num_char(*c->p)) {
		if (n + 1 >= sizeof(buf))
			return -1;
		buf[n++] = *c->p++;
	}
	if (n == 0)
		return -1;
	buf[n] = '\0';
	*out = strtod(buf, &end);
	return *end == '\0' ? 0 : -1;
}

/* cn_lit() - 匹配 true / false / null 字面量 */
static int cn_lit(struct cn_cur *c, const char *word)
{
	size_t n = strlen(word);

	if ((size_t)(c->end - c->p) < n || memcmp(c->p, word, n) != 0)
		return -1;
	c->p += n;
	return 0;
}

static struct cn_node *cn_parse_val(struct cn_cur *c);

/* cn_parse_obj() - 解析对象体；c->p 指向 '{' */
static int cn_parse_obj(struct cn_cur *c, struct cn_node *n)
{
	struct cn_node *tail = NULL;

	n->type = CN_OBJ;
	c->p++;
	cn_skip(c);
	if (c->p < c->end && *c->p == '}') {
		c->p++;
		return 0;
	}
	for (;;) {
		const char *key;
		size_t klen;
		struct cn_node *val;

		if (cn_parse_str(c, &key, &klen) < 0)
			return -1;
		if (cn_put(c->doc, 0) < 0)
			return -1;
		cn_skip(c);
		if (c->p >= c->end || *c->p != ':')
			return -1;
		c->p++;
		val = cn_parse_val(c);
		if (val == NULL)
			return -1;
		val->key = key;
		if (tail == NULL)
			n->child = val;
		else
			tail->sibling = val;
		tail = val;
		cn_skip(c);
		if (c->p < c->end && *c->p == ',') {
			c->p++;
			cn_skip(c);
			continue;
		}
		if (c->p < c->end && *c->p == '}') {
			c->p++;
			return 0;
		}
		return -1;
	}
}

/* cn_parse_arr() - 解析数组体；c->p 指向 '[' */
static int cn_parse_arr(struct cn_cur *c, struct cn_node *n)
{
	struct cn_node *tail = NULL;

	n->type = CN_ARR;
	c->p++;
	cn_skip(c);
	if (c->p < c->end && *c->p == ']') {
		c->p++;
		return 0;
	}
	for (;;) {
		struct cn_node *val = cn_parse_val(c);

		if (val == NULL)
			return -1;
		if (tail == NULL)
			n->child = val;
		else
			tail->sibling = val;
		tail = val;
		cn_skip(c);
		if (c->p < c->end && *c->p == ',') {
			c->p++;
			continue;
		}
		if (c->p < c->end && *c->p == ']') {
			c->p++;
			return 0;
		}
		return -1;
	}
}

/* cn_parse_val() - 按首字符分派到各类型解析 */
static struct cn_node *cn_parse_val(struct cn_cur *c)
{
	struct cn_node *n;

	if (c->depth >= CN_DEPTH)
		return NULL;
	cn_skip(c);
	if (c->p >= c->end)
		return NULL;
	n = cn_new_node(c);
	if (n == NULL)
		return NULL;
	c->depth++;
	switch (*c->p) {
	case '{':
		if (cn_parse_obj(c, n) == 0) {
			c->depth--;
			return n;
		}
		break;
	case '[':
		if (cn_parse_arr(c, n) == 0) {
			c->depth--;
			return n;
		}
		break;
	case '"':
		n->type = CN_STR;
		if (cn_parse_str(c, &n->str, &n->len) == 0) {
			c->depth--;
			return n;
		}
		break;
	case 't':
	case 'f':
		n->type = CN_BOOL;
		n->boolean = *c->p == 't';
		if (cn_lit(c, n->boolean ? "true" : "false") == 0) {
			c->depth--;
			return n;
		}
		break;
	case 'n':
		n->type = CN_NULL;
		if (cn_lit(c, "null") == 0) {
			c->depth--;
			return n;
		}
		break;
	default:
		n->type = CN_NUM;
		if (cn_parse_num(c, &n->num) == 0) {
			c->depth--;
			return n;
		}
		break;
	}
	c->depth--;
	return NULL;
}

int cn_parse(const char *text, size_t len, struct cn_doc **out)
{
	struct cn_cur cur;
	struct cn_doc *doc;

	if (text == NULL || out == NULL)
		return -1;
	doc = cn_doc_new();
	if (doc == NULL)
		return -2;
	cur.p = text;
	cur.end = text + len;
	cur.doc = doc;
	cur.depth = 0;
	if (cn_parse_val(&cur) == NULL) {
		int full = doc->full;

		cn_free(doc);
		return full ? -2 : -1;
	}
	cn_skip(&cur);
	if (cur.p != cur.end) {
		cn_free(doc);
		return -1;
	}
	*out = doc;
	return 0;
}

void cn_free(struct cn_doc *doc)
{
	free(doc);
}

const struct cn_node *cn_root(const struct cn_doc *doc)
{
	return doc == NULL ? NULL : &doc->nodes[0];
}

const struct cn_node *cn_get(const struct cn_node *node, const char *path)
{
	const struct cn_node *cur = node;
	const char *seg = path;

	while (cur != NULL && *seg != '\0') {
		const char *dot = strchr(seg, '.');
		size_t n = dot != NULL ? (size_t)(dot - seg) : strlen(seg);
		const struct cn_node *it =
			cur->type == CN_OBJ ? cur->child : NULL;

		cur = NULL;
		for (; it != NULL; it = it->sibling) {
			if (it->key != NULL && strlen(it->key) == n &&
			    memcmp(it->key, seg, n) == 0) {
				cur = it;
				break;
			}
		}
		seg = dot != NULL ? dot + 1 : seg + n;
	}
	return cur;
}

const char *cn_type_name(enum cn_type type)
{
	switch (type) {
	case CN_NULL:
		return "null";
	case CN_BOOL:
		return "bool";
	case CN_NUM:
		return "number";
	case CN_STR:
		return "string";
	case CN_OBJ:
		return "object";
	case CN_ARR:
		return "array";
	}
	return "unknown";
}

int cn_as_long(const struct cn_node *node, long *out)
{
	long val;

	if (node == NULL || node->type != CN_NUM || out == NULL)
		return -1;
	if (node->num < (double)LONG_MIN || node->num > (double)LONG_MAX)
		return -1;
	val = (long)node->num;
	if ((double)val != node->num)
		return -1;
	*out = val;
	return 0;
}
