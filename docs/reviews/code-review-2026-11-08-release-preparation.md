# Code review — release preparation

- Date: 2026-11-08.
- Range: `b4ce04043b5f70cf31db0641dc29decc72290497..48bcf32ad1302dbc16ce105ff33b01768012ee88` (`main..HEAD`, branch `release-prep/v0.1.0-alpha.1`). `activeContext.md` explicitly identifies this range. The user's uncommitted `config.example.json` edits are excluded.
- Reviewer: GPT-6 / Codex, OpenAI. Implementer: Opus 5.5, Anthropic, confirmed by the developer; cross-provider review.
- Docs consulted: [distribution requirements](../requirements/product.md#distribution-and-usability); [configuration](../design/architecture.md#configuration), [maintenance actions](../design/architecture.md#model-removal-repair-and-maintenance-actions), [distribution](../design/architecture.md#distribution), and [versions and releases](../design/architecture.md#versions-and-releases); ADRs [0008](../adr/0008-os-owned-login-startup.md), [0009](../adr/0009-qwen-release-selection-without-bake-off.md), [0010](../adr/0010-model-revisions-storage-and-release-layout.md), and [0011](../adr/0011-standard-and-nvidia-release-variants.md); [alpha plan review and triage](plan-review-2026-11-03-first-public-alpha.md); [roadmap](../../roadmap.md#first-public-alpha-windows); README and alpha release notes.
- Verification: model-list target was current (`ninja: no work to do`); targeted Linux CTest passed 4/4 (`tts_host_model_list`, `tts_host_model_update`, `tts_host_cli_language_selection`, `tts_host_tray_not_implemented_on_this_platform`). An isolated temporary Git remote reproduced finding #2. A Windows PowerShell probe rejected a missing native command, so that suspected issue was discarded. No full Windows release rebuild, installer execution, real push, or GitHub release creation was performed. Existing recorded Windows dry-run evidence is not a new reviewer verification.

| # | Finding | Evidence (file:line) | Severity | Risk | Value | Effort |
|---|---------|----------------------|----------|------|-------|--------|
| 1 | A runner-absent model can still download through **Verify and repair**. After switching NVIDIA → standard, a damaged per-user Qwen package at the current catalogue revision retains this action, and confirmation downloads its damaged assets despite the absent runner. See Details #1. | `src/model_list.cpp:52`, `src/model_list.cpp:64`, `src/settings_window.cpp:1287`, `src/settings_window.cpp:1258`, `src/model_update.cpp:508` | med | med | med | low |
| 2 | `-DryRun` changes caller refs and can import tags through its unconditional `git fetch origin`, contrary to the explicit untouched-refs-and-tags contract. See Details #2. | `scripts/release.ps1:82`, `scripts/release.ps1:153`; contract: `roadmap.md:134`, `docs/design/architecture.md:1181` | low | low | med | low |
| 3 | **Enhancement:** add inexpensive release-script failure-path checks using temporary repositories and stubbed build/GitHub commands. The successful rehearsal does not exercise rejected atomic pushes or draft-creation failures after publication. See Details #3. | `scripts/release.ps1:160`, `scripts/release.ps1:162`, `scripts/release.ps1:169`, `scripts/release.ps1:173` | enhancement | med | high | med |

## Details

### 1 — Keep verification separate from runner-absent repair downloads

The new `runnable` guard applies to Load, Download, and Update, but `verifiable` considers only catalogue membership and revision. `package_status()` (`src/model_catalogue.cpp:329`) compares the recorded revision, not file integrity, so damaged current-revision packages qualify. The damaged-row path also calls the same action builder.

Concrete scenario: a user has downloaded Qwen in the NVIDIA installation, switches to standard (which deliberately preserves per-user packages), and a model asset is damaged or missing. Settings marks the row as needing the NVIDIA variant, but More → Verify and repair remains enabled. `on_verify_model_clicked()` schedules `start_model_repair()` for a damaged non-bundled package without checking runner availability; that function invokes `repair_catalogue_package()`, which calls `stage_catalogue_file()` for damaged files. This can download a large asset that the installation cannot use.

The distribution requirement says the standard variant never offers a download for a model whose runner it lacks. The narrower action-eligibility design enumerates only Download, Update, and Load, while the existing maintenance design retains verification/repair. Reconcile this boundary during triage: preserving read-only verification is useful, but repair's network phase needs the absent-runner restriction, or an explicitly captured exception. Add a damaged current-revision runner-absent case; the new tests currently exercise absent catalogue downloads and an installed local package, not this repair route (`tests/model_list_tests.cpp:315`).

### 2 — Avoid fetching into caller refs during rehearsal

The roadmap expressly promises that `-DryRun` leaves caller refs and tags untouched; the script also promises untouched branches/tags through step 6. Its precondition runs `git fetch --quiet origin` before any dry-run branch. With ordinary origin refspecs, this updates `refs/remotes/origin/*` and auto-follows newly reachable tags. Worktree cleanup cannot undo those changes.

Verified in an isolated temporary repository: clone a local bare remote, advance its main and add an unrelated tag, then run the exact fetch used by the script. Comparing `git show-ref` before and after shows changed refs, and `git tag --list` shows the imported tag. No project remotes or refs were changed by this probe.

For a dry run, skip this fetch: remote tag existence is already checked through `ls-remote`, and the origin/main equality check is deliberately skipped. Keep any real-run synchronization separate. This does not invalidate the built artifacts or move the checked-out branch, hence low severity.

### 3 — Exercise publication recovery without making a real release

The successful recorded rehearsal exits at line 156. Consequently it cannot validate local-tag cleanup after an atomic push rejection, main fast-forward after success, or preservation of artifacts and useful recovery instructions when `gh release create` fails after the tag is pushed.

Recommend a small harness with a temporary bare Git remote and stubbed expensive build, packaging, and GitHub commands. Assert that a rejected push changes neither remote main nor remote tag and removes the newly created local tag; that successful publication tags the exact built commit; and that a draft failure retains artifacts and reports recovery. Include a before/after refs assertion for dry runs to cover finding #2. These tests exercise workflow guarantees rather than duplicating the script's implementation and need no real account, installer execution, or GPU. No recorded decision requires changing the release design to add them.
