# 贡献指南 - cloudnative 子仓（云原生控制面）

> **治理依据**：[`docs/AirymaxOS/50-engineering-standards/07-maintainers-and-governance.md`](https://github.com/openairymax/docs/blob/main/AirymaxOS/50-engineering-standards/07-maintainers-and-governance.md)
> **开发流程**：[`docs/AirymaxOS/50-engineering-standards/05-development-process.md`](https://github.com/openairymax/docs/blob/main/AirymaxOS/50-engineering-standards/05-development-process.md)

Copyright (c) 2025-2026 SPHARX Ltd. All Rights Reserved.

License: GPL-2.0-only

---

## 1. 子仓职责

cloudnative 子仓负责 AirymaxOS 的云原生控制面，包括：

- **agentctl**（`agentctl/`）：命令行控制工具（[IND] 独立实现）
- **CRD 定义**（`crd/`）：AiryAgent / AiryService 等 CRD
- **Controller**（`controller/`）：Operator 控制器
- **Scheduler**（`scheduler/`）：Agent 调度器
- **Registry**（`registry/`）：Agent 镜像仓库
- **Packager**（`packager/`）：Helm-like 打包工具

### IRON-9 主层

- `[IND]`：全部组件均为 agent-linux 专属实现
- `[SC]`：与 agentrt 共享 `ipc.h`（A-IPC 消息头）等契约

## 2. 开发环境要求

- **C 标准**：GNU C11（用户态 CLI）
- **编译器**：gcc ≥ 11 或 clang ≥ 14
- **构建系统**：CMake ≥ 3.20
- **内核头**：通过 `-I../kernel/include` 引用 [SC] 头文件

### [SC] 头文件引用约束（OS-IRON-014）

- [SC] 头文件**唯一物理宿主**：`../kernel/include/uapi/linux/airymax/`
- 本子仓通过 `-I` 引用，**禁止物理副本**
- CMakeLists.txt 配置：
  ```cmake
  include_directories(${CMAKE_SOURCE_DIR}/../kernel/include)
  include_directories(${CMAKE_SOURCE_DIR}/../kernel/include/uapi/linux)
  ```

## 3. 构建命令

```bash
mkdir build && cd build
cmake ..
make
```

构建产物：
- `agentctl/agentctl` — CLI 控制工具

## 4. 代码规范

### 4.1 C 代码风格

- **OS-STD-FMT-001**：Tab-8 缩进（由 `.clang-format` 强制）
- **OS-STD-FMT-002**：80 列硬限制
- **GPL-2.0-only**：所有 `.c` / `.h` 文件必须包含 `SPDX-License-Identifier: GPL-2.0-only`
- **版权头**：`Copyright (c) 2025-2026 SPHARX Ltd.`

### 4.2 A-IPC Magic 常量（[SC] ipc.h）

| 常量 | 值 | 说明 |
|------|------|------|
| `AIRY_IPC_MAGIC` | `0x41524531u` | 'ARE1'，IPC 消息头 magic |
| `AIRY_IPC_HDR_SIZE` | `128` | 消息头固定长度 |

## 5. 提交规范

### 5.1 DCO 签名

```bash
git commit -s
```

### 5.2 提交信息格式

```
cloudnative: 简短描述（≤72 字符）

详细说明 what 和 why，72 字符换行。

Signed-off-by: Your Name <your.email@example.com>
```

### 5.3 子系统前缀

| 前缀 | 范围 |
|------|------|
| `cloudnative:` | cloudnative/ 子仓整体 |
| `cloudnative: agentctl:` | agentctl CLI |
| `cloudnative: crd:` | CRD 定义 |
| `cloudnative: controller:` | Operator 控制器 |

## 6. 分支策略

- **开发分支**：`feature/official-hubs-01`
- PR 目标分支：`feature/official-hubs-01`

## 7. 测试要求

- **OS-STD-TEST-***：所有变更必须包含或更新测试
- 单元测试：随代码放在各组件目录
- 集成测试：在 `tests-linux/` 子仓

## 8. 审查流程

1. 向 `feature/official-hubs-01` 提交 PR
2. CI 运行：SSoT 校验 + CMake 构建 + 测试
3. 至少一名维护者审批
4. Squash-merge

## 9. 报告问题

- **Bug**：在 [cloudnative issues](https://github.com/openairymax/cloudnative/issues) 提交
- **设计讨论**：使用管理仓的 GitHub Discussions
