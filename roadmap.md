# Roadmap

Open work only, in order. Requirements and architecture live under `docs/`;
completed slices are recorded in `progress.md` and git history.

Current state: the Windows desktop prototype and both branch reviews
([October 3](docs/reviews/code-review-2026-10-03-roadmap-slices-branch.md#triage--2026-10-03),
[October 14](docs/reviews/code-review-2026-10-14-checkpoint.md#triage--2026-10-14))
are complete and merged into `main` (PR #2). Windows acceptance is complete on user confirmation (October 16).

Native checks that require the user are collected for combined acceptance and
do not block independent slices; never mark them passed from a build alone.
Synthetic Copy adapters require technical compatibility evidence, not a separate
user approval. Platform ports are later work, Linux before macOS per
[ADR 0007](docs/adr/0007-native-ui-per-platform.md).

## Windows acceptance

- [x] Combined Windows desktop acceptance — closed on user confirmation;
  Ctrl+F8 retained and recorded UI findings accepted.
- [x] Windows start-at-login acceptance — closed on user confirmation; agent
  did not perform isolated-machine testing.
- [x] Audible boundary gap — measured with the user; no audible artifacts reported.

Evidence, measurements, and final closure are in the
[October 16 acceptance report](docs/reviews/windows-acceptance-2026-10-16.md).
Synthetic Copy adapter compatibility remains separate open work below.

## Architecture deepening

Branch `chore/improve-codebase-architecture`. Candidates and ranking are in the
[2026-10-25 architecture review](docs/reviews/architecture-review-2026-10-25.md).
Each refactor keeps behavior unchanged; the grilling slice before it fixes the
deepened module's interface and records it in `docs/design/`.

- [ ] **HITL — Grill model-action eligibility:** settle the shape of the actions
  a model list row carries and their target packages, using the review's
  questions; capture the result in the design's model maintenance section — see
  [candidate 1](docs/reviews/architecture-review-2026-10-25.md#1--model-action-eligibility-in-the-model-list-module)
  and [ADR 0010](docs/adr/0010-model-revisions-storage-and-release-layout.md).
- [ ] **AFK — Move model-action eligibility into the model list module:**
  Settings buttons and the More menu render the row's actions; the rules are
  covered by `model_list_tests` on Linux — see
  [candidate 1](docs/reviews/architecture-review-2026-10-25.md#1--model-action-eligibility-in-the-model-list-module)
  and the design captured by the slice above.
- [ ] **HITL — Grill the speech queue module:** settle its interface and the
  synthesis seam — see
  [candidate 2](docs/reviews/architecture-review-2026-10-25.md#2--speech-queue-module-out-of-maincpp).
- [ ] **AFK — Extract the speech queue module from `main.cpp`:** queue modes,
  warm-up cancellation, idle unload, and profile-change reload tested with the
  stub runner — see
  [candidate 2](docs/reviews/architecture-review-2026-10-25.md#2--speech-queue-module-out-of-maincpp).
- [ ] **HITL — Grill profile resolution:** decide whether to merge the selection
  types, how the eager pick joins the module, and whether candidate 4's profile reads fold in — see
  [candidate 3](docs/reviews/architecture-review-2026-10-25.md#3--one-profile-resolution-module)
  and [configuration](docs/design/architecture.md#configuration).
- [ ] **AFK — One profile resolution module:** language, profile, fallback, idle
  override, and eager pick behind one interface, unchanged
  [load rejection](docs/design/architecture.md#load-rejection) behavior — see
  [candidate 3](docs/reviews/architecture-review-2026-10-25.md#3--one-profile-resolution-module).
- [ ] **HITL — Decide typed config reads:** go, fold into profile resolution, or
  drop — see
  [candidate 4](docs/reviews/architecture-review-2026-10-25.md#4--typed-reads-over-the-config-document)
  and [ADR 0004](docs/adr/0004-json-canonical-config.md).

## Product

- [x] **AFK — Qwen end-to-end synthesis (evaluation):** local package discovery,
  reference voice, resident synthesis, cancellation and recovery verified on
  Windows and Linux with the provisional configuration — see
  [engine design](docs/design/architecture.md#engines-and-models) and
  [package design](docs/design/architecture.md#model-packages-and-discovery).
- [x] **HITL — Select the bundled Qwen package:** bake-off skipped; 1.7B Base
  Q4_K_M/Q8_0 on llama.cpp b11029 CUDA ships, selectable but not a default —
  see [ADR 0009](docs/adr/0009-qwen-release-selection-without-bake-off.md).
- Bundled two-model release, in order:
  - [x] **AFK — Qwen catalogue and local import:** the ADR 0009 package is in
    the pinned catalogue with a public-domain LJ Speech reference voice — see
    [catalogue design](docs/design/architecture.md#download-catalogue).
    Windows Settings download/import of the real package is collected for
    combined acceptance.
  - [x] **AFK — Qwen speech-speed control:** the `qwen3-gguf` runner applies
    the typed 0.5–2.0 multiplier with a streaming pitch-preserving time
    stretch — see
    [playback design](docs/design/architecture.md#playback-control-and-position).
    Listening check on Windows is collected for combined acceptance.
  - [x] **HITL — Decide model revision and replacement storage:** file
    fingerprint revisions, read-only bundled plus per-user storage,
    catalogue-match precedence, one previous version kept, per-runner folders,
    CUDA only — see
    [ADR 0010](docs/adr/0010-model-revisions-storage-and-release-layout.md).
  - [x] **AFK — Model revisions and two-tier discovery:** record the revision
    on catalogue install, scan bundled and per-user tiers with ADR 0010
    precedence, and show local/current/update-available/superseded in Settings
    — see
    [update design](docs/design/architecture.md#model-installation-and-updates)
    and [ADR 0010](docs/adr/0010-model-revisions-storage-and-release-layout.md).
  - [x] **AFK — Manual catalogue model update:** user-triggered staged
    replacement keeping one previous version with Revert/Delete, preserving the
    working package on failure and recovering interrupted commits; verify
    resident-model handling and stable profile references — see
    [update design](docs/design/architecture.md#model-installation-and-updates)
    and [ADR 0010](docs/adr/0010-model-revisions-storage-and-release-layout.md).
  - [x] **AFK — Two-model release package:** move runners into
    `runners/<engine>/` with their libraries beside them, removing the Qwen
    compiled-in runtime path and DLL search setup; package Kokoro, selected
    Qwen, all voice/tokenizer assets, runtimes, and licences; fail packaging if
    required files are absent and verify both models speak from a clean
    extracted artifact with networking disabled — see
    [distribution design](docs/design/architecture.md#distribution) and
    [ADR 0010](docs/adr/0010-model-revisions-storage-and-release-layout.md).
  - [x] **AFK — Offline Windows installer:** per-user, after the two-model
    artifact, install both models with no network or developer runtime;
    preserve model updates and added packages across application upgrades — see
    [distribution requirements](docs/requirements/product.md#distribution-and-usability)
    and [distribution design](docs/design/architecture.md#distribution).
    Bundled models stay read-only; additions live in per-user storage — see
    [ADR 0010](docs/adr/0010-model-revisions-storage-and-release-layout.md).
  - Fixes before merge, on `feature/bundled-models`, from the
    [bundled-models review triage](docs/reviews/code-review-2026-10-19-bundled-models.md#triage--2026-10-19):
    - [x] **AFK — Validate the restored package in interrupted-Revert
      recovery:** on startup recovery, validate the installed folder before
      finalizing; on failure put the working copy back and the previous version
      in its `.previous` slot, and report it; fault-injection tests after each
      of the three revert renames, including an invalid previous package — see
      [review #1](docs/reviews/code-review-2026-10-19-bundled-models.md#1--validate-restored-content-before-completing-recovery)
      and [update design](docs/design/architecture.md#model-installation-and-updates).
    - [x] **AFK — Installer honours an unticked startup box on upgrade:**
      remove the Run entry only when it starts this copy, sharing the
      uninstall ownership check; never touch `StartupApproved`; validate in
      isolation, not on the developer machine — see
      [review #4](docs/reviews/code-review-2026-10-19-bundled-models.md#4--apply-a-negative-installer-answer-to-an-owned-registration),
      [login startup design](docs/design/architecture.md#login-startup), and
      [ADR 0008](docs/adr/0008-os-owned-login-startup.md).
    - [x] **AFK — Correct stale architecture examples:** single-language Qwen
      manifest example declaring `mmproj`, `voice`, `voiceLicense`; replace the
      "release runner/runtime layout still to be decided" sentence with a
      pointer to the `runners/<engine>/` layout — see
      [review #5](docs/reviews/code-review-2026-10-19-bundled-models.md#triage--2026-10-19),
      [engine design](docs/design/architecture.md#engines-and-models), and
      [ADR 0010](docs/adr/0010-model-revisions-storage-and-release-layout.md).
  - [x] **HITL — Merge `feature/bundled-models`:** after the three fixes above
    pass build and tests, user approves the merge to `main`; the slices below
    continue on a new branch — no doc governs: merge of reviewed work.
  - [x] **AFK — Model removal, open folder, and maintenance menu:** Remove…
    with previous-version and profile handling, Open folder / Open models
    folder, and the More ▾ menu enabling only valid actions (Revert, Delete
    previous, Remove superseded move into it) — see
    [maintenance design](docs/design/architecture.md#model-removal-repair-and-maintenance-actions)
    and [ADR 0010](docs/adr/0010-model-revisions-storage-and-release-layout.md).
  - [x] **AFK — Verify and repair catalogue models:** hash against the
    pinned catalogue, report damage, re-download only damaged files, commit
    in place with recovery; bundled installed-mode packages report only — see
    [maintenance design](docs/design/architecture.md#model-removal-repair-and-maintenance-actions).
  - [x] **HITL — Decide how a runner reports an unusable load:** a `load`
    rejection reply; no restart, retry, or implicit fallback; last load
    result in Settings and the Activity log — see
    [ADR 0002](docs/adr/0002-runner-protocol.md) and
    [load rejection design](docs/design/architecture.md#load-rejection).
  - [x] **AFK — Report Qwen unavailable without a usable GPU:** closed on
    user decision (2026-10-23); the no-CUDA Windows check was waived. Runners reject
    `load` with a message (Qwen's GPU check, Kokoro's package errors, a stub
    option for the CTest); the Host skips restart/retry, applies a configured
    `fallbackProfile`, shows the last load result in Settings, and records every
    message in the Activity log; verify on a Windows machine without usable
    CUDA — see
    [load rejection design](docs/design/architecture.md#load-rejection),
    [ADR 0002](docs/adr/0002-runner-protocol.md),
    [ADR 0010](docs/adr/0010-model-revisions-storage-and-release-layout.md), and
    [review #2](docs/reviews/code-review-2026-10-19-bundled-models.md#2--surface-unavailable-hardware-through-the-host).
  - [x] **AFK — Reject stale staged release models:** before reusing the
    release stage, compare each package's revision and file sizes/SHA-256 with
    the compiled catalogue and restage stale content; the shipping check fails
    on a catalogue mismatch; cover preparing a second release from the previous
    release's stage — see
    [review #3](docs/reviews/code-review-2026-10-19-bundled-models.md#3--check-cached-release-models-against-the-current-catalogue)
    and [distribution design](docs/design/architecture.md#distribution).
  - [x] **HITL — Clean Windows two-model acceptance:** closed on user
    confirmation (2026-10-23); not performed by the agent. Verify offline install,
    both models speaking, model selection, local/catalogue additions, a
    manual update, removal, and verify/repair on supported hardware — see
    [distribution requirements](docs/requirements/product.md#distribution-and-usability).
  - [x] **AFK — Unified Settings model list:** desktop checks (layout,
    selection persistence, Ctrl+F8) passed on user confirmation 2026-10-24. One list of installed and
    not-installed catalogue packages with source/status/size/loaded columns
    and a details pane replacing the registry dump, reusing the More ▾ menu —
    see
    [maintenance design](docs/design/architecture.md#model-removal-repair-and-maintenance-actions).
    Selection follow-up: successful Load saves and immediately applies the
    speech default for the package's languages; verify Q8_0 remains selected
    through the next read and restart, then repeat with Kokoro — see
    [desktop design](docs/design/architecture.md#desktop-integration).
    Shortcut follow-up: verify Ctrl+F8 while Settings is open, after probing
    a binding, and after close; the registration now stays active except
    during the availability probe.
- Fixes before merge, on `feature/model-maintenance`, from the
  [model-maintenance review triage](docs/reviews/code-review-2026-10-24-model-maintenance.md#triage--2026-10-24):
  - [x] **AFK — Keep runner recovery for initial startup failures:** make
    `prepare()` take part in the one-restart policy for launch, protocol, and
    deadline failures while `RunnerLoadRejected` still skips retries and
    applies the fallback; keep one retry budget for the first unplayed chunk;
    test initialize and load transport failures apart from explicit
    rejection — see
    [review #3](docs/reviews/code-review-2026-10-24-model-maintenance.md#3--preserve-recovery-while-distinguishing-explicit-load-rejection),
    [ADR 0002](docs/adr/0002-runner-protocol.md), and
    [load rejection design](docs/design/architecture.md#load-rejection).
  - [x] **AFK — Reset the default-profile picker on Load:** clear the combo
    before refilling it after a successful Load; repeated Load → General
    shows one row per profile with the saved default selected. Windows
    desktop check passed on user confirmation 2026-10-24 — see
    [review #2](docs/reviews/code-review-2026-10-24-model-maintenance.md#2--reset-the-profile-picker-before-refilling-it).
  - [x] **AFK — Verify and repair damaged Qwen packages:** done ahead of the
    Qwen keep/drop decision at the user's request; keep damaged-package metadata for
    maintenance without making it loadable, preserving schema/containment and
    bundled report-only; cover missing reference, missing mmproj, and a bad
    GGUF header — see
    [review #1](docs/reviews/code-review-2026-10-24-model-maintenance.md#1--retain-enough-package-identity-to-verify-damaged-qwen-assets)
    and [maintenance design](docs/design/architecture.md#model-removal-repair-and-maintenance-actions).
  - [x] **AFK — Journal the installed folder for repair:** record the package
    folder in the repair journal, check it stays inside the registry
    directory, recover from it, and keep the journal when no recovery branch
    succeeds; test a folder named differently from its ID — see
    [review #4](docs/reviews/code-review-2026-10-24-model-maintenance.md#4--journal-the-actual-installed-folder).
  - [x] **AFK — Recover from malformed runner replies:** a host-side
    `RunnerProtocolError` from initialize, load, or synthesize restarts once
    like a launch failure; `RunnerLoadRejected` and cancellation stay
    non-retryable; tests cover malformed initialize, load, and synthesize
    replies — see
    [follow-up triage](docs/reviews/code-review-2026-10-24-model-maintenance.md#follow-up-triage--2026-10-24),
    [follow-up #5](docs/reviews/code-review-2026-10-24-model-maintenance.md#follow-up-details-5--include-malformed-startup-replies-in-recovery),
    and [ADR 0002](docs/adr/0002-runner-protocol.md).
  - [x] **AFK — Recovery actions for non-current damaged packages:** on the
    user's instruction to fix all follow-ups, a damaged older revision offers
    Update (the damaged copy is discarded, not kept for Revert) and a damaged
    per-user copy, local or catalogued, offers Remove…; bundled stays
    read-only; row tests cover current, older, and local damaged packages —
    see
    [follow-up #6](docs/reviews/code-review-2026-10-24-model-maintenance.md#follow-up-details-6--give-non-current-damaged-packages-a-recovery-route)
    and [maintenance design](docs/design/architecture.md#model-removal-repair-and-maintenance-actions).
  - [x] **AFK — Tier precedence for damaged copies:** damaged copies of an ID
    are ordered by catalogue-match, then per-user precedence, so maintenance
    acts on the winning copy; tested damaged in both tiers. The mixed-revision
    order is not tested, because the test fixtures are not in the compiled
    catalogue — see
    [follow-up #7](docs/reviews/code-review-2026-10-24-model-maintenance.md#follow-up-details-7--resolve-damaged-package-identity-before-choosing-a-maintenance-target)
    and [ADR 0010](docs/adr/0010-model-revisions-storage-and-release-layout.md).
- Qwen quality and model options, after the unified model list:
  - [x] **AFK — Higher-precision Qwen catalogue variants:** add the Q8_0 and
    bf16 packages with pinned URLs, sizes, and SHA-256; download and synthesize
    each on Windows and record RTF and peak VRAM if measurable — see
    [catalogue design](docs/design/architecture.md#download-catalogue) and
    [ADR 0009](docs/adr/0009-qwen-release-selection-without-bake-off.md).
  - [x] **HITL — Compare Qwen variants and reference voices with Kokoro:**
    closed 2026-10-24 with no change: bundled Q4_K_M, the catalogue variants and
    the LJ Speech voice stay; ADR 0009 stands. User verdict: every variant is
    poor quality and about 5× too slow, with waits between sentences; possibly
    worse than Chrome's built-in voice. Decided 2026-10-26: Qwen and all its
    catalogue variants stay — it is already built and no variant is yet known
    to be best; revisit removal only if a second quality option arrives. User listens to Q4_K_M/Q8_0/bf16 at speed 1.0 with the LJ Speech clip and
    at least one cleaner, licensed reference clip the user approves, against
    Kokoro; decide whether bundled Qwen stays Q4_K_M, changes, or becomes
    catalogue-only, and whether the reference voice changes (a superseding
    ADR if so) — see
    [ADR 0009](docs/adr/0009-qwen-release-selection-without-bake-off.md) and
    [default-model criteria](docs/requirements/product.md#acceptance-criteria-for-default-model-selection).
  - [x] **AFK — Research Qwen CustomVoice and VoiceDesign via llama.cpp:**
    [research note](docs/research/2026-10-23-qwen-customvoice-voicedesign-llamacpp.md)
    (2026-10-23), captured in the engine design. Question was
    whether GGUF conversions of the 1.7B CustomVoice (named speakers, no
    reference clip) and VoiceDesign models exist or can be produced, and
    whether pinned or newer llama.cpp runs them; PyTorch stays rejected per
    [ADR 0009](docs/adr/0009-qwen-release-selection-without-bake-off.md) —
    no doc governs: the output is a research note for `/planning-capture`.
  - [ ] **HITL — Decide the Qwen named-speaker and voice-design path:**
    choose between waiting for upstream llama.cpp named-speaker and
    instruction input (then a pin bump), patching our llama.cpp build, or adding
    codec.cpp as a separate runner. The last two need an ADR. Also decide
    whether VoiceDesign is in scope or only CustomVoice's fixed speakers. A
    chosen runtime gets a Windows CUDA measurement slice (RTF, first chunk,
    VRAM) before any catalogue entry — see
    [engine design](docs/design/architecture.md#engines-and-models) and
    [ADR 0009](docs/adr/0009-qwen-release-selection-without-bake-off.md).
  - [ ] **HITL — Plan per-model load and synthesis options in Settings:**
    decide which typed options users may set per profile and how Settings
    shows them. Candidates for Qwen: sampling temperature/top-k/top-p/seed
    (currently fixed at 0.9/50/1.0/42 in the runner, values not recorded in
    `stats` — [Qwen review finding 6](docs/reviews/code-review-2026-10-16-qwen-evaluation.md#triage--2026-10-16)),
    context size, KV-cache type, GPU layer count, reference voice; for Kokoro:
    voice, CPU thread count. Language is a package property (one per Qwen
    package) and does not reduce memory: the weights are multilingual, so the
    memory lever is quantization (the package choice). Load options need a
    reload; synthesis options can travel per request. Typed fields only, no
    generic options map — see
    [playback design](docs/design/architecture.md#playback-control-and-position),
    [configuration design](docs/design/architecture.md#configuration),
    [ADR 0002](docs/adr/0002-runner-protocol.md), and
    [ADR 0010](docs/adr/0010-model-revisions-storage-and-release-layout.md)
    (partial CPU offload must not become the rejected CPU fallback).
- [ ] **AFK — Synthetic Copy compatibility verification:** validate
  focused-control identification, modifier handling, and clipboard
  ownership/preservation for each adapter before enabling it. Unknown targets
  stay manual-copy-only — see
  [design](docs/design/architecture.md#desktop-integration).
- [ ] **HITL — Plan played-text history and visible queue:** user idea
  (2026-10-14): list played/playing reads, step back/forward and replay a
  chosen one, and show the queue. Needs `/planning-capture` first; a history
  holds spoken text, which the Activity record deliberately never does.
  Queued requests already carry a source label (`SpeechRequest` in
  `src/main.cpp`).
- [ ] **HITL — Russian and Armenian acceptance:** compare Qwen Russian and the
  offline Armenian candidate with native-speaker samples; confirm best-effort
  quality is adequate and that English defaults are unaffected.

## Cross-platform releases

Native Linux and Apple Silicon macOS artifacts plus a Windows installer, using
the same configuration, manifests, API, and conformance tests. Findings and a
proposed sequence are in the
[release packaging audit](docs/reviews/2026-08-31-release-packaging-audit.md);
the runner layout is decided by
[ADR 0010](docs/adr/0010-model-revisions-storage-and-release-layout.md)
(per-runner folders, not a shared `runtimes/`).

- [ ] **AFK — Native Linux selection adapter:** KDE Plasma Wayland direct
  accessibility capture and actionable unavailable-capability reporting; use
  Kubuntu for initial desktop acceptance and retain Rocky 8/9 KDE as
  compatibility targets. Never assume Windows active-window or injection
  behavior — see [design](docs/design/architecture.md#desktop-integration)
  and [design](docs/design/architecture.md#platform-strategy).
- [ ] **HITL — Linux KDE Wayland acceptance:** focused/empty/protected
  selections and unsupported applications on Kubuntu; compatibility on the
  user's Rocky 8/9 KDE sessions — see
  [design](docs/design/architecture.md#platform-strategy).
- [ ] **AFK — Linux X11 selection compatibility:** native X11-session capture
  after the KDE Wayland adapter; fail closed for unavailable targets — see
  [design](docs/design/architecture.md#platform-strategy).
- [ ] **AFK — Native macOS selection adapter:** macOS accessibility
  permission/status and focused-selection capture; no synthetic Windows-style
  copy fallback — see [design](docs/design/architecture.md#desktop-integration)
  and [design](docs/design/architecture.md#platform-strategy).
- [ ] **AFK — Linux and macOS login startup:** XDG Autostart (Background
  portal under Flatpak) and `SMAppService.mainApp`, with the same Settings
  states as Windows — see [design](docs/design/architecture.md#login-startup).
- Windows installer and release-layout work is tracked under the bundled
  two-model release above; do not duplicate its implementation slices.

## Open decisions

- Whether and when Qwen becomes the quality-profile default: it misses the
  RTF ≤ 1.0 hard limit (≈1.5 warm on Linux) and peak VRAM is unmeasured; the
  user may measure it against, or change, the
  [default-model criteria](docs/requirements/product.md#acceptance-criteria-for-default-model-selection)
  — see [ADR 0009](docs/adr/0009-qwen-release-selection-without-bake-off.md).
- Whether a WSL headless distribution is published, or `--headless` merely makes
  it possible.
- Whether played-text history may retain spoken text.

## Deliberately deferred

- CI pipeline (GitHub Actions or equivalent) running the CTest suite,
  including the stub end-to-end test. Local CTest already covers this; revisit
  once the application works end-to-end with real inference and is worth
  protecting from regressions.
- Remote and cloud providers. Revisit only if local quality proves inadequate.
- Full text normalization (numbers, dates, currency, abbreviations).
- Token authentication and LAN serving. The config vocabulary is reserved;
  see `docs/adr/0003-no-authentication.md`.
- Self-updating.
- Spool recovery (October 3 finding 14): rejected without a use case — see
  [validation](docs/reviews/code-review-2026-10-03-roadmap-slices-branch.md#finding-14).
