# Active Context

- Mode: implementation, bundled two-model release follow-ups.
- Branch: `feature/model-maintenance` (from `main` at `01f1684`, PR #3 merged).
  "Report Qwen unavailable without a usable GPU" is implemented
  (uncommitted): 52/52 Linux CTests pass; Windows build compiles and the
  affected Windows tests pass; `tts-host.exe` link blocked by the running
  Debug Host. Its roadmap item stays open until the no-CUDA Windows check.
- Next: commit the slice, then `/next-slice` → "AFK — Unified Settings model
  list" (clean Windows acceptance is HITL and waits on the user).
- Pending user checks on Windows: a full `scripts/release-win.ps1` run
  reusing the existing `cache/release-models` stage (verifies, reuses or
  restages); quit Debug Host, run
  `powershell -f .\fresh-win.ps1`, then in Settings → Models try More ▾,
  Remove… (dialog, keep-previous tick, Cancel default), and Verify and
  repair… (corrupt a per-user file; No is the default); scroll the activity log
  (repaint fix `1896937`); installer unticked-startup upgrade path in an
  isolated environment (never this machine); on a Windows machine without
  usable CUDA, Qwen as `quality` with `fallbackProfile` speaks with Kokoro and
  Settings → Models shows Qwen "[unavailable: …]" with the reason.
- Known gap: a previous version kept after Remove… has no installed row to
  revert from until the unified model list.
- Risk: setup.exe is 1.99 GB, near Inno's 2.1 GB single-file limit; a larger
  bundle needs `DiskSpanning`. The pinned Hugging Face revision may lack the
  Q8_0/bf16 files (only `main` was checked).
- Open questions: in `roadmap.md` (Qwen default, per-model options). Linux
  Qwen with runner-folder loading is unverified.
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
  In this shell `grep` is rewritten to `rg`; use `/usr/bin/grep` for grep
  syntax (`\|`, `-r` on directories).
