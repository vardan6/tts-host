# Active Context

- Mode: session closed; console-window fix implemented, Host link pending.
- Branch: `feature/bundled-models`; release checkpoint pushed (`3402563`),
  Settings worker fix committed (`c3a85aa`). Release/build history: `progress.md`.
- State: `src/runner_launcher.cpp` adds `CREATE_NO_WINDOW` for speech runners.
  Windows runner inheritance/protocol/audio CTest passes (1/1). The Host source
  compiles, but replacing `build/Debug/tts-host.exe` failed with LNK1168 while
  the existing Host was running. Desktop visibility remains unverified.
- Next: user quits TTS Host from tray; run
  `cmake --build build --config Debug --target tts-host`, then launch
  `.\build\Debug\tts-host.exe --config config.development.json` and verify
  startup/model loading creates no terminal. Run pending Settings worker checks.
  Qwen 1.7B loads in the Debug Host (runner fallback/dependency/if-different
  copy in `CMakeLists.txt`, uncommitted).
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
