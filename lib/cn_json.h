/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2025-2026 SPHARX Ltd. All Rights Reserved.
 *
 * cn_json.h — 云原生模块的最小 JSON 文档读取件
 *
 * 职责：
 *   kubectl 以 JSON 输出对象，CRD 校验器、shim 与 CNI 插件都只读取
 *   这些对象的部分字段。本件把 JSON 文本解析成只读节点树，并提供按
 *   点分路径取字段的接口；不负责序列化，也不做模式校验。
 *
 * 设计：
 *   节点与字符串共用一次 malloc 的文档对象，解析失败即整体作废。
 *   树结构为「首子节点 + 兄弟链」，路径段只按对象键匹配，数组按下标
 *   访问由调用方自行遍历，避免把路径语法扩成第二套事实。
 *
 * [IND] 完全独立层（agentrt-linux 专属实现）。
 */

#ifndef CN_JSON_H
#define CN_JSON_H

#include <stddef.h>

struct cn_doc;

/* 节点类型；取值与 JSON 语法一一对应 */
enum cn_type {
	CN_NULL,
	CN_BOOL,
	CN_NUM,
	CN_STR,
	CN_OBJ,
	CN_ARR,
};

struct cn_node {
	enum cn_type type;
	const char *key;	/* 对象成员键；根节点为 NULL */
	const char *str;	/* CN_STR 的起址，落于文档缓冲内 */
	size_t len;		/* CN_STR 的字节数 */
	double num;
	int boolean;
	struct cn_node *child;	/* CN_OBJ / CN_ARR 的首个子节点 */
	struct cn_node *sibling;	/* 同级下一节点 */
};

/*
 * cn_parse() - 解析一段 JSON 文本
 * @text: 文本起址，不需要 NUL 结尾
 * @len:  文本字节数
 * @out:  成功时回填文档对象，调用方负责 cn_free()
 *
 * 返回值：0 成功；-1 语法错误；-2 资源不足（节点或字符串缓冲用尽）。
 */
int cn_parse(const char *text, size_t len, struct cn_doc **out);

/* cn_free() - 释放文档对象；@doc 为 NULL 时为空操作 */
void cn_free(struct cn_doc *doc);

/* cn_root() - 取文档根节点；@doc 为 NULL 时返回 NULL */
const struct cn_node *cn_root(const struct cn_doc *doc);

/*
 * cn_get() - 按点分路径取对象成员
 * @node: 起始节点，通常为 cn_root()
 * @path: 形如 "spec.tokenBudget.total"；只按对象键逐段匹配
 *
 * 返回值：命中节点；任一段缺失或中途遇到非对象返回 NULL。
 */
const struct cn_node *cn_get(const struct cn_node *node, const char *path);

/* cn_type_name() - 节点类型的英文名，用于错误信息 */
const char *cn_type_name(enum cn_type type);

/*
 * cn_as_long() - 取整数值
 * @node: 待取节点
 * @out:  成功时回填
 *
 * 只接受 CN_NUM 且数值为整数并落在 long 范围内，避免把 1.5 当成 1。
 * 返回值：0 成功；-1 类型或取值不符。
 */
int cn_as_long(const struct cn_node *node, long *out);

#endif /* CN_JSON_H */
