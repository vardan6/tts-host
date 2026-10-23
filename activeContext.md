# Active Context

- Mode: implementation; the bundled two-model release block is complete.
- Branch: `feature/model-maintenance`, pushed and clean. `86a7e43` has the four
  review fixes; `af7c728` has the follow-up triage and the fixes for #5–#7.
- State: all seven model-maintenance review findings are fixed and pass both
  builds' tests. The new Settings actions for damaged rows (#6: Update on an
  older revision, Remove… on a per-user copy) are not desktop-checked. The
  user is considering dropping Qwen (Open decisions in `roadmap.md`).
- Next: user checks the damaged-row actions on the desktop if
  wanted; then merge to `main`. The UI fixes passed the user's desktop check.
  The Qwen keep/drop decision gates the remaining Qwen slices.
- Blockers/environment: the running Host locks its executable; do not
  terminate it without permission, and do not link over it. Use a separate
  output folder for build checks. Developer launches use
  `config.development.json` (`PROJECT.md`). Linux tests: `build-linux/`. Run
  `tts-host --list-models` with `--headless`, or it starts the app. Windows
  PowerShell: `/mnt/c/Windows/System32/WindowsPowerShell/v1.0/powershell.exe`.
- Open questions: whether to drop Qwen; named-speaker path and VoiceDesign
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
