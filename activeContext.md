# Active Context

- Mode: session closed; Qwen review fixed, bundled-Qwen selection captured.
- Branch: `feature/bundled-models`, clean; review fixes and ADR 0009 are
  committed (not pushed).
- State: bake-off skipped per
  [ADR 0009](docs/adr/0009-qwen-release-selection-without-bake-off.md); 1.7B
  Q4_K_M/Q8_0 llama.cpp b11029 is the release Qwen, selectable but not default.
  The runner now rejects unsupported speed / long text without restarting
  ([review](docs/reviews/code-review-2026-10-16-qwen-evaluation.md#triage--2026-10-16)).
  Linux: 47/47 CTests + real-GPU Qwen test pass. Windows not rebuilt since.
- Next: `/next-slice` → **Qwen catalogue and local import**
  (`roadmap.md`, bundled two-model release).
- Open questions: whether/when Qwen becomes quality default (RTF ≈1.5 warm,
  VRAM unmeasured) — `roadmap.md` open decisions.
- Risks for next slice: the Qwen runner is an optional CMake target (normal
  builds lack it); the catalogue needs a hosted, licensed reference-voice clip.
- Blockers: release layout / storage / revision decision (HITL) gates update,
  packaging, and installer slices. No agents/external agent CLIs without approval.
- Environment: Linux Qwen runs need
  `LD_LIBRARY_PATH=~/.cache/tts-bakeoff/llama/b11029/cudart-llama-b11029-bin-ubuntu-cuda-13.3-x64:/usr/lib/wsl/lib`.
  Evaluation package: `build/qwen-evaluation/models/qwen3-tts-1.7b/`. Never
  change developer login startup; quit Host before replacing its binary.
  `clipboardOnly` needs manual copy before Ctrl+F8.
- Discarded as noise: Linux Qwen test "requires a working CUDA runtime" failure
  was only the missing `LD_LIBRARY_PATH`, not a code fault.
