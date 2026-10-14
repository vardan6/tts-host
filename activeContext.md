# Active Context

- Mode: session closed.
- Branch: `roadmap-slices`. Reload of external config edits is implemented
  and verified but **uncommitted**; commit it before starting new work.
- Next: `/next-slice` → **Now Playing read actions and source targeting**
  ([roadmap](roadmap.md#next), [finding 3](docs/reviews/code-review-2026-10-14-checkpoint.md#finding-3)).
- After that: **Coalesce download activity progress** (finding 4), then
  finalize the `roadmap-slices` review with the user. The local review is
  same-provider and focused, not an independent full audit.
- Blocker: PR #1 was prematurely merged into `main` as `001192c` by the
  assistant. Do not revert it, merge again, or create/switch branches without
  explicit instruction. No recovery action is authorized yet.
- Verification: Windows/Linux Debug, 47/47 CTests each. Open acceptance: live
  external config edit in the running tray, audible gap probe, combined
  desktop/browser walkthrough, Settings-to-Now-Playing skip, first-run/tab
  visuals, isolated startup/sign-in, model performance. Now Playing visuals
  are already accepted.
- Environment: never change developer login startup; use the development
  config and pre-created startup-choice marker. Quit Host before replacing its
  binary (currently stopped). Managed Chrome blocks Developer mode; use the
  permitted browser. No agents or external agent CLIs without approval
  (`PROJECT.md`).
- Open questions: benchmark harness versus candidate runners first; Qwen
  runtime/quantization and defaults; release layout.
- Discarded as noise: no #14 spool recovery fix without a use case; no
  browser-policy bypass. Use the 440 Hz gap detector; the audible probe needs
  permission.
