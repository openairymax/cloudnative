**Language:** English | [简体中文](README_zh.md)

# cloudnative — agent-linux (AirymaxOS) CloudNative

[![Version](https://img.shields.io/badge/version-0.1.1-5a6b7e)](https://atomgit.com/openairymax/cloudnative)
[![License](https://img.shields.io/badge/license-AGPL--3.0+Apache--2.0-4a90d9)](LICENSE)

> Cloud-native subsystem of [agent-linux (AirymaxOS)](https://atomgit.com/openairymax/agent-linux) — the AI Agent Operating System.
> One of the 8 leaf repositories aggregated by the [agent-linux](https://atomgit.com/openairymax/agent-linux) management repo.
> Reuses and extends the Airymax `gateway` and `sdk` modules for OS-level cloud-native orchestration.

Copyright (c) 2025-2026 SPHARX Ltd. All Rights Reserved.

---

## Positioning

The **cloudnative** leaf repository is the cloud-native orchestration subsystem of
agent-linux (AirymaxOS). It delivers Kubernetes CRDs for agent workloads, a
containerd shim for agent-aware runtime, OCI image conventions, CNI networking,
the `agentctl` CLI, and the hyper-node OS image — the surface that packages,
schedules and operates agent-linux agents across cloud-native infrastructure.

## Core Responsibilities

- **Kubernetes CRD** — custom resources modelling agents, cognition loops and hyper-nodes.
- **containerd shim** — agent-aware runtime shim integrating with the `cognition` engine.
- **OCI image** conventions for reproducible, signed agent and OS artifacts.
- **CNI** networking for hyper-node interconnect and agent communication.
- **agentctl** — unified CLI for deploying, inspecting and operating agent-linux agents.
- **Hyper-node OS** — the minimal OS image that boots a hyper-node and joins the agent-linux mesh.

## Relationship with Airymax `gateway` + `sdk`

The cloudnative leaf repo reuses and extends the `gateway` and `sdk` modules from
the Airymax runtime platform. The gateway routing, control-plane abstractions and
SDK client surface are shared between the user-space runtime (`agentrt`) and the
OS-level cloud-native layer, ensuring architectural homology with no adaptation
layer.

## Document & File List

```
cloudnative/
├── README.md           # This file (English)
├── README_zh.md        # Chinese translation
├── LICENSE             # AGPL-3.0 + Apache-2.0 dual license
├── NOTICE              # Copyright, trademark and third-party notices
├── .gitignore
└── .github/
    └── README.md       # GitHub automation for this leaf repo
```

Design documents and reference distribution specifications are maintained in the
`docs/AirymaxOS/` directory of the umbrella documentation repository.

## CI Status

CloudNative changes are governed by management-repository workflows (each ≤ 2 jobs):

| Workflow | Jobs | Applies to cloudnative via |
|----------|------|------------------------------|
| `mgmt-orchestrator.yml` | `file-integrity` + `orchestrate-leaf-ci` | Verifies the `cloudnative/` submodule dir; aggregates this repo's CI status |
| `release.yml` | `build-and-sign` (SBOM scan of `cloudnative/`; OCI image signed with cosign) + `publish-release` (push OCI image to registry) | Release tag |
| `nightly.yml` | `nightly-test-suite` (chaos: net partition) + `nightly-revert-or-budget` | Nightly cron |
| `ssot-validate.yml` | `ssot-syntax-and-rules` + `ssot-cross-ref` | When docs reference cloud-native rules |

Language-level CI (Go for CRD/controller, Rust for containerd shim, OCI image
build) is delegated to this leaf repository's own `.github/workflows/`.

## Development Guide

- **Branch**: `feature/official-hubs-01` (the management repo stays on `main`).
- **DCO**: every commit must be `Signed-off-by` (`git commit -s`).
- **Commit prefix**: `cloudnative:`.
- **Code style**: Go — `gofmt`; Rust — `cargo fmt` (4-space); run formatters before submitting.
- **OCI signing**: release OCI images are signed with cosign (key in CI secret `COSIGN_KEY`).
- **Function prefix**: `airy_*` for any kernel-adjacent helpers.

## Upstream & Downstream

- **Upstream** — `kernel` (kernel image for the hyper-node OS); `system` (RPM/dnf packaging surface); Airymax `gateway` + `sdk`.
- **Downstream** — cluster operators (deploy via CRD + `agentctl`); hyper-node fleet (boots the hyper-node OS image and joins the mesh).

## License

Dual-licensed under **AGPL v3 + Apache 2.0** (SPDX: `AGPL-3.0-or-later OR Apache-2.0`).
See [LICENSE](LICENSE) for the full text.

Copyright (c) 2025-2026 SPHARX Ltd. All Rights Reserved.
