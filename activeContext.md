# Active Context

- Mode: session closed; manual catalogue model update slice done.
- Branch: `feature/bundled-models`; the update slice (code, tests, design
  and README) is uncommitted, not pushed. Revisions/two-tier is in `b7b6039`.
- State: Update/Revert/Delete previous/Remove superseded work in Settings and
  CLI, with journaled commit and startup recovery. Linux 50/50 CTests; Windows
  model tests pass and `tts-host` builds.
- Next: AFK **Two-model release package** (`roadmap.md`) via `/next-slice`
  in a fresh session. Then the offline installer.
- Pending user checks (combined acceptance): Windows Settings model status
  labels and Update/Revert/Delete previous/Remove superseded buttons; listen
  to Qwen at 0.5/1.5/2.0 in Now Playing; Settings download (WinHTTP) and
  import of Qwen.
- Open questions: whether/when Qwen becomes quality default (RTF ≈1.5 warm,
  VRAM unmeasured) — `roadmap.md` open decisions.
- Blockers: none. No agents/external agent CLIs without approval.
- Environment: Linux Qwen runs need
  `LD_LIBRARY_PATH=~/.cache/tts-bakeoff/llama/b11029/cudart-llama-b11029-bin-ubuntu-cuda-13.3-x64:/usr/lib/wsl/lib`.
  No Qwen package persists on disk: build one with
  `tools/package_qwen_evaluation.py` from the cached GGUFs under
  `~/.cache/tts-bakeoff/hf/` (README Qwen evaluation). Whisper for
  intelligibility checks: `HF_HOME=~/.cache/tts-bakeoff/hf
  ~/.cache/tts-bakeoff/venv/bin/python` with `openai/whisper-base`. Never
  change developer login startup; quit Host before replacing its binary.
  `clipboardOnly` needs manual copy before Ctrl+F8. Windows build/tests from
  WSL: `/mnt/c/Windows/System32/WindowsPowerShell/v1.0/powershell.exe
  -NoProfile -Command "Set-Location '<repo>'; cmake --build build --config
  Debug --target <targets>; ctest --test-dir build -C Debug -R <regex>"`.
- Discarded as noise: Windows ctest "Unable to find executable … Not Run"
  only means that test target was never built in `build/`; build it by name.
  Running `tts-host-model-registry-tier-tests` with
  relative paths fails ("missing bundled models/ … unsupported") because
  configured directories resolve against the config folder; use absolute paths
  as CTest does. Older noise: Linux Qwen test "requires a working CUDA runtime" failure
  was only the missing `LD_LIBRARY_PATH`, not a code fault. Qwen output length
  varies 5–20% between runs at a fixed seed, so real-model speed checks assert
  scale, not exact 1/speed. `decode: failed to find a memory slot` in the
  integration test is the deliberate over-context rejection.
