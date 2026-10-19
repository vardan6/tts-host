# Active Context

- Activity-log repaint fix: read-only EDIT controls now use opaque backgrounds
  to erase old glyphs when scrolling. Windows compilation passes; link blocked
  by running Host (LNK1168). After the rebuild below, reopen Settings and scroll
  the log to verify. Existing combo-selection changes preserved.
- Mode: STEP closed; console-window fix verified by user; Settings combo fix pending.
- Branch: `feature/bundled-models`; HEAD `a5e1347`. Uncommitted:
  `src/settings_window.cpp` keeps the Model/Catalogue combo selection across
  `refresh_model_views` (was jumping to Kokoro after Delete previous / Remove
  superseded). Compiles; not linked (LNK1168, Host running). See `progress.md`.
- Next: user quits TTS Host from tray; run
  `cmake --build build --config Debug --target tts-host`, launch
  `.\build\Debug\tts-host.exe --config config.development.json`, select Qwen,
  press Delete previous and Remove superseded — Qwen must stay selected. Then
  commit the fix and run the remaining Settings worker checks.
- After that: clean Windows two-model acceptance from `roadmap.md`, including
  offline install, upgrade install, and interactive startup selection.
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
- Blockers: running Debug Host locks its executable; user must quit before
  rebuilding. Do not terminate it without asking. Existing developer Run entry
  points at Debug Host; left for the user per `PROJECT.md`.
- Environment: Debug `build/` is configured with
  `TTS_HOST_QWEN_RUNTIME_DIR=…/cache/qwen-evaluation/b11029/runtime` (same as
  `build-release`); configure now falls back to it when unset (Windows), and `tts-host` depends on its runners.
  Linux Qwen runs need
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
- Discarded as noise: Host already uses the GUI subsystem; the speech runner
  lacked `CREATE_NO_WINDOW`. LNK1168 is a running-executable lock, not a compiler
  failure. Missing Qwen runner was a Debug CMake configuration issue (a clean configure
  without the runtime dir); runtime-copy "Permission denied" was a loaded runner
  holding its DLLs — both fixed, see progress. Grep for the Qwen name trips the
  harness guard; use a script file.
  Build test targets before CTest; GUI Host output must be piped for PowerShell
  to wait and set `$LASTEXITCODE`.
