# Active Context

- Mode: session closed; branch-review remediation checkpoint.
- Phase: review/finalize `roadmap-slices` before the Linux adapter.
- State: all review blockers plus #11/#12/#13/#17 committed (`b733ad1`); #16 gap
  measured (`e1ea73a`). Canonical [triage/plan](docs/reviews/code-review-2026-10-03-roadmap-slices-branch.md#captured-implementation-plan).
- Next action: get the user's explicit yes on #5 (proposed: only the Settings
  toggle/future installer writes the login entry; ordinary launches never
  repoint it), then implement it; then checkpoint review and combined Windows
  acceptance. Also ask whether to schedule a #16 fix (continuous utterance
  stream, or reused client plus event-driven drain).
- Verification: 45/45 Linux CTests; 43/43 Windows checks excluding the two existing
  ONNX 1.17.1/API-29 failures. Resident reuse, warm-up cancellation, shared Settings
  lifetime, desktop fatal dialog, and Unicode spool cleanup covered. Native
  audible/browser acceptance and Settings-to-Now-Playing skip check remain;
  Now Playing visuals accepted. Real-model performance measurement remains pending.
- Blockers/env: never change developer login startup (`PROJECT.md`); launch with
  `config.development.json`. Quit Host before replacing its binary. Managed
  Chrome disallows Developer mode; use the user's permitted browser.
- Open questions: #5 startup synchronization; #16 fix scheduling (~80 ms/boundary,
  [measurement](docs/reviews/code-review-2026-10-03-roadmap-slices-branch.md#finding-16)); bake-off execution, Qwen runtime
  and quantization, release layout. Model manager currently has one resident slot;
  eager priority and lifecycle are in [design](docs/design/architecture.md#desktop-integration).
- Discarded as noise: native UI lookups must use process id; invoke full Windows
  PowerShell path from WSL. No #14 spool transaction fix without a recovery use
  case, no Chrome policy bypass retries. ONNX DLL resolution is a separate fix.
  Gap probe: broadband loopback detection is unreliable with other system audio;
  use the 440 Hz detector. Rerunning the probe plays audible tones, so ask first.
