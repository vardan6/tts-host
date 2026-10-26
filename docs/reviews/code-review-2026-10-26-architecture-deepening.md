# Cross-review — architecture deepening

- Date: 2026-10-26.
- Range: `a47ebe4c93f701c906826ba4761d57c617f1e472..56155ef7531339701f5acc00b41474f093e55a03` (`main..HEAD`; merge base equals `main`). Uncommitted configuration changes excluded.
- Reviewer: GPT-6 / OpenAI Codex. Implementer: Opus 5.5 / Anthropic, identified by the developer; different-provider review.
- Docs consulted: [architecture deepening roadmap](../../roadmap.md#architecture-deepening), [architecture review](architecture-review-2026-10-25.md), [speech queue design](../design/architecture.md#speech-queue), [profile resolution design](../design/architecture.md#profile-resolution), [configuration](../design/architecture.md#configuration), [load rejection](../design/architecture.md#load-rejection), [local API](../design/architecture.md#local-api), [model installation and updates](../design/architecture.md#model-installation-and-updates), [maintenance actions](../design/architecture.md#model-removal-repair-and-maintenance-actions), [desktop integration](../design/architecture.md#desktop-integration), [product requirements](../requirements/product.md), [implementation notes](../implementation-notes.md), ADRs [0002](../adr/0002-runner-protocol.md), [0003](../adr/0003-no-authentication.md), [0004](../adr/0004-json-canonical-config.md), [0007](../adr/0007-native-ui-per-platform.md), [0010](../adr/0010-model-revisions-storage-and-release-layout.md), and the [maintenance review follow-ups](code-review-2026-10-24-model-maintenance.md).
- Project instructions: `AGENTS.md`, `PROJECT.md`, and the referenced RTK instructions. No implementation files changed.

| # | Finding | Evidence (file:line) | Severity | Risk | Value | Effort |
|---|---------|----------------------|----------|------|-------|--------|
| 1 | The promised single home for profile reads is incomplete: Settings model selection still interprets `fallbackProfile`, `idleUnloadSeconds`, and the fallback profile's `model` directly in config persistence. The new design says all reads of these fields go through the typed profile view. See Details #1. | `docs/design/architecture.md:408`; `src/config_persistence.cpp:69`; `src/config_persistence.cpp:70`; `src/config_persistence.cpp:74`; `src/config_persistence.cpp:75` | low | low | med | low |

## Details

### #1 — Finish moving profile reads behind the typed view

The newly captured design explicitly centralizes profile reads, while allowing Settings to keep writing the JSON keys. `with_selected_model()` still reads the old profile's fallback and idle timeout by key, and separately reads the fallback profile's model to prevent selecting the same model through fallback. These are semantic profile reads, rather than merely constructing the candidate JSON. This file is unchanged in the branch: the finding is incomplete delivery of the new architectural contract, not a newly introduced runtime regression.

Use `profile_view()` for the existing profile and its fallback when constructing the selection candidate; keep JSON writes in config persistence. Preserve the current behavior of dropping a fallback that names the generated profile or selects the same model, retaining an explicit zero idle timeout, and preserving unrelated profiles. The deferral at architecture line 417 concerns the remaining Settings/audio/registry configuration reads; it does not explicitly exempt these profile reads. If an exemption was intended, resolve it in the captured design before declaring the profile-read consolidation complete.

## Verification and limits

- Linux Debug build: passed (`cmake --build build-linux -j 4`, no work needed).
- Full Linux suite: 53/55 passed. All changed-area tests passed, including model list, model update, resident synthesis, model session, profile resolution, speech queue, local API, and CLI language selection.
- The two full-suite failures were isolated from this branch's changes. Config persistence fails against the existing uncommitted example because it already contains `selected-model-en`, which the test incorrectly assumes is an untouched named profile. Running the same binary with a temporary copy of the committed example passed. The Linux tray check encountered port 7861 already in use; running with a temporary config on a free port produced the expected tray-not-implemented error. Neither temporary check changed the user's configuration or startup registration.
- A temporary C++ harness linked against the built model-update objects checked a healthy writable package alongside a damaged bundled copy of the same ID. Removal correctly warned that it was the last loadable model, and Update retained one previous version. The suspected damaged-copy classification regression was disproved and is not a finding: `damaged_package()` excludes IDs with a loadable copy.
- `git diff --check main...HEAD`: passed.
- Windows builds, real-model/GPU execution, and desktop click-through were not performed. The roadmap's combined model-row click-through remains open; automated tests do not close it.
- No runtime regression was confirmed. One low-severity architectural delivery gap; no separate enhancement findings.
