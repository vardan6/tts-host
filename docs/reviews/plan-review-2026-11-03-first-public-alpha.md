# Plan review — first public Windows alpha

- Date: 2026-11-03.
- Reviewer: Codex (GPT-6), OpenAI.
- Review mode: **same-provider review (confirmed)**. Planner provider was not established; after the cross-provider warning, the developer directed this session to review all the work.
- Scope: today's uncommitted changes to `roadmap.md`, `docs/requirements/product.md`, `docs/design/architecture.md`, ADR 0010, new ADR 0011, and `README.md`; `activeContext.md` and `progress.md` for scope/history; ADR 0009 and existing configuration, packaging, model-list, runner-resolution, and test code where the plan depends on them.
- Baseline: `main`, `origin/main`, and `release-prep/v0.1.0-alpha.1` all resolve to `b4ce040`. The release-prep branch has no additional commits. Today's work is uncommitted planning, not release implementation. Review performed on `main`; plan documents and user configuration left unchanged.

| # | Finding | Why it matters | Severity | Risk | Value | Effort |
|---|---------|----------------|----------|------|-------|--------|
| 1 | The prescribed dry run prevents the following real run: the release procedure requires `main` to match `origin`, then creates a local version commit, while `-DryRun` stops only before pushing. The roadmap explicitly says to run dry, then for real. See Details #1. | The rehearsal leaves a state rejected by the next invocation; tagging during the rehearsal can also collide with the real tag. | high | high | high | med |
| 2 | The standard-only alpha goal conflicts with mandatory two-variant build and verification instructions. The distribution section still requires both packages and the CUDA runtime even for shipping packaging. See Details #2. | An unavailable NVIDIA build/GPU can block the supposedly independent standard alpha; future implementers receive conflicting release gates. | med | med | high | low |
| 3 | The plan retains a shipped Qwen default contrary to ADR 0009 and the distribution requirements. The only planned configuration change is `capturePolicy`; the committed English default is `quality`, which names Qwen, and the architecture still specifies quality-first routing. See Details #3. | A clean NVIDIA release selects Qwen automatically despite the explicit decision that it remains selectable only. The developer's uncommitted Kokoro selection masks this problem. | med | med | high | low |
| 4 | The newly deferred release-notification link remains mandatory in both requirements and design. See Details #4. | The roadmap declares it out of scope without recording an alpha exception or changing the standing product decision. | med | low | med | low |
| 5 | The first alpha slice identifies a Linux-only tray test as a known Windows release-gate failure. See Details #5. | The next implementation slice is routed toward a test that Windows CTest never registers, obscuring which failures actually block the Windows gate. | low | low | med | low |
| 6 | **Enhancement:** add explicit acceptance scenarios for switching between variants and for runner-absent model rows, beyond testing a fresh draft on another PC. See Details #6. | The new variant behavior crosses installer replacement, preserved user data, and shared model-action eligibility; a fresh standard install alone cannot exercise those interactions. | enhancement | med | high | med |

## Details

### 1 — Define how rehearsal hands off to publication

Evidence:

- [Release procedure](../design/architecture.md#versions-and-releases), lines 1144–1153: refuse unless the tree is clean and `main` matches `origin`; write and commit the version; build/test; tag and push; `-DryRun` stops before pushing.
- [Alpha execution slice](../../roadmap.md#first-public-alpha-windows), lines 118–120: run the script with `-DryRun`, then for real.

After the first dry run, `main` is ahead of `origin/main` by the version commit. The next invocation fails its first condition. If the version was already set, a mandatory new version commit also needs defined no-change behavior. Because tagging precedes pushing in step 6, stopping merely "before pushing" does not establish whether a dry run creates a local tag.

Choose and capture one consistent contract: for example, build the rehearsal in a temporary checkout without changing the caller's refs, or explicitly support resuming publication of the already prepared, verified commit. Specify whether dry runs create tags and how failed builds and retries reuse the prepared version. Preserve the invariant that the eventual tag identifies the exact source used for its artifacts; do not solve this by silently pushing an unverified commit.

### 2 — Make NVIDIA optional throughout the alpha path

Evidence:

- [Alpha goal](../../roadmap.md#first-public-alpha-windows), lines 79–81: only standard is required; NVIDIA ships if ready.
- The same section, lines 106–109: `scripts/release-win.ps1` "builds both variants".
- [Distribution design](../design/architecture.md#distribution), lines 1061–1069: `TTS_HOST_RELEASE=ON` needs the pinned llama.cpp runtime and both staged packages, and the script has both models synthesize.
- [Release procedure](../design/architecture.md#versions-and-releases), line 1147: build "the variants" and run the extracted-copy speech check, without an optional-variant rule.
- [ADR 0011](../adr/0011-standard-and-nvidia-release-variants.md#decision), lines 10–18: releases ship two variants, without mentioning the alpha exception.

The existing implementation really has the dependency the old prose describes: `CMakeLists.txt:442–455` requires the Qwen runtime for shipping configuration, and `scripts/release-win.ps1:29–39`, `:51`, and `:84` fetch/configure Qwen and iterate both model IDs. Their replacement is planned; these are dependency evidence, not defects claimed against unfinished code.

State the alpha exception in the governing design/ADR and define how the scripts select the variants being released. Standard staging, packaging, and speech verification must succeed independently of Qwen assets and a usable NVIDIA GPU. Every selected variant should retain its complete-file and speech gates. An explicitly omitted NVIDIA installer should not be treated as a failed standard release.

### 3 — Reconcile release defaults with the accepted model decision

Evidence:

- [ADR 0009](../adr/0009-qwen-release-selection-without-bake-off.md#decision), lines 21–25: Kokoro stays default until Qwen meets the criteria or the user explicitly changes them.
- [Distribution requirements](../requirements/product.md#distribution-and-usability), lines 240–243: the bundled Qwen package becomes a fast/quality profile default only by meeting the default-model criteria.
- [Configuration design](../design/architecture.md#configuration), lines 371–395: `quality.model` is Qwen, `languageDefaults.en` is `quality`, and initial English routing is explicitly quality first.
- Committed `config.example.json`, inspected with `git show HEAD:config.example.json`: `languageDefaults.en = quality`; `profiles.quality.model = qwen3-tts-1.7b`.
- [Configuration slice](../../roadmap.md#first-public-alpha-windows), lines 92–96: change only `selection.capturePolicy`; leave the user's other edits uncommitted.
- `CMakeLists.txt:504` installs that example as the release `config.json`; `src/model_session.cpp:24–47` resolves the chosen model and its runner. On a supported NVIDIA installation, the Qwen choice is available rather than triggering the Kokoro fallback.

The working copy currently routes English through a user-created `selected-model-en` profile naming Kokoro. That personal choice is outside the release change and must not be mistaken for the committed shipping default. The clean-tree release precondition makes the distinction particularly relevant.

Add an explicit shipping-default change and align the configuration design with ADR 0009, or obtain and capture a deliberate change to that ADR/criteria. Preserve the developer's private configuration edits independently. Check the release configuration from a clean source tree rather than inferring defaults from the running developer installation.

### 4 — Capture the release-notification scope exception

Evidence:

- [Deferred work](../../roadmap.md#deliberately-deferred), line 437, newly adds the in-app "newer release available" link.
- [Distribution requirements](../requirements/product.md#distribution-and-usability), lines 277–278: the application checks for a newer release and links to it.
- [Distribution design](../design/architecture.md#distribution), lines 1094–1096: the same behavior is required there.

Deferring this feature may be appropriate for alpha, but the roadmap alone does not amend its product requirement. Capture a clearly scoped alpha exception in the canonical requirement and reflect that scope in design, or retain an implementation slice. Keep manual replacement distinct from release notification: the existing self-updating deferral does not cover the notification requirement.

### 5 — Identify test failures by platform and actual test name

Evidence:

- [First alpha slice](../../roadmap.md#first-public-alpha-windows), lines 87–91: "Make the Windows test suite pass" lists the fixed-port tray test among known failures.
- `CMakeLists.txt:1435–1444` registers `tts_host_tray_not_implemented_on_this_platform` only inside `if(NOT WIN32)`.
- `tests/assert_language_selection.cmake:123–126` does search the entire rejected-load output for `fallback`, supporting the other stated test-maintenance issue.

The tray collision belongs to non-Windows verification, not the Windows release gate. Name the affected test and platform separately and base Windows closure on a fresh Windows CTest result from the intended clean release source.

The existing Linux persistence executable was also checked narrowly: it fails with `named profile was changed` against the current user-edited example and passes against a temporary copy of the committed example. This confirms the documented input distinction; it is not evidence that the current Windows suite was rerun, nor a reason to weaken the gate. No rebuild or broad suite was run for this plan review.

### 6 — Exercise variant transitions, not only fresh installation

Evidence and motivation:

- [ADR 0011](../adr/0011-standard-and-nvidia-release-variants.md#decision), lines 20–23: either installer upgrades the other in place, per-user data survives, and absent runners get an actionable Settings state.
- [Alpha execution slice](../../roadmap.md#first-public-alpha-windows), lines 118–120: only a generic draft test on another PC is explicit.
- Existing `installer/tts-host.iss:57–60` deletes the application's runner/model directories on upgrade; `:67` preserves an existing per-user configuration.
- Existing `src/model_list.cpp:52–55` derives Load/Download/Update eligibility from package/catalogue presence, without runner availability; `src/model_session.cpp:34–37` detects an absent runner only during resolution. The new state therefore changes an interaction across existing boundaries.

Consider adding these acceptance cases to the variant slice and its collected Windows check:

- Fresh standard installation: Kokoro works offline; every Qwen catalogue entry whose engine is absent shows the NVIDIA guidance and cannot download.
- Standard → NVIDIA: existing configuration and downloaded models survive; Qwen is selectable without an install-time download.
- NVIDIA → standard with a selected or downloaded Qwen model: Qwen/CUDA application files are removed, per-user data survives, the absent-runner explanation remains actionable, and Kokoro can still be selected and used.
- Distinguish an absent runner from an installed runner whose load is rejected for unsupported hardware. Keep the shared model-list eligibility module responsible for the new UI rules.

Use automated model-list/packaging checks where practical and an isolated Windows machine for installer transitions, respecting `PROJECT.md`'s restriction on changing this developer machine's login startup state.

## Triage — 2026-11-03

Triaged by the original planner (Claude Opus 5.5, Anthropic). All six findings
were validated against the plan docs and, where they concern existing
behavior, against the cited code. All are accepted; none is invalid.

| # | Finding | Risk | Value | Effort | Priority | Reason |
|---|---------|------|-------|--------|----------|--------|
| 1 | Dry run leaves state the real run rejects | high | high | med | must_fix_now | the prescribed alpha procedure cannot complete as written |
| 2 | Standard-only alpha conflicts with two-variant build gates | med | high | low | must_fix_now | an absent NVIDIA build or GPU would block the standard alpha |
| 3 | Shipped English default is Qwen, contrary to ADR 0009 | med | high | low | must_fix_now | a clean NVIDIA install would contradict an accepted ADR |
| 4 | Deferred release-notification link still required | low | med | low | should_fix_before_phase_complete | roadmap-only deferral of a standing requirement; doc fix only |
| 5 | Tray port test is Linux-only, not a Windows gate failure | low | med | low | should_fix_before_phase_complete | misroutes the first slice; no behavior risk |
| 6 | Acceptance lacks variant-transition and runner-absent cases | med | high | med | should_fix_before_phase_complete | runner-absent row is alpha scope; transitions need an NVIDIA installer |

| # | Recommendation | Landed in |
|---|----------------|-----------|
| 1a | One consistent rehearsal contract (temporary checkout, caller refs untouched) | `design/architecture.md` §Versions and releases, steps 2 and 6; roadmap "Release script" |
| 1b | Defined behavior when the version is already written | §Versions and releases, step 2 (no empty commit) |
| 1c | State whether dry runs create tags | §Versions and releases, steps 6–7 and closing paragraph (no tag before step 7) |
| 1d | How failed builds and retries reuse the prepared version | §Versions and releases, closing paragraph (rerun from scratch; a step 8 failure prints the `gh` commands) |
| 1e | Tag names the built source; never push an unverified commit | §Versions and releases, step 7 (atomic push) and closing paragraph |
| 2a | State the alpha exception in the governing design/ADR | ADR 0011 §Decision; §Versions and releases (`-Variants`) |
| 2b | Define how scripts select the variants released | §Distribution (`TTS_HOST_RELEASE_VARIANT`, `release-win.ps1 -Variant`); §Versions and releases (`-Variants`); roadmap variant and HITL release items |
| 2c | Standard staging, packaging, speech check independent of Qwen and GPU | §Distribution; ADR 0011 §Decision; roadmap "Standard (Kokoro-only) release variant" |
| 2d | Every selected variant keeps complete-file and speech gates | ADR 0011 §Decision; §Distribution |
| 2e | An omitted NVIDIA installer is not a failed release | §Versions and releases (`-Variants`) |
| 3a | Explicit shipping-default change | roadmap "Ship the release defaults" (`languageDefaults.en` = `fast`) |
| 3b | Align the configuration design with ADR 0009 | `design/architecture.md` §Configuration (example and English policy) |
| 3c | Preserve the developer's private configuration edits | roadmap "Ship the release defaults" (commit two lines against the committed file) |
| 3d | Check release configuration from a clean source tree | roadmap "Ship the release defaults" |
| 4a | Scoped alpha exception in the canonical requirement | `requirements/product.md` §Distribution and usability |
| 4b | Reflect that scope in design | §Distribution (manual-update paragraph); roadmap §Deliberately deferred |
| 4c | Keep manual replacement distinct from notification | `requirements/product.md` (replacement unconditional, only the check excepted); deferred entry separate from "Self-updating" |
| 5a | Name affected tests and platforms separately | roadmap "Make the Windows test suite pass" |
| 5b | Close the Windows gate on a fresh Windows CTest run from clean release source | roadmap "Make the Windows test suite pass" |
| 5c | Do not weaken the gate | roadmap "Make the Windows test suite pass" ("Fix the tests, not the gate") |
| 6a | Fresh standard install: Kokoro offline, Qwen rows show NVIDIA guidance, no download | roadmap HITL "Release `v0.1.0-alpha.1`"; roadmap variant slice (automated model-list tests) |
| 6b | Standard → NVIDIA preserves data; Qwen selectable without download | roadmap HITL "Variant transitions, with the first NVIDIA installer" |
| 6c | NVIDIA → standard removes Qwen/CUDA, keeps data, row stays actionable, Kokoro works | roadmap HITL "Variant transitions, with the first NVIDIA installer" |
| 6d | Distinguish runner absent from rejected hardware; the model list module owns the rule | `design/architecture.md` §Model removal, repair, and maintenance actions (action eligibility); roadmap variant slice |
| 6e | Automated model-list/packaging checks where practical; isolated machine for installers | roadmap variant slice (model-list tests, standard-tree packaging check); both HITL items (isolated machine, `PROJECT.md`) |
