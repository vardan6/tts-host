# Active Context

- Mode: implementation, bundled two-model release follow-ups.
- Branch: `feature/model-maintenance`; all work is uncommitted: Settings
  layout, model selection and shortcut fixes, 2026-10-23 roadmap closures,
  CustomVoice research capture, and the Kokoro fetch-script revision stamp.
- State: the only open slice in the release block is the Unified Settings model
  list. Its code is built; Linux 53/53 CTests and Windows build/link passed in
  `build/settings-selection-check`. Only the user's desktop checks remain.
- Next: user quits the running Debug Host, runs `.\compile-win.ps1`, and checks:
  - Ctrl+F8 with Settings open, after probing a binding, and after closing.
    The saved capture policy is `clipboardOnly`, so copy text first.
  - Load Q8_0, then read while Settings is open and after close/restart; the
    model must stay selected. Repeat with Kokoro.
  - All four tabs render, the window resizes, the Model/Source divider drags,
    and the rows, details, buttons and More ▾ work. Kokoro now shows `current`.

  Keep the roadmap item unchecked until the user confirms or waives these.
- Then: the user reports the Qwen comparison outcome (they believe it is done;
  nothing is recorded): which variant is bundled and whether the reference
  voice changes. A change means an ADR superseding 0009. After that, the HITL
  decisions on the named-speaker path and per-model options in `roadmap.md`.
- Blockers/environment: the running Host locks its executable; do not
  terminate it without permission, and do not link over it. Use a separate
  output folder for build checks. Developer launches use
  `config.development.json` (`PROJECT.md`). Linux tests: `build-linux/`. Run
  `tts-host --list-models` with `--headless`, or it starts the app. Windows
  PowerShell: `/mnt/c/Windows/System32/WindowsPowerShell/v1.0/powershell.exe`.
- Open questions: the Qwen comparison outcome; whether Ctrl+F8 also failed
  after closing Settings; the named-speaker path and VoiceDesign scope;
  per-model options; the Qwen default (Open decisions in `roadmap.md`). Linux
  Qwen runner-folder loading is unverified; previous-only restoration is
  undesigned.
- Discarded as noise:
  - The reported Q8→Q4 switch was real profile routing, not a list-row
    display error.
  - Blanket clipping of sibling fields hid tab contents; keep the tab frame
    behind its fields.
  - The port 7861 failure was an existing Debug Host, not a test failure.
  - Kokoro showing `local` was a missing revision stamp from the developer
    fetch script; its files are identical to the catalogue's.
