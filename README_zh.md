**语言:** [English](README.md) | 简体中文

# agentrt-linux 云原生（AirymaxOS CloudNative）

[![Version](https://img.shields.io/badge/version-0.1.1-5a6b7e)](https://atomgit.com/openairymax/cloudnative)
[![License](https://img.shields.io/badge/license-AGPL--3.0+Apache--2.0-4a90d9)](LICENSE)

> [agentrt-linux（AirymaxOS）](https://atomgit.com/openairymax/agentrt-linux)（智能体操作系统）的云原生子系统。
> 由 [agentrt-linux](https://atomgit.com/openairymax/agentrt-linux) 管理仓聚合的叶子仓之一。
> 复用并扩展 Airymax `gateway` 与 `sdk` 模块以提供 OS 级云原生编排能力。

---

## 概述

**agentrt-linux 云原生（AirymaxOS CloudNative）**（`airymaxos-cloudnative`）是 agentrt-linux（AirymaxOS）（智能体操作系统）的云原生编排子系统。它提供面向智能体工作负载的 Kubernetes CRD、agent 感知的 containerd shim、OCI 镜像规范、CNI 网络、`agentctl` CLI 与超节点 OS 镜像——这是在云原生基础设施上打包、调度并运维 agentrt-linux 智能体的用户面。

在 Airymax 0.1.1 中，本仓库为**文档体系完成**，包含设计文档、参考发行版规范及架构草案。实际的内核与 OS 开发在 1.0.1 版本进行。

### 核心技术

- **Kubernetes CRD** — 建模智能体、认知循环与超节点的自定义资源
- **containerd shim** — 与 agentrt-linux 认知引擎集成的 agent 感知运行时 shim
- **OCI 镜像** 规范实现可复现、签名的智能体与 OS 制品
- **CNI** 网络提供超节点互联与智能体通信
- **agentctl** — 部署、检查并运维 agentrt-linux 智能体的统一 CLI
- **超节点 OS** — 启动超节点并加入 agentrt-linux 网格的最小 OS 镜像

### 与 Airymax gateway + sdk 的关系

agentrt-linux 云原生（AirymaxOS CloudNative）复用并扩展了 Airymax 运行时平台的 `gateway` 与 `sdk` 模块。网关路由、控制面抽象与 SDK 客户端面在用户态运行时（agentrt）与 OS 级云原生层（agentrt-linux）之间共享，确保架构同源、无适配层。

## 仓库结构（0.1.1（文档体系完成））

```
cloudnative/
├── README.md           # 本文件（英文）
├── README_zh.md        # 中文翻译
├── LICENSE             # AGPL-3.0 + Apache-2.0 双许可证
├── NOTICE              # 版权、商标与第三方声明
└── .gitignore
```

设计文档与 参考发行版规范维护在伞仓的 `docs/AirymaxAgentOS/` 目录。

## 上下游依赖

### 上游

- **agentrt-linux 内核（AirymaxOS Kernel）** — 提供超节点 OS 镜像所携带的内核镜像与原语
- **agentrt-linux 系统态（AirymaxOS System）** — 提供超节点 OS 镜像消费的 RPM/dnf 打包面
- **Airymax gateway + sdk** — 提供被复用并扩展的网关路由与 SDK 客户端面

### 下游

- **Cluster operators** — 通过 CRD 与 agentctl 面部署并运维 agentrt-linux 智能体的集群运维者
- **Hyper-node fleet** — 启动超节点 OS 镜像并加入 agentrt-linux 网格的超节点集群

## 分支策略

本叶子仓在 **`feature/official-hubs-01`** 分支上开发。聚合管理仓 `agentrt-linux` 保持在 `main` 分支。

## 许可证

采用 **AGPL v3 + Apache 2.0** 双许可证（SPDX：`AGPL-3.0-or-later OR Apache-2.0`）。完整文本见 [LICENSE](LICENSE)。

Copyright (c) 2025-2026 SPHARX Ltd. All Rights Reserved.
