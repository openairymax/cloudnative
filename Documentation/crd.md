# CRD — AiryAgent / AiryService 自定义资源定义

> **子仓**：`cloudnative/`
> **组件**：`crd/`（规划中，本文件为设计占位）
> **[SC] 依赖**：`kernel/include/uapi/linux/airymax/ipc.h`、`lsm_types.h`
> **协同组件**：`agentctl/`（CRD 通过 agentctl 应用）
> **设计文档**：`docs/AirymaxOS/10-architecture/07-directory-structure.md` §4.6

Copyright (c) 2025-2026 SPHARX Ltd. All Rights Reserved.

License: GPL-2.0-only

---

## 1. 设计目标

CRD（Custom Resource Definition）子组件定义 AirymaxOS 在 Kubernetes
集群中的自定义资源，使 Agent 能够以声明式方式部署与管理：

| CRD | 用途 | 关键字段 |
|-----|------|----------|
| `AiryAgent` | 单个 Agent 实例 | `spec.image` / `spec.capabilities` / `spec.memoryTier` |
| `AiryService` | Agent 服务（多副本） | `spec.replicas` / `spec.agentTemplate` |
| `AiryCapability` | Capability 配置 | `spec.perms` / `spec.epoch` |
| `AiryMemoryPolicy` | MemoryRovol 策略 | `spec.tiers` / `spec.agingPolicy` |

## 2. AiryAgent CRD 草案（设计）

```yaml
apiVersion: airy.os/v1
kind: AiryAgent
metadata:
  name: agent-example
  namespace: default
spec:
  image: registry.spharx.com/airy/agent-base:0.1.1
  capabilities:
    - perm: SEND
    - perm: RECV
    - perm: CALL
  memoryTier:
    hot: 128Mi
    warm: 512Mi
    cold: 2Gi
  cognition:
    thinkMode: FAST
    confidenceThreshold: 0.75
  security:
    capSpaceRoot: "/airy/caps/agent-example"
    freezeOnFault: true
status:
  phase: Running
  agentId: 42
  faultCount: 0
  lastHeartbeat: "2026-07-20T10:00:00Z"
```

## 3. 与 [SC] 的契约映射

| CRD 字段 | [SC] 类型 | 说明 |
|----------|-----------|------|
| `spec.capabilities[*].perm` | `AIRY_CAP_PERM_*` | 权限位（ipc.h） |
| `spec.cognition.thinkMode` | `enum airy_think_mode` | FAST / SLOW |
| `spec.cognition.confidenceThreshold` | `airy_q16_t` | Q16.16 定点 |
| `spec.security.capSpaceRoot` | `struct airy_task_sec.cap_space_root` | Capability 根 |
| `spec.memoryTier.hot/warm/cold` | `enum airy_mem_level` | L1-L4 层级 |

CRD 字段必须与 [SC] 类型严格对应，确保用户态 YAML 描述与内核态
数据结构语义一致。

## 4. IRON-9 同源关系

| 层级 | 实体 | 说明 |
|------|------|------|
| `[SC]` | CRD 字段映射的类型 | 跨用户态/内核态共享 |
| `[IND]` | CRD YAML schema 与 controller 实现 | agent-linux 专属 |

## 5. 当前实现状态

0.1.1 版本中 CRD 仅作为设计占位：

- ✅ [SC] 类型已完成（ipc.h / lsm_types.h / cognition_types.h / memory_types.h）
- ⏳ `crd/` 子目录与 Go CRD schema 尚未实现（1.0.1）
- ⏳ Controller（`controller/`）尚未实现（1.0.1）

当前 agentctl（0.1.1）不解析 CRD YAML，仅提供状态查询与 magic 校验。

## 6. 变更历史

| 版本 | 日期 | 变更 |
|------|------|------|
| 0.1.1 | 2026-07-20 | 初始设计文档；[SC] 类型映射草案 |
