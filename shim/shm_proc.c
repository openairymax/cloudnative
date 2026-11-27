/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2025-2026 SPHARX Ltd. All Rights Reserved.
 *
 * shm_proc.c — 工作负载的启动与监管
 *
 * 职责：
 *   在 bundle 根目录下启动 process.args，转发终止信号，等待退出并
 *   折算退出码。启动后按调度类尝试内核桥；内核对 stc_agent 后端未
 *   内置时如实输出 unsupported，而不是把回落当成成功。
 *
 * [IND] 完全独立层（agentrt-linux 专属实现）。
 */

#define _POSIX_C_SOURCE 200809L

#include "shm_api.h"

#include <errno.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static volatile pid_t shm_child = -1;

/* shm_forward() - 把终止信号交给工作负载 */
static void shm_forward(int sig)
{
	if (shm_child > 0)
		kill(shm_child, sig);
}

/* shm_signals() - 安装转发处理器 */
static int shm_signals(void)
{
	static const int sigs[] = { SIGINT, SIGTERM, SIGQUIT, SIGHUP };
	struct sigaction sa;
	size_t i;

	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = shm_forward;
	sigemptyset(&sa.sa_mask);
	for (i = 0; i < sizeof(sigs) / sizeof(sigs[0]); i++) {
		if (sigaction(sigs[i], &sa, NULL) < 0)
			return -1;
	}
	return 0;
}

/* shm_exec() - 子进程侧：切目录并执行 */
static void shm_exec(const struct shm_spec *spec)
{
	char *argv[SHM_ARG_MAX + 1];
	char *envp[SHM_ENV_MAX + 1];
	int i;

	for (i = 0; i < spec->argc; i++)
		argv[i] = (char *)spec->argv[i];
	argv[spec->argc] = NULL;
	for (i = 0; i < spec->envc; i++)
		envp[i] = (char *)spec->env[i];
	envp[spec->envc] = NULL;

	if (chdir(spec->root) < 0) {
		dprintf(STDERR_FILENO, "shim: chdir %s: %s\n",
			spec->root, strerror(errno));
		_exit(127);
	}
	if (spec->envc > 0)
		execve(argv[0], argv, envp);
	else
		execv(argv[0], argv);
	dprintf(STDERR_FILENO, "shim: exec %s: %s\n", argv[0],
		strerror(errno));
	_exit(127);
}

/* shm_wait() - 等待子进程，受信号打断时继续 */
static int shm_wait(pid_t pid, int *status)
{
	while (waitpid(pid, status, 0) < 0) {
		if (errno != EINTR)
			return -1;
	}
	return 0;
}

/* shm_sched() - 按调度类尝试内核桥并输出结果 */
static void shm_sched(const struct shm_spec *spec, const char *cgroup,
		      FILE *out)
{
	enum shm_kern kern;

	if (!cgroup)
		return;
	if (spec->sched == SHM_SCHED_EEVDF) {
		fprintf(out, "shim sched class=eevdf result=native\n");
		return;
	}
	kern = shm_sched_apply(cgroup, "stc_agent");
	fprintf(out, "shim sched class=stc_agent result=%s\n",
		shm_kern_name(kern));
}

int shm_run(const struct shm_spec *spec, const char *cgroup, FILE *out,
	    int *code)
{
	pid_t pid;
	int status;

	if (!spec || !spec->argc || !code)
		return -1;
	if (shm_signals() < 0)
		return -1;
	fprintf(out, "shim phase=Creating\n");
	pid = fork();
	if (pid < 0)
		return -1;
	if (pid == 0)
		shm_exec(spec);
	shm_child = pid;
	fprintf(out, "shim phase=Running pid=%d\n", (int)pid);
	shm_sched(spec, cgroup, out);
	if (shm_wait(pid, &status) < 0) {
		shm_child = -1;
		return -1;
	}
	shm_child = -1;
	*code = shm_decode(status);
	fprintf(out, "shim exit code=%d\n", *code);
	fprintf(out, "shim phase=Dead\n");
	return 0;
}
