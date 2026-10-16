# Active Context

- Mode: implementation; Qwen speed control and Qwen catalogue slices done.
- Branch: `feature/bundled-models`, committed, not pushed.
- State: ADR 0009 Qwen package is selectable, not default. The `qwen3-gguf`
  runner applies 0.5–2.0 speed via a streaming WSOLA stretch
  (`time_stretch.cpp`). The catalogue offers `qwen3-tts-1.7b` with the
  public-domain LJ Speech LJ001-0001 reference; host-written `voiceLicense`.
  Linux: 48/48 CTests, real-GPU Qwen test, and a real catalogue install that
  speaks. Windows not rebuilt since October 16.
- Next: HITL **Decide model revision and replacement storage** (`roadmap.md`)
  via `/grilling` or `/planning-capture`; it gates update, packaging, installer.
- Pending user checks (combined acceptance): Windows rebuild; listen to Qwen at
  0.5/1.5/2.0 in Now Playing; Settings download (WinHTTP) and import of Qwen.
- Open questions: whether/when Qwen becomes quality default (RTF ≈1.5 warm,
  VRAM unmeasured) — `roadmap.md` open decisions.
- Blockers: release layout / storage / revision decision (HITL) gates update,
  packaging, and installer slices. No agents/external agent CLIs without approval.
- Environment: Linux Qwen runs need
  `LD_LIBRARY_PATH=~/.cache/tts-bakeoff/llama/b11029/cudart-llama-b11029-bin-ubuntu-cuda-13.3-x64:/usr/lib/wsl/lib`.
  Evaluation package: `build/qwen-evaluation/models/qwen3-tts-1.7b/`. Never
  change developer login startup; quit Host before replacing its binary.
  `clipboardOnly` needs manual copy before Ctrl+F8.
- Discarded as noise: Linux Qwen test "requires a working CUDA runtime" failure
  was only the missing `LD_LIBRARY_PATH`, not a code fault.
