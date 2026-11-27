# SPDX-License-Identifier: GPL-2.0-only

# CRD — Agent 自定义资源

> **子仓**：`cloudnative/`
> **组件**：`crd/`（模式表、校验与规划）、`manifests/`（集群声明）
> **[SC] 依赖**：经 `cnjson` 读取 kubectl 输出的 JSON；不依赖内核头
> **协同组件**：`agentctl/`（集群侧应用与查询）
> **设计文档**：`docs-linux/150-cloudnative/01-k8s-crd-design.md`

Copyright (c) 2025-2026 SPHARX Ltd. All Rights Reserved.

License: GPL-2.0-only

---

## 1. 资源模型

四个自定义资源同属 `agent.airymaxos.dev/v1`，命名空间作用域：

| CRD | 用途 | 关键字段 | 清单 |
|-----|------|----------|------|
| `Agent` | 单个 Agent 实例 | `spec.image`／`spec.tokenBudget`／`spec.cognition` | `crd/manifests/crd-agent.yaml` |
| `AgentWorkflow` | 多 Agent 编排 | `spec.steps[].dependsOn` | `crd/manifests/crd-agentworkflow.yaml` |
| `AgentBudget` | 命名空间配额 | `spec.total`／`spec.refillRate` | `crd/manifests/crd-agentbudget.yaml` |
| `MemoryRovolClaim` | 记忆卷载声明 | `spec.layers`／`spec.capacity` | `crd/manifests/crd-memoryrovolclaim.yaml` |

清单的必填、枚举与区间是本仓声明来源；`crd/` 内的模式表是本地
判定的来源，两者由 `ci/gates/crd-schema.sh` 断言一致。

## 2. 本地判定与规划

`airy-crdctl` 消费 `kubectl get -o json` 的输出：

```
airy-crdctl validate agent.json    # 模式通过输出 ok，失败退出码 3
airy-crdctl plan agent.json        # 输出五步动作序列
airy-crdctl state Running stop     # 转移表单项判定
```

`plan` 的五步顺序固定：配额准入、内核注册、记忆卷载、安全注入、
调度切换；每行一个动作，顺序即执行顺序。输出不含时间与主机信息，
可逐字节比对。

## 3. 状态机

`Agent` 的八个状态与设计文档一致：`Creating`、`Registered`、`Ready`、
`Running`、`Blocked`、`Suspended`、`Terminating`、`Dead`。

`stop` 事件对除 `Dead`／`Terminating` 之外的所有状态进入
`Terminating`；`die` 事件只接受 `Running`／`Blocked`／`Suspended`／
`Terminating` 四个来源；其余组合返回拒绝，不做兜底放行。

## 4. 与 [SC] 的契约

| CRD 字段 | [SC] 侧消费 | 说明 |
|----------|-------------|------|
| `spec.security.capabilities` | Cupolas capability 令牌 | 逐项签发，缺失即拒绝启动 |
| `spec.security.landlockPaths` | Landlock 规则 | 路径白名单，越界访问返回 `EACCES` |
| `spec.cognition.scheduler` | 用户态调度器 | `stc_agent` 或 `SCHED_NORMAL` |
| `spec.tokenBudget` | 内核 Agent 注册 | 注册时签订预算契约 |

## 5. 不做的事

- 不实现 API Server 的准入 webhook：模式校验在清单与本地两处闭环，
  集群侧的拒绝由 Kubernetes 自身完成。
- 不实现 CSI 插件本体：`MemoryRovolClaim` 只声明意图，绑定由
  CSI 组件负责。
