# Active Context

- Mode: implementation; the bundled two-model release block is complete.
- Branch: `feature/model-maintenance`, pushed. Uncommitted: the 2026-10-24
  roadmap closures, the review file with its triage, and the four review fixes.
- State: all four model-maintenance review fixes are in and pass both builds'
  tests. The user is considering dropping Qwen (Open decisions in `roadmap.md`).
- Next: `/review-triage` the follow-up cross-review findings #5–#7 in
  `docs/reviews/code-review-2026-10-24-model-maintenance.md` (startup protocol
  errors skip recovery; no recovery route for older or local damaged packages;
  damaged copies ignore tier precedence). The UI fixes passed the user's desktop
  check. Merge to `main` waits on that triage; then the Qwen keep/drop decision
  gates the remaining Qwen slices.
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
