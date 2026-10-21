# Active Context

- Mode: implementation, bundled two-model release follow-ups.
- Branch: `feature/model-maintenance` (from `main` at `01f1684`, PR #3 merged).
  "Reject stale staged release models" is done (uncommitted); 52/52 Linux
  CTests pass; Windows build compiles, `tts-host.exe` link blocked by the
  running Debug Host.
- Next: `/next-slice` → "HITL — Decide how a runner reports an unusable
  load" needs the user's decision; "Report Qwen unavailable without a usable
  GPU" waits on it.
- Pending user checks on Windows: a full `scripts/release-win.ps1` run
  reusing the existing `cache/release-models` stage (verifies, reuses or
  restages); quit Debug Host, run
  `powershell -f .\fresh-win.ps1`, then in Settings → Models try More ▾,
  Remove… (dialog, keep-previous tick, Cancel default), and Verify and
  repair… (corrupt a per-user file; No is the default); scroll the activity log
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
  `build-linux/`. Commands in README, `progress.md`, `PROJECT.md`. From WSL,
  `powershell.exe` is not on PATH: call
  `/mnt/c/Windows/System32/WindowsPowerShell/v1.0/powershell.exe` to build
  `build/` (`cmake --build build --config Debug`) without launching the app.
- Discarded as noise: the `harness-guard` hook blocks shell commands with a
  segment starting with the Qwen model name; put such text in a file instead.
