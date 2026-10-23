# Code review — model maintenance

- Range reviewed: `01f1684a27f476f2a117ace3bd94ff57f3b73308..3688056ae8dc61f8f3eecfbb88b08b4ed7598f3c` (`main..feature/model-maintenance`). The review covers committed changes only.
- Reviewer: GPT-6 / Codex, OpenAI. Implementer: Opus 5.5, Anthropic, confirmed by the user; cross-provider review.
- Date: 2026-10-24.
- Docs consulted: [product requirements](../requirements/product.md), [architecture](../design/architecture.md) (runner/load rejection, configuration, discovery, catalogue, maintenance, desktop integration), ADRs [0002](../adr/0002-runner-protocol.md), [0009](../adr/0009-qwen-release-selection-without-bake-off.md), [0010](../adr/0010-model-revisions-storage-and-release-layout.md), [implementation notes](../implementation-notes.md), [bundled-models review and triage](code-review-2026-10-19-bundled-models.md), [roadmap](../../roadmap.md), and [active context](../../activeContext.md).
- Verification: Linux Debug build passed (`cmake --build build-linux -j 4`, no rebuild needed). CTest: 52/53 passed; `tts_host_tray_not_implemented_on_this_platform` cannot reach its expected error because port 7861 is occupied, including on an isolated rerun. Temporary-directory CLI reproductions confirmed #1, #3, and #4. #2 was verified against the Win32 control population logic; Windows UI execution, packaging, and real-model/GPU synthesis were not performed. Only this findings file was added.

| # | Finding | Evidence (file:line) | Severity | Risk | Value | Effort |
|---|---------|----------------------|----------|------|-------|--------|
| 1 | Verify and repair cannot inspect common Qwen damage: missing/empty assets or damaged GGUF headers make discovery reject the package, so verification reports “not installed” instead of offering repair. See Details #1. | `src/model_update.cpp:373`, `src/model_registry.cpp:147`, `src/model_registry.cpp:151`, `src/settings_window.cpp:1353` | med | med | high | med |
| 2 | Loading a model repopulates the General default-profile picker without clearing it. Duplicate entries accumulate and selection uses an index into the old entries, so the picker can display a different profile from the saved speech default. See Details #2. | `src/settings_window.cpp:821`, `src/settings_window.cpp:437`, `src/settings_window.cpp:448` | med | low | med | low |
| 3 | The new pre-synthesis prepare bypasses one-restart recovery for initial runner crashes, protocol failures, and deadlines. Requests now fail immediately for transient startup failures that previously reached the recovery loop. See Details #3. | `src/main.cpp:260`, `src/main.cpp:277`, `src/main.cpp:283`, `src/recovering_runner.cpp:37`, `src/recovering_runner.cpp:81` | med | med | high | med |
| 4 | Interrupted repair recovery assumes the package folder equals its model ID, although normal repair uses the discovered path. A renamed catalogue package can lose access to good files moved into staging, and recovery deletes the journal without restoring them. See Details #4. | `src/model_update.cpp:425`, `src/model_update.cpp:464`, `src/model_update.cpp:550`, `src/model_update.cpp:569` | med | med | high | med |

## Details

### #1 — Retain enough package identity to verify damaged Qwen assets

The maintenance design promises reports and repair for damaged or missing catalogue files. Verification first requires an entry in `scan.discovered_packages`. Qwen discovery, however, requires every declared asset to exist and be nonempty, and requires GGUF magic in both weight files. These are precisely failures Verify should diagnose. The resulting catalogue row is “not installed,” with Verify disabled; an invalid installed copy still occupies its destination folder.

Reproduction: create a minimal Qwen manifest with the compiled catalogue revision, four nonempty declared assets, and GGUF magic for its weights; delete `reference.wav`; invoke `--list-models` and `--verify-model qwen3-tts-1.7b`. Discovery returns zero packages and an unsupported entry for `/files/voice`; verification exits with `'qwen3-tts-1.7b' is not installed`. The missing-file tests exercise a Kokoro fixture, whose discovery does not enforce Qwen's asset checks.

Keep damaged-package metadata available to maintenance without allowing the package to load. Preserve schema/containment checks and storage-tier protection. Add coverage for a missing Qwen reference, missing mmproj, and corrupted GGUF header, including bundled report-only behavior.

### #2 — Reset the profile picker before refilling it

The new successful-Load path calls `populate_default_profile_combo` directly. That helper appends with `CB_ADDSTRING`, counts indices from zero, and selects `current_index`; it does not send `CB_RESETCONTENT`. Its earlier refresh caller explicitly resets the control first.

For example, when the original profiles are `fast`, `quality`, and `russian`, loading an English model adds `selected-model-en` to the saved configuration and appends all four profiles after the three existing rows. The helper selects index 3, which is the newly appended `fast`, while the saved default is `selected-model-en`. Subsequent loads accumulate more copies. This contradicts the requirement that the controls reflect the canonical configuration and makes the latest-choice workflow misleading.

Reset the combo before populating it, and verify repeated Model Load → General visits produce one entry per profile with the saved default selected. This can be a small Windows control-level regression check without loading real weights.

### #3 — Preserve recovery while distinguishing explicit load rejection

Previously the first runner start happened inside `RecoveringRunner::synthesize`, whose `RunnerLaunchError` handler logs a restart and retries the unplayed chunk once. The new `run_synthesis` calls `prepare()` before entering the pipeline. `prepare()` resets and rethrows every failure; the new caller catches only `RunnerLoadRejected`. Initial transport/protocol/deadline failures therefore escape before the recovery loop runs. Explicit load rejection should bypass retries, but ADR 0002 retains recovery for other failures.

Reproduction: use a temporary executable wrapper as `--runner`; it exits on its first invocation and delegates to the normal stub runner on subsequent invocations. The first CLI synthesis exits 1 with “runner closed its control channel before responding,” without a restart. A second identical CLI request succeeds and writes audio. No real weights or GPU are needed.

Make preparation participate in the recovery policy for non-rejection failures while retaining cancellation and no-retry handling for `RunnerLoadRejected`. Ensure the retry budget remains consistent for the first unplayed chunk; cover initialize and load transport failures separately from explicit load rejection.

### #4 — Journal the actual installed folder

Registry identity comes from `model.json`, and a valid catalogue package may be moved to another non-dot-prefixed folder without changing its ID or revision. Normal repair takes `verification.package_path`, but its journal contains only `repair`. Startup reconstructs the installed path as `directory / id`.

If repair is interrupted while moving good files out of `models/renamed-qwen`, recovery looks for `models/qwen3-tts-1.7b`. It cannot validate the incomplete staging directory or find a discard directory because the swap has not started, then unconditionally removes the journal. Good assets remain stranded in `.qwen3-tts-1.7b.downloading`, and the original package stays incomplete.

Reproduction: place a valid Qwen fixture in `renamed-qwen`, move its good mmproj into `.qwen3-tts-1.7b.downloading`, and write `.qwen3-tts-1.7b.repairing` containing `repair`, matching an interruption before the manifest is written. Startup reports zero discovered packages and missing mmproj; the journal is deleted, mmproj is not restored, and it remains in staging.

Persist the original package folder in the journal, validate that it stays within the registry directory, and use it during recovery. Retain the journal when no recovery branch succeeds. Extend interrupted-repair tests to a folder whose name differs from the manifest ID.

## Triage — 2026-10-24

Triaged by Opus 5.5, Anthropic. Every finding was checked against the code at
`3688056`; all four are confirmed. No implementation files were changed during
triage.

Must fix now (2): #3, #2.

| # | Finding | Risk | Value | Effort | Priority | Reason |
|---|---------|------|-------|--------|----------|--------|
| 3 | Pre-synthesis `prepare()` bypasses one-restart recovery for initial runner failures | med | high | med | must_fix_now | regression on this branch; breaks the ADR 0002 recovery contract for transient startup failures |
| 2 | Successful Load refills the General profile picker without clearing it | low | med | low | must_fix_now | regression on this branch; picker shows a profile other than the saved default; one-line fix |
| 1 | Verify/repair cannot reach Qwen packages with missing/empty assets or bad GGUF headers | med | high | med | should_fix_before_phase_complete | promised behaviour never worked for Qwen, not a regression; moot if Qwen is dropped, so it waits on that decision |
| 4 | Interrupted-repair recovery assumes the package folder equals its ID | med | med | med | should_fix_before_phase_complete | needs a manually renamed folder plus an interruption; value lowered from high for that narrow trigger, but a later repair's stale-file sweep would delete the stranded good files |

### Validation notes

- #3: `RecoveringRunner::prepare` (`src/recovering_runner.cpp:37`) resets and
  rethrows everything; the `prepared` lambda (`src/main.cpp:258`) catches only
  `RunnerLoadRejected`. `RunnerLaunchError` and `RunnerLoadRejected` are
  unrelated `std::runtime_error` subclasses, so a launch failure escapes
  before the retry loop in `synthesize` (`src/recovering_runner.cpp:81`) runs.
- #2: `populate_default_profile_combo` (`src/settings_window.cpp:428`) only
  appends; the refresh caller resets first (line 535) but the Load path
  (line 821) does not. Selecting a row saves its text (`CB_GETLBTEXT`,
  line 457), so a duplicate row still saves the right name; the defect is the
  displayed selection and the growing list.
- #1: `load_manifest_candidate` (`src/model_registry.cpp:136`) throws for any
  missing, empty, or non-GGUF asset of a `qwen3-gguf` package, so it never
  reaches `scan.discovered_packages` and `verify_catalogue_package`
  (`src/model_update.cpp:371`) reports "not installed". Kokoro has no such
  checks, which is why the existing tests pass.
- #4: the repair journal holds only `repair` (`src/model_update.cpp:462`), and
  recovery rebuilds `directory / id` (line 550), then removes the journal
  unconditionally (line 569). Catalogue install and update also target
  `directory / entry.id` (line 213), so a renamed folder is an edge case
  throughout maintenance; the fix stays scoped to the repair journal as the
  review proposes.


## Follow-up cross-review — 2026-10-24

- Range reviewed: `3688056ae8dc61f8f3eecfbb88b08b4ed7598f3c` → working tree (the fixes are uncommitted; there is no later fix commit). Scope: the four triaged fixes, their tests, and the added damaged-package contract. Original review and triage above remain historical.
- Reviewer: GPT-6 / Codex, OpenAI; follow-up to the Opus/Anthropic implementation session.
- Docs consulted: original findings and triage above; roadmap's four fix items; architecture runner recovery, load rejection, maintenance, and configuration sections, including the working-tree amendments; product model/configuration/distribution requirements; ADR 0002 and ADR 0010.
- Verification: Linux Debug build passed (no work needed). All six targeted CTests passed: model update, model list, runner recovery, language selection CLI, registry tiers, and config persistence. Diff whitespace check passed. Temporary fixtures reproduced #5 and #7 below; #6 follows directly from row construction and action predicates. Windows controls and real-model/GPU execution were not repeated. Only this review file was edited by this follow-up review.
- Original finding disposition: **#2 resolved** by clearing the combo inside its population helper; **#4 resolved** for named-folder repair and retaining unrecoverable journals, with regression coverage. **#1 substantially addressed** for isolated current-revision damage, including bundled report-only handling; new state edges remain in #6–#7. **#3 partially resolved**: transport/crash recovery and shared first-chunk retry budget are covered, but protocol-response validation still escapes recovery (#5).

| # | Finding | Evidence (file:line) | Severity | Risk | Value | Effort |
|---|---------|----------------------|----------|------|-------|--------|
| 5 | Startup protocol validation errors still bypass the promised one-restart policy: `prepare()` retries `RunnerLaunchError`, while initialize/load response validators throw the unrelated `RunnerProtocolError`, which is rethrown without retry. This is the remaining protocol case of original #3. See follow-up Details #5. | `src/recovering_runner.cpp:18`, `src/recovering_runner.cpp:64`, `src/recovering_runner.cpp:67`, `src/runner_protocol.cpp:144`, `include/tts_host/runner_protocol.hpp:14` | med | med | high | low |
| 6 | A damaged package at an older revision, or a damaged local Qwen import, has no Settings recovery action: it is neither installed nor updateable in the row, Download and Remove are disabled, and Verify is restricted to the current catalogue revision. See follow-up Details #6. | `src/model_list.cpp:56`, `src/model_list.cpp:64`, `src/settings_window.cpp:714`, `src/settings_window.cpp:715`, `src/settings_window.cpp:1347`, `src/settings_window.cpp:1364` | med | med | med | med |
| 7 | Damaged copies bypass registry duplicate/tier precedence and are selected by pathname order. When both bundled and per-user copies are damaged, Verify can choose the read-only bundled copy and refuse repair although a repairable per-user copy exists. See follow-up Details #7. | `src/model_registry.cpp:289`, `src/model_registry.cpp:410`, `src/model_registry.cpp:442`, `src/model_update.cpp:55`, `src/model_list.cpp:57` | med | med | med | med |

### Follow-up Details #5 — Include malformed startup replies in recovery

The new helper correctly handles process death, closed channels, and deadline failures represented as `RunnerLaunchError`. It does not handle semantic control-response errors from `parse_runner_initialize_response` or `parse_runner_load_response`. Those are `RunnerProtocolError`, a separate `std::runtime_error` subclass. They reach the catch-all at `prepare():67`, which drops the process and rethrows immediately. The roadmap fix explicitly includes protocol failures; ADR 0002 also puts protocol errors under one-restart recovery.

Reproduction used a temporary runner wrapper that answers its first initialize with a well-framed JSON-RPC result `{}` and delegates subsequent launches to the stub. CLI synthesis exits 1 with “runner initialize message is missing 'protocolVersion'” and never restarts. This is distinct from an explicit, non-retryable `RunnerLoadRejected`.

Normalize startup protocol-validation failures into the recoverable error path, retaining the rejection and cancellation exceptions. Add malformed initialize and load response cases; the added tests currently cover process death at those operations, not semantic response errors.

### Follow-up Details #6 — Give non-current damaged packages a recovery route

A damaged row sets only `damaged = true`; `installed` and `update_available` stay false. The Download predicate expressly excludes damaged rows. The maintenance menu computes `writable` only from `discovered_packages`, so Remove and Revert are disabled. Verify requires a compiled-catalogue match and the current revision. Consequently, a previously downloaded package damaged after an application/catalogue upgrade can neither update nor repair from Settings. An imported Qwen package with missing assets similarly cannot be removed there. Open folder is the only useful action, requiring manual filesystem intervention.

This also exposes a contract conflict: the new architecture text deliberately says damaged packages offer only Verify and Open folder, while the existing maintenance contract says older revisions take Update and user-added packages are removable. The new rule needs an explicit exception or a documented decision; do not silently override it in implementation.

Keep damaged packages unloadable, but provide an appropriate writable-package recovery action for non-current/local packages, respecting bundled read-only storage. Add row/action coverage for current, older, and local damaged packages; the added repair tests use only current-revision catalogue fixtures.

### Follow-up Details #7 — Resolve damaged-package identity before choosing a maintenance target

`scan_registry_directory` diverts damaged entries directly into `scan.damaged_packages`, before the writable and bundled vectors are deduplicated and merged. The resulting damaged vector is merely sorted by `(id, manifest_path)`. Both row construction and maintenance take the first matching entry. This makes maintenance selection depend on directory spelling rather than ADR 0010's catalogue-match/per-user precedence.

A temporary C++ harness linked against the existing model-update test objects installed the same current-revision Qwen fixture into `a-bundled/` and `z-user/`, then removed the reference clip from both. The scan retained two damaged entries; Verify selected `a-bundled/demo-qwen` with `bundled = true`, and Repair refused with “is bundled with the app; reinstall or upgrade the application to repair it.” The equally damaged writable copy was unreachable by ID. No repository source or fixture was modified for this reproduction.

Apply a defined precedence/duplicate policy to damaged maintenance candidates, or expose separate targets where required. Do not repair an arbitrary copy solely because its path sorts first. Add two-tier damaged/damaged and mixed-revision tests alongside the existing single-tier damage cases.

## Follow-up triage — 2026-10-24

Triaged by Opus 5.5, Anthropic. Each follow-up finding was checked against the
code at `86a7e43`, which commits the fixes the follow-up review read as
uncommitted. All three are confirmed. No implementation files were changed
during triage.

Must fix now (1): #5.

| # | Finding | Risk | Value | Effort | Priority | Reason |
|---|---------|------|-------|--------|----------|--------|
| 5 | Malformed initialize/load replies bypass one-restart recovery | med | high | low | must_fix_now | unfinished part of must-fix #3; breaks the ADR 0002 contract that protocol errors restart once |
| 6 | Damaged older-revision or local packages have no Settings recovery action | med | med | med | should_fix_before_phase_complete | real dead end after a catalogue upgrade, but needs a user decision first: it conflicts with the "Verify and Open folder only" rule this branch added |
| 7 | Damaged copies ignore tier precedence; Verify can pick the read-only bundled copy | low | med | med | backlog | risk lowered from med: needs both tiers damaged at once, and the failure is a clear refusal with no data loss |

### Follow-up validation notes

- #5: `prepare()` (`src/recovering_runner.cpp:56`) recovers only
  `RunnerLaunchError`; `RunnerProtocolError`
  (`include/tts_host/runner_protocol.hpp:14`) is a separate
  `std::runtime_error` subclass thrown by the initialize/load parsers
  (`src/runner_protocol.cpp:64`, `:72`), so it falls into the catch-all and is
  rethrown. ADR 0002 (line 61) puts protocol errors under one-restart recovery.
  The same gap exists in `synthesize()` (`src/recovering_runner.cpp:88`) for a
  malformed synthesize reply; it predates this branch, but the same contract
  covers it, so the fix should handle both call sites.
- #6: damaged rows set only `damaged` (`src/model_list.cpp:56`); Download
  excludes them (`src/settings_window.cpp:714`); `writable` needs an installed
  package (`:1347`); Verify needs the current revision (`:1364`). The
  architecture text (`docs/design/architecture.md:603`–`609`) says damaged
  packages offer only Verify and Open folder, while line 589 says per-user
  packages are removable. Which actions a damaged row offers is the user's
  decision, recorded through `/doc-update`.
- #7: damaged entries go straight to `scan.damaged_packages`
  (`src/model_registry.cpp:289`), skipping the tier merge (`:410`), and
  `maintained_package` (`src/model_update.cpp:55`) takes the first match by
  `(id, manifest_path)`. On Windows the bundled `Program Files` path sorts
  before the per-user `Users` path, so the bundled copy is the one chosen when
  both are damaged.

### Follow-up resolution

The user asked for all three findings to be fixed before merge, so #6 and #7
were done ahead of their triaged priority. #6 follows the action set proposed
at triage: Update for a damaged older revision, Remove… for a damaged per-user
copy, bundled read-only. The rule is recorded in the
[maintenance design](../design/architecture.md#model-removal-repair-and-maintenance-actions).
The #7 mixed-revision ordering has no test, because the test fixtures are not
compiled catalogue entries.
