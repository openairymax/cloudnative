# cloudnative 子仓文档索引

> **子仓定位**：AirymaxOS 云原生控制面 —— agentctl CLI + CRD + Controller + Scheduler + Registry + Packager。

Copyright (c) 2025-2026 SPHARX Ltd. All Rights Reserved.

License: GPL-2.0-only

---

## 文档清单

### 已有文档

| 文档 | 说明 |
|------|------|
| [agentctl.md](agentctl.md) | agentctl CLI 工具设计 |
| [crd.md](crd.md) | AiryAgent / AiryService CRD 设计 |

### 待编写文档（0.1.1 → 1.0.1）

| 文档 | 说明 | 计划版本 |
|------|------|---------|
| `controller.md` | Operator 控制器设计 | 1.0.1 |
| `scheduler.md` | Agent 调度器设计 | 1.0.1 |
| `registry.md` | Agent 镜像仓库设计 | 1.0.1 |
| `packager.md` | Helm-like 打包工具设计 | 1.0.1 |

## 相关文档

- [07-directory-structure.md](../../../docs/AirymaxOS/10-architecture/07-directory-structure.md) §4.6 — cloudnative 子仓完整目录结构
- [ipc.h](../../kernel/include/uapi/linux/airymax/ipc.h) — [SC] A-IPC 消息头契约
- 顶层 [CONTRIBUTING.md](../CONTRIBUTING.md) — cloudnative 子仓贡献指南
