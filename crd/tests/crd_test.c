/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2025-2026 SPHARX Ltd. All Rights Reserved.
 *
 * crd_test.c — CRD 校验、归一、规划与状态机的单元测试
 *
 * 覆盖：
 *   JSON 读取件的正常与异常路径；Agent 模式的必填、枚举、区间、
 *   跨字段与去重约束；缺省回填；动作序列的确定内容；八态机允许
 *   与不允许的转移；资源单位折算。
 *
 * [IND] 完全独立层（agentrt-linux 专属实现）。
 */

#include "crd_api.h"

#include <stdio.h>
#include <string.h>

static int fails;

#define CHECK(cond) do {						\
	if (!(cond)) {							\
		fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__,	\
			#cond);						\
		fails++;						\
	}								\
} while (0)

static const char *agent_json =
	"{"
	"\"apiVersion\":\"agent.airymaxos.dev/v1\","
	"\"kind\":\"Agent\","
	"\"metadata\":{\"name\":\"cognition-agent-01\","
	"\"namespace\":\"production\"},"
	"\"spec\":{"
	"\"image\":\"registry.airymaxos.dev/cognition:v1.0.1\","
	"\"imagePullPolicy\":\"IfNotPresent\","
	"\"tokenBudget\":{\"total\":1000000,\"perRequestLimit\":10000,"
	"\"refillRate\":100,\"policy\":\"throttle\"},"
	"\"memoryRovol\":{\"enabled\":true,"
	"\"layers\":[\"L1\",\"L2\",\"L3\",\"L4\"],"
	"\"capacity\":\"100Gi\",\"forgettingStrategy\":\"EBBINGHAUS\"},"
	"\"cognition\":{\"cycle\":\"CoreLoopThree\","
	"\"scheduler\":\"stc_agent\",\"thinkMode\":\"dual\"},"
	"\"security\":{\"capabilities\":[\"AIRY_CAP_MEMORY_READ\","
	"\"AIRY_CAP_MEMORY_WRITE\",\"AIRY_CAP_IPC_SEND\"],"
	"\"landlockPaths\":[\"/var/lib/agentrt/agent-01/data\","
	"\"/var/log/agentrt/agent-01\"],"
	"\"seccompProfile\":\"agentrt-default\"},"
	"\"resources\":{\"cpu\":\"4\",\"memory\":\"8Gi\",\"pids\":200},"
	"\"nodeSelector\":{\"airymaxos.dev/node-type\":\"agent-linux\"}"
	"}"
	"}";

/* agent_head() - 一个只含对象头的最小文档，供负例拼接 */
static const char *agent_head =
	"{\"apiVersion\":\"agent.airymaxos.dev/v1\",\"kind\":\"Agent\","
	"\"metadata\":{\"name\":\"probe\"},\"spec\":{";

static struct cn_doc *doc_load(const char *text)
{
	struct cn_doc *doc = NULL;

	if (cn_parse(text, strlen(text), &doc) < 0)
		return NULL;
	return doc;
}

static void test_parse(void)
{
	struct cn_doc *doc = doc_load("{\"a\":{\"b\":[1,2,\"x\"]},\"n\":null}");
	const struct cn_node *arr;
	long val;

	CHECK(doc != NULL);
	arr = cn_get(cn_root(doc), "a.b");
	CHECK(arr != NULL && arr->type == CN_ARR);
	CHECK(arr != NULL && cn_as_long(arr->child, &val) == 0 && val == 1);
	CHECK(arr != NULL && arr->child->sibling->type == CN_NUM);
	CHECK(arr != NULL && arr->child->sibling->sibling->type == CN_STR);
	CHECK(cn_get(cn_root(doc), "n") != NULL &&
	      cn_get(cn_root(doc), "n")->type == CN_NULL);
	CHECK(cn_get(cn_root(doc), "a.zz") == NULL);
	CHECK(cn_get(cn_root(doc), "a.b.c") == NULL);
	cn_free(doc);
}

static void test_escape(void)
{
	struct cn_doc *doc = doc_load("{\"s\":\"\\u4e2d\\u6587 \","
				      "\"e\":\"a\\tb\",\"d\":\"\\ud83d\\ude00\"}");
	const struct cn_node *s;
	const struct cn_node *e;
	const struct cn_node *d;

	CHECK(doc != NULL);
	s = cn_get(cn_root(doc), "s");
	CHECK(s != NULL && s->len == 7);
	CHECK(s != NULL && memcmp(s->str, "\xe4\xb8\xad\xe6\x96\x87 ", 7) == 0);
	e = cn_get(cn_root(doc), "e");
	CHECK(e != NULL && e->len == 3);
	d = cn_get(cn_root(doc), "d");
	CHECK(d != NULL && d->len == 4);
	cn_free(doc);
}

static void test_bad(void)
{
	struct cn_doc *doc = NULL;
	long val = 0;

	CHECK(cn_parse("{\"a\":1}x", 8, &doc) < 0);
	CHECK(cn_parse("{\"a\":1,}", 8, &doc) < 0);
	CHECK(cn_parse("[1,]", 4, &doc) < 0);
	CHECK(cn_parse("{\"a\":1.5}", 9, &doc) == 0);
	CHECK(cn_as_long(cn_get(cn_root(doc), "a"), &val) < 0);
	cn_free(doc);
	doc = NULL;
	CHECK(cn_parse("", 0, &doc) < 0);
	CHECK(cn_parse("{\"a\":\"x\\q\"}", 11, &doc) < 0);
}

static void test_depth(void)
{
	char buf[128];
	struct cn_doc *doc = NULL;
	int i;

	for (i = 0; i < 40; i++)
		buf[i] = '[';
	for (i = 0; i < 40; i++)
		buf[40 + i] = ']';
	CHECK(cn_parse(buf, 80, &doc) < 0);
}

static void test_fill(void)
{
	struct cn_doc *doc = doc_load(agent_json);
	struct crd_agent agent;
	char err[128];

	CHECK(doc != NULL);
	CHECK(crd_check(cn_root(doc), err, sizeof(err)) == 0);
	CHECK(crd_fill(cn_root(doc), &agent) == 0);
	CHECK(strcmp(agent.name, "cognition-agent-01") == 0);
	CHECK(strcmp(agent.ns, "production") == 0);
	CHECK(strcmp(agent.pull, "IfNotPresent") == 0);
	CHECK(agent.budget.total == 1000000);
	CHECK(agent.budget.per_req == 10000);
	CHECK(agent.budget.refill == 100);
	CHECK(strcmp(agent.budget.policy, "throttle") == 0);
	CHECK(agent.layers == (CRD_L1 | CRD_L2 | CRD_L3 | CRD_L4));
	CHECK(strcmp(agent.capacity, "100Gi") == 0);
	CHECK(strcmp(agent.forget, "EBBINGHAUS") == 0);
	CHECK(strcmp(agent.cycle, "CoreLoopThree") == 0);
	CHECK(strcmp(agent.sched, "stc_agent") == 0);
	CHECK(strcmp(agent.think, "dual") == 0);
	CHECK(strcmp(agent.seccomp, "agentrt-default") == 0);
	CHECK(agent.caps == 3 && agent.paths == 2);
	CHECK(agent.cpu_milli == 4000 && agent.mem_mib == 8192);
	CHECK(agent.pids == 200);
	cn_free(doc);
}

/* test_reject() - 拼接一句 spec 体，断言模式判负 */
static void test_reject(const char *body, const char *why)
{
	char buf[512];
	struct cn_doc *doc = NULL;
	char err[128] = "";

	snprintf(buf, sizeof(buf), "%s%s}}", agent_head, body);
	CHECK(cn_parse(buf, strlen(buf), &doc) == 0);
	if (doc == NULL)
		return;
	if (crd_check(cn_root(doc), err, sizeof(err)) == 0) {
		fprintf(stderr, "FAIL 应拒绝: %s\n", why);
		fails++;
	}
	cn_free(doc);
}

static void test_rules(void)
{
	test_reject("\"tokenBudget\":{\"total\":5}", "缺 image");
	test_reject("\"image\":\"a/b:1\",\"imagePullPolicy\":\"Sometimes\"",
		    "pull 枚举越界");
	test_reject("\"image\":\"a/b:1\",\"tokenBudget\":"
		    "{\"total\":5,\"perRequestLimit\":9}", "单次超总额");
	test_reject("\"image\":\"a/b:1\",\"tokenBudget\":{\"total\":5},"
		    "\"memoryRovol\":{\"layers\":[\"L1\",\"L1\"]}", "层重复");
	test_reject("\"image\":\"a/b:1\",\"tokenBudget\":{\"total\":5},"
		    "\"cognition\":{\"thinkMode\":\"t3\"}", "思考模式越界");
	test_reject("\"image\":\"a/b:1\",\"tokenBudget\":{\"total\":5},"
		    "\"resources\":{\"pids\":0}", "pids 下界");
	test_reject("\"image\":\"a b\",\"tokenBudget\":{\"total\":5}",
		    "镜像字符集");
	test_reject("\"image\":\"a/b:1\",\"tokenBudget\":{\"total\":0}",
		    "预算下界");
}

static void test_plan(void)
{
	struct cn_doc *doc = doc_load(agent_json);
	struct crd_agent agent;
	FILE *fp = tmpfile();
	char line[256];
	int summary = 0;
	int layers = 0;

	CHECK(doc != NULL && fp != NULL);
	CHECK(crd_fill(cn_root(doc), &agent) == 0);
	crd_plan(&agent, fp);
	rewind(fp);
	while (fgets(line, sizeof(line), fp) != NULL) {
		if (strstr(line, "plan summary actions=5") != NULL)
			summary = 1;
		if (strstr(line, "layers=L1,L2,L3,L4") != NULL)
			layers = 1;
	}
	CHECK(summary == 1 && layers == 1);
	fclose(fp);
	cn_free(doc);
}

static void test_units(void)
{
	struct cn_doc *doc = doc_load("{\"a\":\"500m\",\"b\":\"4\","
				      "\"c\":\"8Gi\",\"d\":\"512Mi\","
				      "\"e\":\"2G\",\"f\":\"bad\"}");
	int ok = 0;

	CHECK(doc != NULL);
	CHECK(crd_cpu_milli(cn_get(cn_root(doc), "a"), &ok) == 500);
	CHECK(ok == 1);
	CHECK(crd_cpu_milli(cn_get(cn_root(doc), "b"), &ok) == 4000);
	CHECK(crd_mem_mib(cn_get(cn_root(doc), "c"), &ok) == 8192);
	CHECK(crd_mem_mib(cn_get(cn_root(doc), "d"), &ok) == 512);
	CHECK(crd_mem_mib(cn_get(cn_root(doc), "e"), &ok) == 1908);
	CHECK(crd_mem_mib(cn_get(cn_root(doc), "f"), &ok) == 0);
	CHECK(ok == 0);
	CHECK(crd_cpu_milli(NULL, &ok) == 0 && ok == 0);
	cn_free(doc);
}

static void test_state(void)
{
	enum crd_state state;
	enum crd_event event;

	CHECK(crd_state_parse("Running", &state) == 0);
	CHECK(state == CRD_RUNNING);
	CHECK(crd_event_parse("suspend", &event) == 0);
	CHECK(event == CRD_EV_SUSPEND);
	CHECK(crd_next(CRD_CREATING, CRD_EV_REGISTER) == CRD_REGISTERED);
	CHECK(crd_next(CRD_REGISTERED, CRD_EV_READY) == CRD_READY);
	CHECK(crd_next(CRD_READY, CRD_EV_RUN) == CRD_RUNNING);
	CHECK(crd_next(CRD_RUNNING, CRD_EV_BLOCK) == CRD_BLOCKED);
	CHECK(crd_next(CRD_BLOCKED, CRD_EV_RESUME) == CRD_RUNNING);
	CHECK(crd_next(CRD_RUNNING, CRD_EV_SUSPEND) == CRD_SUSPENDED);
	CHECK(crd_next(CRD_SUSPENDED, CRD_EV_RESUME) == CRD_READY);
	CHECK(crd_next(CRD_RUNNING, CRD_EV_STOP) == CRD_TERMINATING);
	CHECK(crd_next(CRD_CREATING, CRD_EV_STOP) == CRD_TERMINATING);
	CHECK(crd_next(CRD_TERMINATING, CRD_EV_DIE) == CRD_DEAD);
	CHECK(crd_next(CRD_RUNNING, CRD_EV_DIE) == CRD_DEAD);
	CHECK(crd_next(CRD_DEAD, CRD_EV_STOP) == CRD_STATE_BAD);
	CHECK(crd_next(CRD_READY, CRD_EV_DIE) == CRD_STATE_BAD);
	CHECK(crd_next(CRD_CREATING, CRD_EV_RUN) == CRD_STATE_BAD);
	CHECK(strcmp(crd_state_name(CRD_STATE_BAD), "bad") == 0);
	CHECK(crd_state_parse("nope", &state) < 0);
	CHECK(crd_event_parse("nope", &event) < 0);
}

int main(void)
{
	test_parse();
	test_escape();
	test_bad();
	test_depth();
	test_fill();
	test_rules();
	test_plan();
	test_units();
	test_state();
	if (fails != 0) {
		fprintf(stderr, "crd_test: %d 项失败\n", fails);
		return 1;
	}
	printf("crd_test: ok\n");
	return 0;
}
