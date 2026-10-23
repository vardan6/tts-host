# Active Context

- Mode: implementation; the bundled two-model release block is complete.
- Branch: `feature/model-maintenance`, pushed. The 2026-10-24 roadmap closures
  are uncommitted.
- State: the unified Settings list passed the user's desktop checks. The Qwen
  comparison closed with no change, but the user rated every variant poor and
  about 5× too slow and is considering dropping Qwen (Open decisions in
  `roadmap.md`).
- Next: the user decides whether Qwen stays. That gates the named-speaker
  path and per-model options slices. Otherwise `/next-slice` picks from the
  remaining roadmap (Synthetic Copy compatibility, history planning,
  Russian/Armenian acceptance). The branch is also ready for review/merge to
  `main` when the user wants.
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
