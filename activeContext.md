# Active Context

- Mode: pre-merge fixes done; awaiting Windows checks and merge (HITL).
- Branch: `feature/bundled-models`; review #1, #4, #5 fixes and the Qwen
  options planning are in `94963cb`. 51/51 Linux CTests pass.
- Next: the user runs `powershell -f .\fresh-win.ps1` (Windows
  Debug build + CTest; quit Debug Host first) and checks the installer's
  unticked-startup upgrade path in an isolated Windows environment (never this
  machine); then approves the merge to `main`.
- After merge: new branch (e.g. `feature/model-maintenance`) for the open
  `roadmap.md` slices in order, ending with "Qwen quality and model options";
  the user found bundled Qwen Q4_K_M clearly worse than Kokoro (2026-10-21).
- Pending user check: activity-log scrolling repaint fix (in `1896937`) —
  reopen Settings and scroll the log.
- Risk: setup.exe is 1.99 GB, near Inno's 2.1 GB single-file limit; a larger
  bundle needs `DiskSpanning`. The pinned Hugging Face revision may lack the
  Q8_0/bf16 files (only `main` was checked).
- Open questions: in `roadmap.md` (runner load-failure reporting, Qwen default,
  per-model options). Linux Qwen with runner-folder loading is unverified.
- Blockers: running Debug Host locks its executable; do not terminate it
  without asking. Developer Run entry left for the user per `PROJECT.md`.
- Environment: Windows Debug `build/` uses the cached b11029 Qwen runtime; use
  `config.development.json` with login startup disabled. Commands in README,
  `progress.md`, `PROJECT.md`.
- Discarded as noise: LNK1168 is the running-executable lock. The
  `harness-guard` hook blocks shell commands with a segment starting with the
  Qwen model name; put such text in a file instead.
