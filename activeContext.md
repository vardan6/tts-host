# Active Context

- Mode: review triaged; pre-merge fixes planned.
- Branch: `feature/bundled-models` (14 commits over `main`, ~5.6k lines).
  Only `5e03455..5af176b` was reviewed
  ([Qwen evaluation review](docs/reviews/code-review-2026-10-16-qwen-evaluation.md));
  `5af176b..HEAD` was cross-reviewed 2026-10-19. Console-window fix and combined Settings
  checks confirmed by the user 2026-10-19.
- Next: the three pre-merge fixes (review #1, #4, #5) are done; 51/51 Linux
  CTests pass. Before the HITL merge: Windows Debug build and tests, and the
  installer's unticked-startup upgrade path checked in an isolated Windows
  environment (never this machine), then the user approves the merge to `main`.
- After merge: new branch (e.g. `feature/model-maintenance`) for the roadmap
  slices "Model removal, open folder, and maintenance menu", "Verify and
  repair", the GPU-unavailable decision and fix (review #2), stale release
  stage (review #3), then clean Windows acceptance, then the unified model list.
  Then the "Qwen quality and model options" slices (higher-precision catalogue
  variants, listening comparison, CustomVoice research, per-model options plan);
  the user found bundled Qwen Q4_K_M clearly worse than Kokoro (2026-10-21).
- Pending user check: activity-log scrolling repaint fix (in `1896937`, whose
  message names only the combo fix) — reopen Settings and scroll the log.
- Risk: setup.exe is 1.99 GB, near Inno's 2.1 GB single-file limit; a larger
  bundle needs `DiskSpanning`.
- Open questions: how a runner reports a load that cannot succeed (review #2;
  amends ADR 0002) — HITL slice in `roadmap.md`. Whether/when Qwen becomes quality default (RTF ≈1.5 warm,
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
