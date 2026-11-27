# SPDX-License-Identifier: GPL-2.0-only

# 超节点 OS — 拓扑、令牌池与迁移准入

> **子仓**：`cloudnative/`
> **组件**：`supernode/`（拓扑聚合、亲和度、令牌池、迁移准入与快照）
> **[SC] 依赖**：经 `cnjson` 读取快照与令牌池；549 号调用仅在支持的内核上生效
> **协同组件**：A-ULS（`macro_d`，用户态生命周期）、MemoryRoVol 内核后端
> **设计文档**：`docs-linux/150-cloudnative/04-supernode-os.md`

Copyright (c) 2025-2026 SPHARX Ltd. All Rights Reserved.

License: GPL-2.0-only

---

## 1. 定位

`airy-supernode` 是超节点级生命周期的**用户态决策入口**：把 `/sys` 暴露的
NUMA 节点、CPU 拓扑与 CXL 设备聚合成一张超节点拓扑，按页访问轨迹给出
Agent 的目标 die，并维护跨 die 的令牌预算池；迁移前做准入判定，通过后
才调用 549 号 MemoryRoVol 控制接口完成实际搬运。整机快照与恢复用于故障
切换前的状态留档与硬件一致性核对。

本组件**不搬运页**、**不改内核调度器**、**不做 cgroup 装配**：页搬运由
MemoryRoVol 后端（里程 M5）承担，调度策略由 sched_tac 注入，cgroup 层级
由 A-ULS 维护。把决策与执行分开，准入规则才能在没有真实 CXL 硬件的环境
里逐条复算。

## 2. 拓扑来源与字段

| 层次 | 来源 | 说明 |
|------|------|------|
| die | `/sys/devices/system/node/nodeN` | 目录编号即 NUMA 节点标识 |
| 核数 | `nodeN/cpulist` | 解析 `0-2,5` 形式的列表 |
| 内存 | `nodeN/meminfo` | 取 `MemTotal`，单位 kB |
| 距离 | `nodeN/distance` | SLIT 行，缺省按 10 填充 |
| chip | `cpuN/topology/physical_package_id` | 取该节点最低编号 CPU 的 package |
| CXL 池 | `/sys/bus/cxl/devices/mem*` | 目录计数 |

`discover` 输出单行 JSON：`dies`、`chips`、`cores`、`memoryKiB`、
`cxlPools`，以及每个 die 的 `id`、`chip`、`cores`、`memoryKiB`、
`distance`。节点按标识升序落位，重复标识只累加不新增。

## 3. 令牌池

令牌池状态落在 `state/<pool>.json`，字段为总量、保留量与逐 die 的上限与
占用。初始化把总量扣除保留量后均分，余数归 0 号 die；`alloc` 只扣全局
余量，`borrow` 移动两个 die 的上限而不改全局余量。

| 操作 | 判据 | 结果 |
|------|------|------|
| `init` | 保留量不得吞掉全部总量 | 否则退 2 |
| `alloc` | 全局余量足够 | 否则退 4 |
| `borrow` | 源 die 的可借余量足够 | 否则退 6 |
| `status` | 状态文件存在且可解析 | 否则退 3 |

水位按占用比例给出：低于 5% 为 `critical`，低于 20% 为 `low`，其余
`ok`。水位只用于输出，不参与准入。

## 4. 亲和度

`affinity` 读取一份 `node=N pages=M` 的轨迹，按

```
score[dst] += pages[i] × 10000 / distance[dst][i]
```

累加得到每个 die 的加权得分，取最高者作为 `recommend`。距离来自 SLIT，
得分只在同一份拓扑内可比，不做跨机归一。轨迹行无法解析即退 3，缺轨迹
同样退 3。

## 5. 迁移准入与内核桥

迁移按固定顺序判定，任一不满足即拒绝并写审计：

| 顺序 | 判据 | 拒绝理由 |
|------|------|----------|
| 1 | 策略为 `hot`／`cold`／`increment` | `strategy` |
| 2 | 源与目的都在拓扑内且不相等 | `range` |
| 3 | 调用方持有迁移能力令牌 | `capability` |
| 4 | 令牌池可读且目的 die 有余量 | `budget` |

通过后调用 `syscall(549, AIRY_ROVOL_MIGRATE, agent, 0)`。本版后端里程为
M5，`kernel/kernel/syscalls/airy_rovol.h` 对已识别操作返回
`-AIRY_ENOTSUP`（紧凑码 11），未定义操作返回 `-AIRY_EINVAL`（5）；由于
`syscall()` 以 `-1` 加 `errno` 回传，用户态实际见到 `ENOSYS` 与 `EAGAIN`。
本组件把 `ENOSYS`／`EAGAIN` 归为 `unsupported`，`EPERM`／`ENOMEM` 归为
`denied`，其余归为 `failed`。数值与标准 errno 重合是紧凑码的既有约定，
判断以 UAPI 语义为准，不以字面数值推断。

未支持时退出码 7、审计记 `state=rolled-back`：没有真实搬运就不能记
`active`。`denied` 退 6，其余失败退 7，二者都记 `rolled-back`。审计写入
`state/migrate.json`，字段为 Agent、源与目的 die、策略、状态、内核标记与
理由。

## 6. 快照与恢复

`snapshot` 把拓扑摘要与令牌池写入一份 JSON：`version`、`dies`、`chips`、
`cores`、`memoryKiB`、`cxlPools` 与 `pool`。`restore` 重新扫描当前硬件，
五项摘要逐项比对；不符即 `restore=bad reason=hardware` 并退 3，令牌池
解析失败即 `reason=pool` 退 3，一致才回写令牌池。

快照只记录可复算的硬件摘要与预算状态，不包含进程内存。整机内存快照属于
MemoryRoVol 快照后端（M5），本组件不越位复制页。

## 7. 命令行与退出码

```
airy-supernode discover [--root DIR]
airy-supernode affinity --trace FILE [--root DIR] [--agent N]
airy-supernode pool init|alloc|borrow|status --state DIR [...]
airy-supernode migrate --state DIR --agent N --src D --dst D [--strategy S] [--cap]
airy-supernode snapshot --state DIR --output FILE [--root DIR]
airy-supernode restore --state DIR --input FILE [--root DIR]
```

缺省 `--root /sys`，`--state /run/airymax/supernode`。

| 退出码 | 含义 |
|--------|------|
| 0 | 成功 |
| 2 | 用法错误或保留量吞掉总量 |
| 3 | 拓扑、状态文件或快照不符 |
| 4 | 全局令牌余量不足 |
| 6 | 准入拒绝（策略、范围、能力、余量、内核拒绝） |
| 7 | 内核桥不支持或失败，未发生搬运 |

## 8. 不做的事

- 不搬运物理页：由 MemoryRoVol 后端负责，本组件只发控制调用。
- 不改 `sched_init_numa()` 与 `migrate_pages()`：保持内核基线不变。
- 不创建 cgroup slice：由 A-ULS 按超节点层级维护。
- 不复制进程内存：整机内存快照属于 MemoryRoVol 快照后端。
