# `.github/` — cloudnative Leaf Repository Automation

> GitHub automation directory for the **cloudnative** leaf repository of
> [agentrt-linux (AirymaxOS)](https://atomgit.com/openairymax/agentrt-linux).

Copyright (c) 2025-2026 SPHARX Ltd. All Rights Reserved.

---

## Positioning

This directory hosts cloudnative-specific GitHub automation. Management-level
orchestration (governance integrity, release OCI image build + cosign signing +
publish, nightly chaos with net-partition, SSoT validation) runs in the management
repository; this leaf repository hosts language-level CI for the Go CRD/controller,
the Rust containerd shim, and OCI image builds.

## Directory Contents

```
cloudnative/.github/
└── README.md    # This file
```

## Applicable Management-Repository Workflows

| Workflow | Jobs | Relevance to cloudnative |
|----------|------|---------------------------|
| `mgmt-orchestrator.yml` | `file-integrity` + `orchestrate-leaf-ci` | Verifies the `cloudnative/` submodule dir exists; aggregates this repo's CI status |
| `release.yml` | `build-and-sign` + `publish-release` | `syft cloudnative/` SBOM; OCI image built, signed with cosign, pushed to `ghcr.io/spharx/airymaxos:<tag>` |
| `nightly.yml` | `nightly-test-suite` + `nightly-revert-or-budget` | Chaos injection (net partition) validates hyper-node interconnect resilience |
| `ssot-validate.yml` | `ssot-syntax-and-rules` + `ssot-cross-ref` | Validates rule IDs referenced in cloud-native documentation |

## Development Guide

- Add leaf-local workflows for Go unit/e2e tests (CRD controller), Rust containerd
  shim tests, and OCI image build/lint; keep each workflow ≤ 2 jobs.
- Release OCI images must be signed with cosign — the signing key is the CI secret
  `COSIGN_KEY` (passphrase `COSIGN_PASSWORD`); never log these.
- Kernel-adjacent helpers use the `airy_*` prefix; Go follows `gofmt`, Rust
  follows `cargo fmt`.

## License

Dual-licensed under **AGPL v3 + Apache 2.0** (SPDX: `AGPL-3.0-or-later OR Apache-2.0`).
See the repository root [LICENSE](../LICENSE) and [NOTICE](../NOTICE).

Copyright (c) 2025-2026 SPHARX Ltd. All Rights Reserved.
