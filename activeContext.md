# Active Context

- Mode: session closed
- Phase/slice: Windows tray selection test; model bake-off is deferred.
- State: the tray has **Read clipboard** and a fixed `Ctrl+Alt+R` selection
  command that sends Copy then speaks through the current default profile. The
  Linux build and all 31 CTests pass; the Windows-only path is unverified.
- Next atomic step: validate the Windows tray selection test: select text in a
  representative application and press `Ctrl+Alt+R`; then add the configurable
  hotkey binding and toggle.
- Blockers/environment: no GPU engine is installed in TTS Host yet; model
  bake-off and selection remain deferred while desktop functionality is built.
  The new selection test still needs native Windows validation across
  representative applications.
- Open questions: Qwen backend/quantization and the fast/quality defaults after
  the agreed-passage comparison; WSL headless and release packaging choices.
- Discarded as noise: spawning a second Host process for tray playback; the tray
  calls the owning Host's synthesis path instead.
