# Active Context

- Mode: implementation (behavior-preserving refactors); model maintenance
  merged to `main` (PR #4, 2026-10-25).
- Branch: `chore/improve-codebase-architecture` from `main` at `a47ebe4`, local
  only, not pushed.
- State: the three AFK slices of `roadmap.md` § Architecture deepening are
  committed (2026-10-26): model-action eligibility in the model list
  (`3ca12cb`), the speech queue module (`d34721f`), and profile resolution
  (`0577039`). Linux: 55/55 tests pass. Windows `build-gpu-fallback` builds
  and passes everything except the known `cli_language_selection` false
  failure. Qwen stays with all variants (decided 2026-10-26).
- Next: the HITL desktop click-through of the model rows (`roadmap.md`), which
  also covers the damaged-row actions (#6); then `/cross-review` of the branch
  before merging. Candidate 4's other typed reads stay deferred
  (`docs/design/architecture.md#profile-resolution`).
- Blockers/environment: the running Host locks its executable; do not
  terminate it without permission, and do not link over it. Use a separate
  output folder for build checks. Developer launches use
  `config.development.json` (`PROJECT.md`). Linux tests: `build-linux/`. Run
  `tts-host --list-models` with `--headless`, or it starts the app. Windows
  PowerShell: `/mnt/c/Windows/System32/WindowsPowerShell/v1.0/powershell.exe`.
- Open questions: Qwen named-speaker path and VoiceDesign
  scope; per-model options; the other Open decisions in `roadmap.md`. Linux
  Qwen runner-folder loading is unverified; previous-only restoration is
  undesigned.
- Discarded as noise:
  - Kokoro showing `local` was a missing revision stamp from the developer
    fetch script.
  - The port 7861 failure was an existing Debug Host, not a test failure.
  - `cli_language_selection` failing in `build-gpu-fallback` is its "fallback"
    check matching the folder name, not a regression.
  - The harness guard blocks Bash heredocs or scripts that contain "Qwen" text;
    edit such files with the Edit tool.
  - A source edit once vanished from `/mnt/c` between a passing build and the
    next command (cause unknown); grep for the change before trusting a
    revert-and-restore check.
