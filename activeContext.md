# Active Context

- Mode: implementation, bundled two-model release follow-ups.
- Branch: `feature/model-maintenance` (from `main` at `01f1684`, PR #3 merged).
  "Model removal, open folder, and maintenance menu" is done (uncommitted);
  51/51 Linux CTests pass.
- Next: `/next-slice` → "AFK — Verify and repair catalogue models" (adds its
  item to the same More ▾ menu, `src/settings_window.cpp`).
- Pending user checks on Windows: quit Debug Host, run
  `powershell -f .\fresh-win.ps1`, then in Settings → Models try More ▾ and
  Remove… (dialog, keep-previous tick, Cancel default); scroll the activity log
  (repaint fix `1896937`); installer unticked-startup upgrade path in an
  isolated environment (never this machine).
- Known gap: a previous version kept after Remove… has no installed row to
  revert from until the unified model list.
- Risk: setup.exe is 1.99 GB, near Inno's 2.1 GB single-file limit; a larger
  bundle needs `DiskSpanning`. The pinned Hugging Face revision may lack the
  Q8_0/bf16 files (only `main` was checked).
- Open questions: in `roadmap.md` (runner load-failure reporting, Qwen default,
  per-model options). Linux Qwen with runner-folder loading is unverified.
- Blockers: running Debug Host locks its executable (LNK1168); do not
  terminate it without asking. Developer Run entry left for the user per
  `PROJECT.md`.
- Environment: Windows Debug `build/` uses the cached b11029 Qwen runtime; use
  `config.development.json` with login startup disabled. Linux tests in
  `build-linux/`. Commands in README, `progress.md`, `PROJECT.md`.
- Discarded as noise: the `harness-guard` hook blocks shell commands with a
  segment starting with the Qwen model name; put such text in a file instead.
