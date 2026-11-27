# SPDX-License-Identifier: GPL-2.0-only

# Shim — Agent 容器的运行时引擎

> **子仓**：`cloudnative/`
> **组件**：`shim/`（bundle 契约、内核桥与进程监管）
> **[SC] 依赖**：经 `cnjson` 读取 OCI bundle；调度控制走 `550`
> **协同组件**：`crd/`（Agent 资源声明）、集群侧 ttrpc 适配
> **设计文档**：`docs-linux/150-cloudnative/02-containerd-shim.md`

Copyright (c) 2025-2026 SPHARX Ltd. All Rights Reserved.

License: GPL-2.0-only

---

## 1. 范围

`airy-shim` 是 Agent 容器从集群声明落到本地进程的最后一环：读取
OCI bundle，按注解契约归一运行参数，向内核申请调度类，监管子进程
并把退出状态折算回调用方。

本组件只承担运行时引擎，不含 ttrpc 传输层。containerd 的 shim v2
协议栈（`Start`/`Wait`/`Create` 等服务方法）由集群侧适配进程调用
本引擎实现，传输与序列化不在本组件内，避免在无协议依赖的仓库里
另造一套 RPC。

## 2. bundle 注解契约

运行参数只从 `annotations` 读取，未识别的取值一律判负，不做静默
降级。缺省值在归一阶段补齐。

| 注解键 | 取值 | 缺省 |
|--------|------|------|
| `airymaxos.agent.token-budget` | 正整数（Token 总量） | `1000000` |
| `airymaxos.agent.memory-rovol` | `disabled` 或 `L1,L2,L3,L4` 的逗号列表 | 无层 |
| `airymaxos.agent.scheduler` | `stc_agent` 或 `eevdf` | `stc_agent` |
| `airymaxos.agent.cognition-cycle` | 仅 `CoreLoopThree` | `CoreLoopThree` |
| `airymaxos.agent.capabilities` | `MEMORY_READ`…`ADMIN` 的逗号列表 | `MEMORY_READ` |

能力名按位映射：`MEMORY_READ`=0 … `TOOL_EXEC`=8，`ADMIN`=63。
`process.args` 必填且非空；`process.env` 可省略；`root.path` 为相对
路径时按 bundle 目录展开。

## 3. 命令行

```
airy-shim check --bundle DIR [--id NAME]         # 只解析并打印规划
airy-shim run   --bundle DIR [--id NAME] [--cgroup PATH]
```

`check` 输出六行规划（admit／budget／memory／sched／security／exec），
合法 bundle 退出码 0，契约不符退出码 3，用法错误退出码 2。`run` 在
规划之后打印 `phase=Creating`、`phase=Running pid=`、`exit code=` 与
`phase=Dead`，并以子进程的退出状态退出（信号退出折算为 `128+信号`）。

## 4. 内核桥与降级语义

调度类通过 `550`（`AIRY_SYS_SCHED_CTL`）的 `SET_POLICY` 操作申请。
本版内核只实现入口契约，对合法请求回答 `-AIRY_ENOTSUP`，尚未内置
调度后端。引擎据此把结果标记为 `unsupported` 并继续启动进程：

```
shim sched class=stc_agent result=applied|unsupported|denied|error
```

`applied` 表示后端已接管，`unsupported` 表示入口可用但后端未构建，
`denied` 表示被安全门拒绝，`error` 表示参数或调用异常。降级不阻断
启动，是因为 Agent 容器的可运行性高于调度类切换；是否放行由
`result` 的值决定，而不是由引擎猜测。`eevdf` 走原生调度类，不进入
`550`，直接打印 `result=native`。

错误码按内核 UAPI 的紧凑空间原样解释（`error.h`：`ENOTSUP`=11、
`EPERM`=12、`EACCES`=1、`EINVAL`=5），不套用标准 errno 名称；两者
数值重叠但语义不同，混用会把「不支持」误判成「暂时不可用」。

## 5. 验证

`ci/gates/shim-runtime.sh` 在出树构建后断言：完整与缺省 bundle 的六行
规划、能力位、非法层名拒绝、真实子进程退出码折算、550 降级结果落在
既定集合、打包接线，以及引擎常量与内核 UAPI 的一致性。
