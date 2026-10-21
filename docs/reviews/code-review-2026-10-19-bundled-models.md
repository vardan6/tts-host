# Code review — bundled models (2026-10-19)

- Range reviewed: `5e034555cc1e101814a6f891f84a3ec7eaebd5d3..2693e5e7b6d4c9d710f8c92d0b534e79a6de4452` (`main..feature/bundled-models`), emphasizing changes after `5af176b` and checking the earlier review resolutions.
- Reviewer: GPT-6 / Codex, OpenAI. Implementer: Opus 5.5, Anthropic, confirmed by the user; cross-provider review.
- Docs consulted: [roadmap](../../roadmap.md), [active context](../../activeContext.md), [architecture](../design/architecture.md) (packages/discovery, catalogue, installation/updates, engines, playback, distribution, login startup), [product requirements](../requirements/product.md#distribution-and-usability), ADRs [0002](../adr/0002-runner-protocol.md), [0007](../adr/0007-native-ui-per-platform.md), [0008](../adr/0008-os-owned-login-startup.md), [0009](../adr/0009-qwen-release-selection-without-bake-off.md), [0010](../adr/0010-model-revisions-storage-and-release-layout.md), and the [previous Qwen review](code-review-2026-10-16-qwen-evaluation.md).
- Verification: Linux Debug build passed. CTest: 50/51 passed initially; the Unicode CLI test passed on rerun after linking finished (the first invocation overlapped the build and copied an executable being linked). All 51 tests therefore passed across these runs. A temporary-directory interrupted-revert reproduction confirmed #1. Windows installer, clean offline installation, visual Settings acceptance, and real GPU synthesis were not executed. Implementation files were not changed.

| # | Finding | Evidence (file:line) | Severity | Risk | Value | Effort |
|---|---------|----------------------|----------|------|-------|--------|
| 1 | Interrupted Revert can declare success with an invalid restored package and leave the working copy inaccessible to Revert. Recovery checks only that the installed folder exists, bypassing the validation/rollback used during a normal revert. See Details #1. | `src/model_update.cpp:186`, `src/model_update.cpp:192`, `src/model_update.cpp:273`, `src/model_update.cpp:155` | med | med | high | med |
| 2 | Missing CUDA/GPU support does not produce the promised unavailable status and actionable reason in Settings. The runner throws the useful reason, writes it only to stderr, and exits; the GUI receives a closed-pipe load failure. The model list still presents the package as current. See Details #2. | `src/qwen_api.hpp:105`, `src/qwen_runner_main.cpp:244`, `src/qwen_runner_main.cpp:263`, `src/runner_launcher.cpp:43`, `src/settings_window.cpp:670`, `src/settings_window.cpp:704` | med | med | high | med |
| 3 | A release build reuses any staged package containing model.json, even after the compiled catalogue changes. Packaging checks only that a revision exists, so an obsolete cached package can ship successfully instead of the release's pinned revision. See Details #3. | `scripts/release-win.ps1:50`, `scripts/release-win.ps1:51`, `CMakeLists.txt:547`, `CMakeLists.txt:548` | med | med | high | med |
| 4 | Clearing the installer's startup checkbox on an upgrade leaves an existing Run entry enabled. The selected task writes the entry; the unselected path only records declined and never removes the registration owned by this installation. See Details #4. | `installer/tts-host.iss:80`, `installer/tts-host.iss:98`, `installer/tts-host.iss:100`, `installer/tts-host.iss:110` | med | low | med | low |
| 5 | Align the remaining architecture examples with the accepted release contracts: the illustrative Qwen manifest declares two languages and lacks required mmproj/reference assets, and the engine section still says runtime layout is undecided. | `docs/design/architecture.md:402`, `docs/design/architecture.md:403`, `docs/design/architecture.md:631`, `src/model_registry.cpp:139`, `src/model_registry.cpp:141` | enhancement | low | med | low |

## Details

### #1 — Validate restored content before completing recovery

A previous package is scanned by identity only, intentionally skipping schema and asset validation. Suppose it has an unsupported schema version. Normal Revert moves it into place, validates it, and restores the working package on failure. A crash between the rename at line 186 and validation at line 192 instead leaves the invalid package installed and the working package parked. Startup treats mere existence of the invalid folder as a completed revert and archives the working package. The invalid package disappears from discovered_packages, so another Revert fails with “not installed.” This contradicts the requirement that an interrupted replacement preserve a working package.

Reproduction used only a temporary directory: create a schemaVersion 2 installed demo-review package, a valid schemaVersion 1 working package in `.previous/.demo-review.reverting`, and invoke the host with `--headless --config <temporary config> --data-dir <temporary directory> --list-models`. It exited successfully with “Completed the interrupted revert,” zero discovered packages, one unsupported package, and the working package under `.previous/demo-review@<revision>`.

Validate the restored folder before finalizing recovery, restore the parked working copy if validation fails, and preserve enough transaction information to retain the rejected previous version safely. Add fault-injection coverage after each revert rename, including invalid previous content; the existing interrupted-revert test covers only the state before restoration.

### #2 — Surface unavailable hardware through the host

ADR 0010 and the distribution design explicitly require Qwen to be listed unavailable with an actionable reason when no usable NVIDIA GPU exists. Registry/UI status currently covers revision state only. The GPU check happens during load, and its exception reaches the runner's outer stderr handler. Windows GUI launches can supply NUL as stderr, so “requires a working CUDA runtime/GPU; CPU fallback is disabled” never reaches the activity pane. Selecting Qwen for speech also spends the ordinary restart/retry on the same deterministic failure.

Expose a capability/load failure to the host with its reason, then retain/display that unavailable state; keep Kokoro selectable. Verify on an isolated Windows machine without usable CUDA. This finding concerns the newly promised release behavior, rather than reopening the earlier synthesize-request error finding.

### #3 — Check cached release models against the current catalogue

The stage cache is deliberately persistent. After a catalogue weight/checksum changes, its existing model.json causes the stage loop to skip the package entirely. The install validator accepts any revision member and existing declared files; it does not compare the revision to the current compiled catalogue or verify the pinned file hashes. Extracted synthesis can still pass because the older package speaks. A stale or modified package therefore need not cause release packaging to fail.

Before reuse, compare the stage's revision with the compiled catalogue and verify file sizes/checksums, refreshing stale content through staging. Make the shipping validation reject a catalogue mismatch. Cover the case of preparing a second release using the previous release's stage directory. This is a release-input validation gap, separate from the deferred end-user Verify/Repair slice.

### #4 — Apply a negative installer answer to an owned registration

Reinstall/upgrade an installation that already owns the TTS Host Run value, and clear “Start TTS Host when I sign in to Windows.” The registry entry is skipped because its task is unselected, while CurStepChanged writes the declined marker. No install-time path deletes the existing value, so startup remains enabled despite the explicit answer. The host reads OS state and does not reconcile the marker into a negative answer later.

On an explicit negative installer answer, remove an entry owned by this installation using the ownership check already used on uninstall. Preserve entries belonging to other copies and the ADR 0008 rule against writing StartupApproved. Validate in isolation; do not change this developer machine's login startup.

## Triage — 2026-10-19

Triaged by Opus 5.5, Anthropic. Every finding was checked against the code at
`2693e5e`; all five are confirmed. No implementation files were changed during
triage.

Must fix now (2): #1, #4.

| # | Finding | Risk | Value | Effort | Priority | Reason |
|---|---------|------|-------|--------|----------|--------|
| 1 | Interrupted Revert can report success with an invalid package installed and the working copy parked | med | high | med | must_fix_now | breaks the "an interrupted replacement keeps a working package" contract; new on this branch |
| 4 | Clearing the startup checkbox on an upgrade leaves the existing Run entry | low | med | low | must_fix_now | ignores an explicit "no" to login startup; small fix reusing the uninstall ownership check |
| 2 | Without a usable GPU, Qwen is not shown unavailable with a reason | med | high | med | should_fix_before_phase_complete | promised behaviour (ADR 0010) never built, not a regression; must land before clean Windows acceptance |
| 3 | Release build reuses any staged `model.json` after the catalogue changes | med | high | med | should_fix_before_phase_complete | affects only developer release builds; must land before the next release |
| 5 | Architecture Qwen manifest example and "runtime layout undecided" line are stale | low | med | low | should_fix_before_phase_complete | docs only; done through `/doc-update` with the #1/#4 fixes |

### Validation notes

- #1: `recover_interrupted_model_updates` (`src/model_update.cpp:273`) treats
  `std::filesystem::exists(installed)` as a finished revert, while the normal
  path validates at line 192 and rolls back on failure. The parked working
  copy is not deleted — it is renamed to `.previous/<id>@<revision>` — but
  the invalid installed package drops out of `discovered_packages`, so Revert
  reports "not installed" and the working copy is unreachable from the UI. The
  window is narrow (a crash between lines 186 and 192, with a previous version
  that no longer validates, e.g. after a schema change), but the outcome
  contradicts the recovery contract and is cheap to close.
- #2: confirmed the GPU check throws in `QwenApi`'s constructor
  (`src/qwen_api.hpp:105`) during `load`, and the outer handler
  (`src/qwen_runner_main.cpp:263`) writes only to stderr, which
  `make_inheritable_child_handle` (`src/runner_launcher.cpp:43`) sets to NUL
  for GUI launches. ADR 0010:45 requires "Qwen is listed as unavailable with an
  actionable reason and Kokoro still works." Settings shows only revision status
  (`src/settings_window.cpp:670`).
- #3: confirmed `scripts/release-win.ps1:50` skips staging when `model.json`
  exists, and the CMake install check (`CMakeLists.txt:547`) only asserts that
  a `revision` member exists and declared files are present.
- #4: confirmed the `[Registry]` entry is gated on `Tasks: startup`
  (`installer/tts-host.iss:80`) and `CurStepChanged` only writes the marker; no
  install-time path deletes the value.
- #5: confirmed `docs/design/architecture.md:402` declares `["en", "ru"]` and
  only `model`, which `src/model_registry.cpp:139`–`141` rejects (exactly one
  language; `model`, `mmproj`, `voice`, `voiceLicense` required); line 631
  still calls the release runner/runtime layout undecided, though ADR 0010 and
  the two-model release slice settled it on `runners/<engine>/`.

### Agreed fixes

- #1: in recovery, run `validate_model_package` on the installed folder before
  finalizing. On failure, move it back to its `.previous/<id>@<revision>` slot
  and the parked copy back to `<id>`, and report "Restored '<id>' after an
  interrupted revert; the previous version does not validate." Add
  fault-injection coverage for a crash after each of the three revert renames,
  including an invalid previous package (the current test covers only the state
  before restoration).
- #4: in `CurStepChanged(ssPostInstall)`, when the `startup` task is not
  selected, delete the Run value only if it starts this copy — the same
  `Pos(... '{app}\tts-host.exe' ...) = 1` check as `CurUninstallStepChanged`;
  factor it into a shared helper. Do not touch other copies' entries or
  `StartupApproved` (ADR 0008). Validate in an isolated Windows environment,
  not on the developer machine's login startup.
- #2 (roadmap slice "Report Qwen unavailable without a usable GPU"): reuse the
  rejection-reply mechanism from the
  [previous review's #1/#2](code-review-2026-10-16-qwen-evaluation.md#triage--2026-10-16)
  for `load`. The runner answers the load with a JSON-RPC `error` carrying the
  reason instead of exiting, the host raises a request error that
  `RecoveringRunner` does not retry, and Settings records and shows Qwen as
  unavailable with that reason while Kokoro stays selectable. Verify on a
  Windows machine without usable CUDA.
- #3 (roadmap slice "Reject stale staged release models"): before reusing a
  staged package, compare its revision and file sizes/SHA-256 with the
  compiled catalogue (e.g. a headless host verification entry point) and
  restage on mismatch; make the shipping check fail on a catalogue mismatch.
  Cover preparing a second release from the previous release's stage. Keep it
  separate from the end-user Verify and repair slice, though both can share
  the hashing code.
- #5: fix through `/doc-update` with the #1/#4 fixes. Replace the example with
  a single-language manifest that declares `mmproj`, `voice`, and
  `voiceLicense`, and replace the "still to be decided" sentence with a link
  to the `runners/<engine>/` layout in [Distribution](../design/architecture.md#distribution)
  and ADR 0010.

### Routing

Planned as `roadmap.md` slices:

- Before merge, on `feature/bundled-models`: #1, #4, and #5 as three AFK
  slices, then "HITL — Merge `feature/bundled-models`".
- After merge: #2 as "HITL — Decide how a runner reports an unusable load"
  (ADR 0002 says any non-`synthesize` failure exits, so the proposed `load`
  rejection reply amends it), then "AFK — Report Qwen unavailable without a
  usable GPU"; #3 as "AFK — Reject stale staged release models". Both come
  before clean Windows acceptance.
- The intended behaviour for #3 and #4 is written into
  [Distribution](../design/architecture.md#distribution) and
  [Login startup](../design/architecture.md#login-startup).

Recommended immediate action: fix #1 and #4 on `feature/bundled-models`, run
`/doc-update` for #5, then `/session-close` (STEP mode) and merge.
Docs impact: `docs/design/architecture.md` (#5); `roadmap.md` (done). #2 and
#3 restore documented behaviour and need no doc change.
