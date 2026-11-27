/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2025-2026 SPHARX Ltd. All Rights Reserved.
 *
 * crd_state.c — Agent 八态机的转移表
 *
 * 职责：
 *   控制器只把观测到的事件交给本件，由转移表给出下一状态；
 *   不允许的转移返回 CRD_STATE_BAD，不做兜底放行。
 *
 * 说明：
 *   stop 事件对除 Dead/Terminating 之外的所有状态统一进入
 *   Terminating，因此不逐条列入转移表；die 事件只接受
 *   Running/Blocked/Suspended/Terminating 四个来源。
 *
 * [IND] 完全独立层（agentrt-linux 专属实现）。
 */

#include "crd_api.h"

#include <string.h>

struct crd_edge {
	enum crd_state from;
	enum crd_event ev;
	enum crd_state to;
};

static const struct crd_edge crd_edges[] = {
	{ CRD_CREATING, CRD_EV_REGISTER, CRD_REGISTERED },
	{ CRD_REGISTERED, CRD_EV_READY, CRD_READY },
	{ CRD_READY, CRD_EV_RUN, CRD_RUNNING },
	{ CRD_READY, CRD_EV_SUSPEND, CRD_SUSPENDED },
	{ CRD_RUNNING, CRD_EV_BLOCK, CRD_BLOCKED },
	{ CRD_RUNNING, CRD_EV_SUSPEND, CRD_SUSPENDED },
	{ CRD_BLOCKED, CRD_EV_RESUME, CRD_RUNNING },
	{ CRD_SUSPENDED, CRD_EV_RESUME, CRD_READY },
	{ CRD_SUSPENDED, CRD_EV_RUN, CRD_RUNNING },
};

static const char *const crd_names[] = {
	"Creating", "Registered", "Ready", "Running",
	"Blocked", "Suspended", "Terminating", "Dead",
};

static const char *const crd_events[] = {
	"register", "ready", "run", "block",
	"suspend", "resume", "stop", "die",
};

enum crd_state crd_next(enum crd_state state, enum crd_event event)
{
	size_t i;

	if (state >= CRD_STATE_BAD || event >= CRD_EV_BAD)
		return CRD_STATE_BAD;
	if (event == CRD_EV_STOP)
		return state == CRD_DEAD || state == CRD_TERMINATING ?
			CRD_STATE_BAD : CRD_TERMINATING;
	if (event == CRD_EV_DIE)
		return state == CRD_RUNNING || state == CRD_BLOCKED ||
		       state == CRD_SUSPENDED || state == CRD_TERMINATING ?
			CRD_DEAD : CRD_STATE_BAD;
	for (i = 0; i < sizeof(crd_edges) / sizeof(crd_edges[0]); i++)
		if (crd_edges[i].from == state &&
		    crd_edges[i].ev == event)
			return crd_edges[i].to;
	return CRD_STATE_BAD;
}

const char *crd_state_name(enum crd_state state)
{
	if (state >= CRD_STATE_BAD)
		return "bad";
	return crd_names[state];
}

int crd_state_parse(const char *name, enum crd_state *out)
{
	size_t i;

	if (name == NULL || out == NULL)
		return -1;
	for (i = 0; i < sizeof(crd_names) / sizeof(crd_names[0]); i++) {
		if (strcmp(name, crd_names[i]) == 0) {
			*out = (enum crd_state)i;
			return 0;
		}
	}
	return -1;
}

int crd_event_parse(const char *name, enum crd_event *out)
{
	size_t i;

	if (name == NULL || out == NULL)
		return -1;
	for (i = 0; i < sizeof(crd_events) / sizeof(crd_events[0]); i++) {
		if (strcmp(name, crd_events[i]) == 0) {
			*out = (enum crd_event)i;
			return 0;
		}
	}
	return -1;
}
