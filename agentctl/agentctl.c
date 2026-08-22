// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2025-2026 SPHARX Ltd. All Rights Reserved.
 *
 * agentctl.c — AirymaxOS 云原生控制面 CLI 工具
 *
 * 职责：
 *   提供 agentctl 命令行工具，用于查询 AirymaxOS 运行状态、
 *   验证 A-IPC 消息头契约、显示版本信息。0.1.1 阶段为最小可编译
 *   CLI 骨架，后续版本将扩展为完整的 Operator 控制面客户端。
 *
 * 选项：
 *   -h, --help      显示帮助
 *   -v, --version   显示版本
 *   -s, --status    读取 /proc/airy/status 并打印
 *
 * 决策 F1：真实可编译实现（非桩，非空 main）。
 *   默认无参数时打印版本横幅与 A-IPC magic 校验结果。
 *
 * [IND] 完全独立层（agent-linux 专属实现）。
 *
 * 相关文档：
 *   docs/AirymaxOS/10-architecture/07-directory-structure.md §4.6
 *   kernel/include/uapi/linux/airymax/ipc.h
 */

#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* [SC] 共享契约头文件（通过 -I../kernel/include/uapi/linux 引用） */
#include <airymax/ipc.h>

/* ─── 常量 ──────────────────────────────────────────────────────────── */

#define AGENTCTL_NAME		"agentctl"
#define AGENTCTL_VERSION	"0.1.1"
#define AGENTCTL_BANNER		\
	"agentctl v" AGENTCTL_VERSION " - Airymax cloud-native control plane"

#define AGENTCTL_STATUS_PATH	"/proc/airy/status"
#define AGENTCTL_STATUS_BUF	4096

/* ─── 选项定义 ──────────────────────────────────────────────────────── */

enum agentctl_opt {
	OPT_HELP	= 'h',
	OPT_VERSION	= 'v',
	OPT_STATUS	= 's',
};

static struct option agentctl_long_opts[] = {
	{ "help",	no_argument,	NULL,	OPT_HELP	},
	{ "version",	no_argument,	NULL,	OPT_VERSION	},
	{ "status",	no_argument,	NULL,	OPT_STATUS	},
	{ NULL,		0,		NULL,	0		},
};

static const char *agentctl_optstring = "hvs";

/* ─── 辅助函数 ──────────────────────────────────────────────────────── */

static void agentctl_usage(FILE *out, const char *prog)
{
	fprintf(out,
		"用法: %s [选项]\n"
		"\n"
		"AirymaxOS 云原生控制面 CLI 工具。\n"
		"\n"
		"选项:\n"
		"  -h, --help      显示帮助\n"
		"  -v, --version   显示版本\n"
		"  -s, --status    读取 /proc/airy/status 并打印\n"
		"\n"
		"无参数时打印版本横幅与 A-IPC magic 校验结果。\n"
		"\n"
		"示例:\n"
		"  %s                  # 显示横幅\n"
		"  %s -v               # 显示版本\n"
		"  %s -s               # 显示状态\n",
		prog, prog, prog, prog);
}

/*
 * agentctl_check_ipc_magic - 验证 [SC] ipc.h 中的 magic 与编译时一致
 *
 * 这一步确保 cloudnative CLI 与 [SC] 头文件的契约一致：
 * AIRY_IPC_MAGIC 必须为 0x41524531（'ARE1'）。
 *
 * 返回值：0 一致，-1 不一致（不可能发生，因为 [SC] 是 SSoT）
 */
static int agentctl_check_ipc_magic(void)
{
	if (AIRY_IPC_MAGIC != 0x41524531u) {
		fprintf(stderr,
			AGENTCTL_NAME ": 错误 — AIRY_IPC_MAGIC "
			"(0x%08x) 与期望值 (0x41524531) 不一致\n",
			AIRY_IPC_MAGIC);
		return -1;
	}
	printf(AGENTCTL_NAME ": A-IPC magic=0x%08x ('ARE1') "
	       "hdr_size=%u 字节 — 契约一致\n",
	       AIRY_IPC_MAGIC, AIRY_IPC_HDR_SIZE);
	return 0;
}

/*
 * agentctl_show_status - 读取 /proc/airy/status 并打印
 *
 * 返回值：0 成功，-1 失败（文件不存在或读取错误）
 */
static int agentctl_show_status(void)
{
	int fd;
	char buf[AGENTCTL_STATUS_BUF];
	ssize_t n;

	fd = open(AGENTCTL_STATUS_PATH, O_RDONLY | O_CLOEXEC);
	if (fd < 0) {
		fprintf(stderr,
			AGENTCTL_NAME ": 无法打开 %s: %s\n"
			AGENTCTL_NAME ": 提示 — AirymaxOS 内核模块未加载时"
			"该文件不存在\n",
			AGENTCTL_STATUS_PATH, strerror(errno));
		return -1;
	}

	n = read(fd, buf, sizeof(buf) - 1);
	close(fd);

	if (n < 0) {
		fprintf(stderr,
			AGENTCTL_NAME ": 读取 %s 失败: %s\n",
			AGENTCTL_STATUS_PATH, strerror(errno));
		return -1;
	}

	buf[n] = '\0';
	printf("=== %s ===\n%s", AGENTCTL_STATUS_PATH, buf);
	return 0;
}

/* ─── main 入口 ─────────────────────────────────────────────────────── */

int main(int argc, char **argv)
{
	int opt;
	int long_idx = 0;
	const char *prog = argv[0] ? argv[0] : AGENTCTL_NAME;

	/* 解析命令行选项 */
	while ((opt = getopt_long(argc, argv, agentctl_optstring,
				  agentctl_long_opts, &long_idx)) != -1) {
		switch (opt) {
		case OPT_HELP:
			agentctl_usage(stdout, prog);
			return EXIT_SUCCESS;

		case OPT_VERSION:
			printf(AGENTCTL_BANNER "\n");
			return EXIT_SUCCESS;

		case OPT_STATUS:
			return agentctl_show_status() == 0
			       ? EXIT_SUCCESS : EXIT_FAILURE;

		default:
			agentctl_usage(stderr, prog);
			return EXIT_FAILURE;
		}
	}

	/* 默认行为：打印横幅 + 验证 [SC] magic */
	printf(AGENTCTL_BANNER "\n");
	if (agentctl_check_ipc_magic() < 0)
		return EXIT_FAILURE;

	printf(AGENTCTL_NAME ": 使用 -h 查看帮助\n");
	return EXIT_SUCCESS;
}
