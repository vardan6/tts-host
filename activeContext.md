# Active Context

- Mode: session closed.
- Phase/slice: Windows start at login.
- State: The setting, Settings checkbox, and per-user Run-key integration are
  implemented in the working tree; native build and login interaction are not
  verified.
- Next atomic step: Build `tts-host` on native Windows, toggle the setting off
  and on, and confirm the Run entry uses the same config path.
- Next after that: Finish the pending incremental and generated-clean Windows
  workflow acceptance.
- Blockers/environment: Agent environment is WSL2 and has no PowerShell
  executable.
- Open questions: English bake-off execution path and Qwen runtime/quantization;
  Linux capture and release sequencing.
- Discarded as noise: A multi-action/preset script proposal; the requested daily
  flow is the three-command compile, test, and launch sequence.
