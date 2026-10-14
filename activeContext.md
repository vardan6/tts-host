# Active Context

- Mode: review finalized.
- Branch: `roadmap-slices`. All October 3 and October 14 review findings are
  resolved or deliberately discarded. On 2026-10-14 the user explicitly
  approved merging `roadmap-slices` into `main` by pull request.
- Next: native Windows acceptance walkthrough (list below), or plan
  played-text history/queue (roadmap HITL item, `/planning-capture`). The
  local review is same-provider and focused, not an independent full audit.
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
