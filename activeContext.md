# Active Context

- Mode: session closed.
- Branch: `main` only. `roadmap-slices` was merged (PR #2, `13d5aa9`) and
  deleted locally and on origin; do not recreate it.
- State: Windows desktop prototype and both branch reviews complete (the
  October 14 review was same-provider and focused, not a full audit).
  Windows/Linux Debug 47/47 CTests. Native acceptance is open:
  `roadmap.md` → Windows acceptance.
- Next: combined Windows acceptance walkthrough with the user, or
  `/planning-capture` for played-text history/queue.
- Environment: never change developer login startup; use the development
  config and pre-created startup-choice marker. Quit Host before replacing its
  binary (currently stopped). Managed Chrome blocks Developer mode; use the
  permitted browser. The audible gap probe needs permission. No agents or
  external agent CLIs without approval (`PROJECT.md`).
- Open questions: `roadmap.md` → Open decisions.
- Discarded as noise: no browser-policy bypass; use the 440 Hz gap detector.
