# Active Context

- Mode: session closed; Qwen evaluation synthesis complete, review pending.
- Branch: `feature/bundled-models`, tracked on origin. Session-close checkpoint
  includes Qwen implementation and prior planning/Windows acceptance docs;
  identify the commit from git history.
- State: Qwen 1.7B Base Q4_K_M + Q8_0 uses pinned llama.cpp b11029 CUDA.
  47/47 CTests pass on Linux and Windows; real GPU synthesis and lifecycle checks
  pass on both. Setup: [README](README.md#qwen-evaluation); evidence: `progress.md`.
- Next: `/cross-review` of the Qwen checkpoint in a user-opened review
  session, then prepare the English bake-off, including
  candidate runner support and reliable peak VRAM measurements. Speed 1.0 only;
  release speech-speed support is separately tracked in `roadmap.md`.
- Limits: smoke throughput misses the target; VRAM is unavailable. No final
  defaults, listening acceptance, catalogue, release artifact or installer claimed.
- Artifacts: evaluation config/package/WAVs/logs in `build/qwen-evaluation/`;
  Windows runtime in `cache/qwen-evaluation/b11029/runtime`. Normal registry and
  development config unchanged; development Host running at close.
- Blockers/open decisions: final runner/quantization and revision/storage/release
  layout; see `roadmap.md`. No agents/external agent CLIs without approval.
- Windows acceptance remains closed on user confirmation:
  [report](docs/reviews/windows-acceptance-2026-10-16.md#final-closure--all-checks-accepted).
- Environment: never change developer login startup; use development config and
  existing startup-choice marker. Quit Host before replacing its binary.
  `clipboardOnly` needs manual copy before Ctrl+F8. No browser-policy bypass.
- Discarded as noise: the initial Linux recovery-test failure used a stale
  fixture binary; rebuilding resolved it and the full suite passed.
