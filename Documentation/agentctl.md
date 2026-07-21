# agentctl — 云原生控制面 CLI

> **子仓**：`cloudnative/`
> **组件**：`agentctl/`
> **可编译入口**：`agentctl/agentctl.c` → `agentctl`
> **[SC] 依赖**：`kernel/include/uapi/linux/airymax/ipc.h`
> **设计文档**：`docs/AirymaxOS/10-architecture/07-directory-structure.md` §4.6

Copyright (c) 2025-2026 SPHARX Ltd. All Rights Reserved.

License: GPL-2.0-only

---

## 1. 设计目标

agentctl 是 AirymaxOS 云原生控制面的命令行入口，提供：

- 状态查询（读取 `/proc/airy/status`）
- A-IPC 契约校验（验证 `AIRY_IPC_MAGIC`）
- 版本信息展示
- 后续版本扩展为完整的 Operator 控制面客户端（list/get/create/delete）

## 2. 命令行接口

```
用法: agentctl [选项]

AirymaxOS 云原生控制面 CLI 工具。

选项:
  -h, --help      显示帮助
  -v, --version   显示版本
  -s, --status    读取 /proc/airy/status 并打印

无参数时打印版本横幅与 A-IPC magic 校验结果。
```

## 3. 默认输出示例

```bash
$ agentctl
agentctl v0.1.1 - Airymax cloud-native control plane
agentctl: A-IPC magic=0x41524531 ('ARE1') hdr_size=128 字节 — 契约一致
agentctl: 使用 -h 查看帮助
```

## 4. -s 状态输出示例

```bash
$ agentctl -s
=== /proc/airy/status ===
agent_count: 3
active_agents: 2
fault_count: 0
ipc_messages: 12345
```

如果 AirymaxOS 内核模块未加载，会输出错误提示：

```bash
$ agentctl -s
agentctl: 无法打开 /proc/airy/status: No such file or directory
agentctl: 提示 — AirymaxOS 内核模块未加载时该文件不存在
```

## 5. A-IPC Magic 校验

agentctl 默认行为会验证 [SC] `ipc.h` 中的 `AIRY_IPC_MAGIC` 是否为
`0x41524531u`（'ARE1'）。这确保 cloudnative CLI 与 [SC] 头文件
契约一致。

```c
if (AIRY_IPC_MAGIC != 0x41524531u) {
    /* 错误处理 */
}
```

由于 [SC] 是 SSoT（Single Source of Truth），此校验在正常构建中
永远不会失败，但作为防御性编程保留。

## 6. 构建与安装

### 6.1 构建

```bash
cd cloudnative
mkdir build && cd build
cmake ..
make
```

产物：`agentctl/agentctl`

### 6.2 安装

```bash
sudo make install
# 默认安装到 /usr/local/bin/agentctl
```

## 7. IRON-9 同源关系

| 层级 | 实体 | 说明 |
|------|------|------|
| `[SC]` | `AIRY_IPC_MAGIC` | 与 agentrt 共享 A-IPC 消息头 magic |
| `[SC]` | `struct airy_ipc_msg_hdr` | 共享消息头布局 |
| `[IND]` | agentctl CLI 实现 | agentrt-linux 专属 |

agentrt 用户态有对应的 `airyctl` 工具（ecosystem/airyctl），
两者通过 [SC] 共享 A-IPC 契约，但 CLI 实现完全独立。

## 8. 后续路线

| 版本 | 计划 |
|------|------|
| 0.1.1 | 当前实现：-h/-v/-s 选项 + magic 校验 |
| 1.0.1 | 增加 `agentctl list/get/create/delete` 子命令 |
| 1.0.1 | 增加 CRD YAML 解析（依赖 `crd/`） |
| 1.0.1 | 增加 Controller RPC 调用（依赖 `controller/`） |

## 9. 变更历史

| 版本 | 日期 | 变更 |
|------|------|------|
| 0.1.1 | 2026-07-20 | 初始最小可编译实现：getopt_long + magic 校验 + 状态读取 |
