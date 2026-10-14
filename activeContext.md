# Active Context

- Mode: implementation; step closed.
- Branch: `roadmap-slices`. All October 14 checkpoint findings (1–4) are
  resolved; finding 4 (download progress coalescing) is uncommitted.
- Next: commit finding 4, then finalize the `roadmap-slices` review with the
  user. Played-text history/queue is a roadmap planning item, not scheduled
  work. The local review is same-provider and focused, not an independent
  full audit.
- Blocker: PR #1 was prematurely merged into `main` as `001192c` by the
  assistant. Do not revert it, merge again, or create/switch branches without
  explicit instruction. No recovery action is authorized yet.
- Verification: Windows/Linux Debug, 47/47 CTests each. Open acceptance: live
  external config edit in the running tray, Now Playing Read selection of a
  real external target, audible gap probe, combined
  desktop/browser walkthrough, Settings-to-Now-Playing skip, first-run/tab
  visuals, isolated startup/sign-in, model performance. The original Now Playing
  visuals are accepted; the added read controls (taller window) are not.
- Environment: never change developer login startup; use the development
  config and pre-created startup-choice marker. Quit Host before replacing its
  binary (currently stopped). Managed Chrome blocks Developer mode; use the
  permitted browser. No agents or external agent CLIs without approval
  (`PROJECT.md`).
- Open questions: whether played-text history may retain spoken text
  (roadmap HITL item); benchmark harness versus candidate runners first; Qwen
  runtime/quantization and defaults; release layout.
- Discarded as noise: no #14 spool recovery fix without a use case; no
  browser-policy bypass. Use the 440 Hz gap detector; the audible probe needs
  permission.
