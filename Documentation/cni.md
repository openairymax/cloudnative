# SPDX-License-Identifier: GPL-2.0-only

# CNI — 链式插件与网络策略

> **子仓**：`cloudnative/`
> **组件**：`cni/`（NetConf 归一、策略判定与注册记录）
> **[SC] 依赖**：经 `cnjson` 读取 NetConf；不依赖内核头
> **协同组件**：主 CNI 插件（创建接口）、Cupolas 网络穹顶（执行判定）
> **设计文档**：`docs-linux/150-cloudnative/03-cni-network-policy.md`

Copyright (c) 2025-2026 SPHARX Ltd. All Rights Reserved.

License: GPL-2.0-only

---

## 1. 定位

`airy-cni` 是 CNI conflist 中的**链式插件**：主插件完成 veth 创建与地址
配置后，把结果写进 `prevResult`，本插件从其 `ips[0].address` 取容器地址，
按默认拒绝模型判定每条流，并把策略写成一份运行时注册记录，供执行面
消费；ADD 结束时把 `prevResult` 原样回传给运行时。

本组件**不创建**网络接口、**不配置**路由，也**不下发** eBPF。接口与
数据面的执行由主插件和 Cupolas 内核穹顶承担，本插件只产出策略意图与
可审计的注册记录。这样拆分的原因是：插件在 kubelet 的短生命周期调用
里运行，无法长期持有 tc/XDP 程序；把判定与执行分开，策略才能在离线
环境复算。

## 2. NetConf 字段

| 字段 | 缺省 | 说明 |
|------|------|------|
| `cniVersion` | — | 接受 `1.*`、`0.4.0`、`0.3.1` |
| `name`／`type` | — | 必填，缺失即拒绝 |
| `cupolasEnabled` | `true` | 为真时写注册记录，`CHECK` 才核对 |
| `defaultDeny` | `true` | 入站默认拒绝 |
| `auditEnabled` | `true` | 随记录交给审计面 |
| `allowedPeers` | 空 | 放行的来源 Agent 名（`agent.airymaxos.dev/name`） |
| `ingressPorts` | `7766`、`7767` | AgentsIPC 标准与流式端口 |
| `egressPorts` | `53`、`443` | DNS 与 HTTPS |
| `egressExceptions` | `10.0.0.0/8`、`169.254.0.0/16` | 禁止出站的内网与元数据段 |
| `stateDir` | `/run/airymax/cupolas/net` | 注册记录目录 |

数组字段整体覆盖缺省；标量字段缺失时保留缺省。`prevResult` 原文保留，
ADD 时逐字节回传，不重新序列化。

## 3. 判定语义

判定只读 NetConf，与运行时状态无关，因此可在门禁里逐条复算。

| 方向 | 条件 | 结果 | 理由标记 |
|------|------|------|----------|
| 入站 | `defaultDeny` 为假 | 放行 | `open` |
| 入站 | 对端不在 `allowedPeers` | 拒绝 | `peer-denied` |
| 入站 | 端口不在 `ingressPorts` | 拒绝 | `port-denied` |
| 入站 | 对端与端口均命中 | 放行 | `peer-port` |
| 出站 | 目的落在 `egressExceptions` | 拒绝 | `egress-exception` |
| 出站 | 端口不在 `egressPorts` | 拒绝 | `egress-denied` |
| 出站 | 端口命中且不属例外 | 放行 | `egress-port` |

出站先判例外再判端口，保证内网与链路本地地址即使端口放行也不可达。
理由标记是稳定英文标识，供审计比对，不随语言变化。

## 4. 注册记录

ADD 在 `stateDir` 下写 `<containerId>.json`，先写 `.tmp`，`fflush`、
`fsync` 与 `fclose` 逐项判错，成功后才 `rename`，避免半写文件被执行面
读到。记录字段为容器标识、网络命名空间、接口名、地址、默认拒绝与审计
开关、放行对端、入站与出站端口、出站例外。

容器标识只接受字母、数字与 `-_.`，越界或含路径分隔符即拒绝，防止记录
写出 `stateDir`。DEL 删除记录，记录不存在视为成功，满足 CNI 的幂等
要求；DEL 不因 NetConf 解析失败而中断清理。

## 5. 命令行与退出码

```
airy-cni version
airy-cni eval --dir ingress|egress --conf FILE [--peer NAME] [--port N] [--dst ADDR]
CNI_COMMAND=ADD|DEL|CHECK|VERSION airy-cni < netconf.json
```

`eval` 是离线判定入口：只读一份 NetConf 与一条流，输出
`verdict=allow|deny reason=<标记>`，不落记录，供门禁与审计回放使用。

| 退出码 | 含义 |
|--------|------|
| 0 | 成功／放行 |
| 2 | 用法或命令错误 |
| 3 | NetConf 语法、版本或链式装配不符 |
| 6 | 判定拒绝（仅 `eval`） |

ADD 要求 `prevResult` 非空；缺失即退 3，提示须作为链式插件运行，而不是
静默补一个空地址。`CHECK` 在 `cupolasEnabled` 为假时直接放行，为真时
核对记录存在。

## 6. 不做的事

- 不创建 veth、不配置地址与路由：由主插件负责。
- 不下发 tc/XDP 程序：由 Cupolas 内核穹顶负责，本插件只提供策略意图。
- 不做 Kubernetes NetworkPolicy 到本配置的转换：由控制器生成 NetConf。
