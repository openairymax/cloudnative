**Language:** English | [简体中文](README_zh.md)

# agentrt-linux CloudNative (AirymaxOS CloudNative)

[![Version](https://img.shields.io/badge/version-0.1.1-5a6b7e)](https://atomgit.com/openairymax/cloudnative)
[![License](https://img.shields.io/badge/license-AGPL--3.0+Apache--2.0-4a90d9)](LICENSE)

> Cloud-native subsystem of [agentrt-linux（AirymaxOS）](https://atomgit.com/openairymax/agentrt-linux) — the AI Agent Operating System.
> One of the leaf repositories aggregated by the [agentrt-linux](https://atomgit.com/openairymax/agentrt-linux) management repo.
> Reuses and extends the Airymax `gateway` and `sdk` modules for OS-level cloud-native orchestration.

---

## Overview

The **agentrt-linux CloudNative (AirymaxOS CloudNative)** (`airymaxos-cloudnative`) is the cloud-native orchestration subsystem of agentrt-linux（AirymaxOS）, the AI Agent Operating System. It delivers Kubernetes CRDs for agent workloads, a containerd shim for agent-aware runtime, OCI image conventions, CNI networking, the `agentctl` CLI, and the hyper-node OS image — the surface that packages, schedules and operates agentrt-linux agents across cloud-native infrastructure.

In agentrt-linux 0.1.1, this repository is **documentation complete** (文档体系完成) containing design documents, reference distribution specifications, and architectural drafts. Actual kernel and OS development takes place in version 1.0.1.

### Core Technologies

- **Kubernetes CRD** — custom resources modelling agents, cognition loops and hyper-nodes
- **containerd shim** — agent-aware runtime shim integrating with the agentrt-linux Cognition engine
- **OCI image** conventions for reproducible, signed agent and OS artifacts
- **CNI** networking for hyper-node interconnect and agent communication
- **agentctl** — unified CLI for deploying, inspecting and operating agentrt-linux agents
- **Hyper-node OS** — the minimal OS image that boots a hyper-node and joins the agentrt-linux mesh

### Relationship with Airymax gateway + sdk

The agentrt-linux CloudNative (AirymaxOS CloudNative) reuses and extends the `gateway` and `sdk` modules from the Airymax runtime platform. The gateway routing, control-plane abstractions and SDK client surface are shared between the user-space runtime (agentrt) and the OS-level cloud-native layer (agentrt-linux), ensuring architectural homology with no adaptation layer.

## Repository Structure (0.1.1 Documentation Complete)

```
cloudnative/
├── README.md           # This file (English)
├── README_zh.md        # Chinese translation
├── LICENSE             # AGPL-3.0 + Apache-2.0 dual license
├── NOTICE              # Copyright, trademark and third-party notices
└── .gitignore
```

Design documents and reference distribution specifications are maintained in the `docs/AirymaxAgentOS/` directory of the umbrella repository.

## Upstream & Downstream Dependencies

### Upstream

- **agentrt-linux Kernel (AirymaxOS Kernel)** — provides the kernel image and primitives that the hyper-node OS image ships
- **agentrt-linux System (AirymaxOS System)** — provides the RPM/dnf packaging surface consumed by the hyper-node OS image
- **Airymax gateway + sdk** — provides the gateway routing and SDK client surface that are reused and extended

### Downstream

- **Cluster operators** — deploy and operate agentrt-linux agents via the CRD and agentctl surface
- **Hyper-node fleet** — boots the hyper-node OS image and joins the agentrt-linux mesh

## Branch Strategy

This leaf repository is developed on **`feature/official-hubs-01`**. The aggregating `agentrt-linux` management repo stays on `main`.

## License

Dual-licensed under **AGPL v3 + Apache 2.0** (SPDX: `AGPL-3.0-or-later OR Apache-2.0`). See [LICENSE](LICENSE) for the full text.

Copyright (c) 2025-2026 SPHARX Ltd. All Rights Reserved.
