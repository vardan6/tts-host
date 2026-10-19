# Active Context

- Mode: session closed; unplanned Settings freeze fix committed, compiled only — not linked or verified.
- Branch: `feature/bundled-models`; release slices committed and pushed (`3402563`).
  Then: Settings model-task worker fix (`src/settings_window.cpp`) —
  Download/Update/Import froze the window ("Not Responding") on the UI thread.
- State: `scripts/release-win.ps1` builds the zip and the Inno Setup
  installer into `build-release/dist/`; both models speak from a clean
  extracted zip and from a silent temp install. Models staged once in
  `cache/release-models`.
- Latest maintenance: failure fixture now reports exceptions and exits unsuccessfully;
  Windows Debug regression and recovery checks pass (4/4). See `progress.md`.
- Next: quit the running Debug Host, link and test the Settings worker fix,
  user checks Download responsiveness/Cancel/resume; then HITL **Clean Windows two-model acceptance** (`roadmap.md`) on a clean
  machine with networking off, including an upgrade install over an existing
  one and the interactive startup tick.
- Pending user checks (combined acceptance): Windows Settings model status
  labels and Update/Revert/Delete previous/Remove superseded buttons; listen
  to Qwen at 0.5/1.5/2.0 in Now Playing; Settings download (WinHTTP) and
  import of Qwen.
- Risk: setup.exe is 1.99 GB, near Inno's 2.1 GB single-file limit; a larger
  bundle needs `DiskSpanning`.
- Open questions: whether/when Qwen becomes quality default (RTF ≈1.5 warm,
  VRAM unmeasured) — `roadmap.md` open decisions. Linux Qwen with the new
  runner-folder loading is unbuilt and unverified (only Windows has the
  runtime configured).
- Blockers: the running Debug Host (`build\Debug\tts-host.exe`) locks the exe; the
  worker fix compiled with no warnings but link (LNK1168) and tests wait for the user to quit it. No agents/external agent CLIs without approval. This
  machine has a pre-existing `HKCU\…\Run\TTS Host` entry pointing at
  `build\Debug\tts-host.exe` (against `PROJECT.md`); left for the user.
- Environment: Linux Qwen runs need
  `LD_LIBRARY_PATH=~/.cache/tts-bakeoff/llama/b11029/cudart-llama-b11029-bin-ubuntu-cuda-13.3-x64:/usr/lib/wsl/lib`.
  A catalogue Qwen package (Windows) persists in
  `cache/release-models/models/qwen3-tts-1.7b`; on Linux build one with
  `tools/package_qwen_evaluation.py` from the cached GGUFs under
  `~/.cache/tts-bakeoff/hf/` (README Qwen evaluation). Whisper for
  intelligibility checks: `HF_HOME=~/.cache/tts-bakeoff/hf
  ~/.cache/tts-bakeoff/venv/bin/python` with `openai/whisper-base`. Never
  change developer login startup; quit Host before replacing its binary.
  `clipboardOnly` needs manual copy before Ctrl+F8. `tts-host.exe` is
  GUI-subsystem: PowerShell waits and sets `$LASTEXITCODE` only when its
  output is piped. The harness guard blocks commands whose segments start with
  the Qwen model name; put such scripts in a file. Windows build/tests from
  WSL: `/mnt/c/Windows/System32/WindowsPowerShell/v1.0/powershell.exe
  -NoProfile -Command "Set-Location '<repo>'; cmake --build build --config
  Debug --target <targets>; ctest --test-dir build -C Debug -R <regex>"`.
- Discarded as noise: the first release run "packaged" before the downloads
  finished only because PowerShell did not wait for GUI-subsystem
  `tts-host.exe` (fixed by piping), not a download fault. Windows ctest "Unable to find executable … Not Run"
  only means that test target was never built in `build/`; build it by name.
  Running `tts-host-model-registry-tier-tests` with
  relative paths fails ("missing bundled models/ … unsupported") because
  configured directories resolve against the config folder; use absolute paths
  as CTest does. Older noise: Linux Qwen test "requires a working CUDA runtime" failure
  was only the missing `LD_LIBRARY_PATH`, not a code fault. Qwen output length
  varies 5–20% between runs at a fixed seed, so real-model speed checks assert
  scale, not exact 1/speed. `decode: failed to find a memory slot` in the
  integration test is the deliberate over-context rejection.
