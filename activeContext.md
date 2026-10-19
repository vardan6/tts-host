# Active Context

- Mode: session closed; branch review pending.
- Branch: `feature/bundled-models` (14 commits over `main`, ~5.6k lines).
  Only `5e03455..5af176b` was reviewed
  ([Qwen evaluation review](docs/reviews/code-review-2026-10-16-qwen-evaluation.md));
  `5af176b..HEAD` is unreviewed. Console-window fix and combined Settings
  checks confirmed by the user 2026-10-19.
- Next: user runs `/cross-review` of `main..feature/bundled-models` in another
  provider's session (focus `5af176b..HEAD`: revisions/discovery, update,
  revert and journal recovery, release layout, installer, Settings worker);
  then `/review-triage`, fix on this branch, merge to `main`.
- After merge: new branch (e.g. `feature/model-maintenance`) for the roadmap
  slices "Model removal, open folder, and maintenance menu", "Verify and
  repair", then clean Windows acceptance, then the unified model list.
- Pending user check: activity-log scrolling repaint fix (in `1896937`, whose
  message names only the combo fix) — reopen Settings and scroll the log.
- Risk: setup.exe is 1.99 GB, near Inno's 2.1 GB single-file limit; a larger
  bundle needs `DiskSpanning`.
- Open questions: whether/when Qwen becomes quality default (RTF ≈1.5 warm,
  VRAM unmeasured) — `roadmap.md` open decisions. Linux Qwen with the new
  runner-folder loading is unbuilt and unverified (only Windows has the
  runtime configured).
- Blockers: running Debug Host locks its executable; user must quit before
  rebuilding. Do not terminate it without asking. Existing developer Run entry
  points at Debug Host; left for the user per `PROJECT.md`.
- Environment: Windows Debug `build/` uses the cached b11029 Qwen runtime;
  `tts-host` builds its runners. Use `config.development.json` with login
  startup disabled. Windows build/test commands and Linux runtime details are
  recorded in `progress.md`, README, and `PROJECT.md`.
- Discarded as noise: LNK1168 is the running executable lock, not a compiler
  error. The scrolling defect came from transparent read-only EDIT backgrounds;
  the fix compiled, but a build alone cannot establish visual acceptance.
