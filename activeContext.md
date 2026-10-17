# Active Context

- Mode: session closed; ADR 0010 revisions/two-tier slice done.
- Branch: `feature/bundled-models`; ADR 0010 capture and the revisions/two-tier
  slice uncommitted, not pushed.
- State: catalogue installs record a revision; the registry scans the bundled
  (`<exe>/models`) and per-user tiers with ADR 0010 precedence, and lists
  superseded copies. CLI and Settings show local/current/update available.
  Linux 49/49 CTests; Windows `tts-host` compiles, not run.
- Next: AFK **Manual catalogue model update** (`roadmap.md`) via
  `/next-slice`, including removal of superseded per-user copies; also skip
  dot-prefixed staging/`.previous` folders in the registry scan. Then
  release package (runner folders), installer.
- Pending user checks (combined acceptance): Windows rebuild; Settings
  model status labels; listen to Qwen at
  0.5/1.5/2.0 in Now Playing; Settings download (WinHTTP) and import of Qwen.
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
  `clipboardOnly` needs manual copy before Ctrl+F8.
- Discarded as noise: running `tts-host-model-registry-tier-tests` with
  relative paths fails ("missing bundled models/ … unsupported") because
  configured directories resolve against the config folder; use absolute paths
  as CTest does. Older noise: Linux Qwen test "requires a working CUDA runtime" failure
  was only the missing `LD_LIBRARY_PATH`, not a code fault. Qwen output length
  varies 5–20% between runs at a fixed seed, so real-model speed checks assert
  scale, not exact 1/speed. `decode: failed to find a memory slot` in the
  integration test is the deliberate over-context rejection.
