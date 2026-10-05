# Active Context

- Mode: session closed; branch-review remediation checkpoint.
- Phase: review/finalize `roadmap-slices` before the Linux adapter.
- State: correctness fixes and #11 desktop launch/#13 resident synthesis are
  implemented, mostly uncommitted. Canonical [triage/plan](docs/reviews/code-review-2026-10-03-roadmap-slices-branch.md#captured-implementation-plan).
- Next action: settle #5 startup registration ownership/triggers, then checkpoint
  code review and combined Windows acceptance. No remaining unblocked phase
  implementation slice. #16 measured: about 80 ms per sentence boundary on Windows;
  scheduling a continuous-stream fix is an open roadmap decision.
- Verification: 45/45 Linux CTests; 43/43 Windows checks excluding the two existing
  ONNX 1.17.1/API-29 failures. Resident reuse, warm-up cancellation, shared Settings
  lifetime, desktop fatal dialog, and Unicode spool cleanup covered. Native
  audible/browser acceptance and Settings-to-Now-Playing skip check remain;
  Now Playing visuals accepted. Real-model performance measurement remains pending.
- Blockers/env: never change developer login startup (`PROJECT.md`); launch with
  `config.development.json`. Quit Host before replacing its binary. Managed
  Chrome disallows Developer mode; use the user's permitted browser.
- Open questions: #5 startup synchronization; bake-off execution, Qwen runtime
  and quantization, release layout. Model manager currently has one resident slot;
  eager priority and lifecycle are in [design](docs/design/architecture.md#desktop-integration).
- Discarded as noise: native UI lookups must use process id; invoke full Windows
  PowerShell path from WSL. No #14 spool transaction fix without a recovery use
  case, no Chrome policy bypass retries. ONNX DLL resolution is a separate fix.
