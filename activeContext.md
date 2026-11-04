# Active Context

- Mode: release preparation on `release-prep/v0.1.0-alpha.1`; all alpha
  implementation slices committed (2026-11-03); the Windows test gate passes (2026-11-04).
- Phase: "First public alpha (Windows)" in `roadmap.md`, a draft
  standard-only `v0.1.0-alpha.1`.
- Next atomic step: `/cross-review` of `main..HEAD` in another provider's
  session (the user launches it), then `/review-triage`, PR → `main`, then
  the HITL dry run.
- Verification: Linux suite passes except `config_persistence` and
  `unicode_cli_paths`, which fail only on the user-edited example. Windows
  standard variant, with `extension/`, built and spoke from its extracted
  zip; Settings title shows the version. Windows CTest in the clean worktree
  (`62de181` plus the `tts_host_model_update` fixture fix): 55/55; that test
  alone 40/40 passing, from 0/40 before the fix.
- Blockers/environment: a release run needs a clean tree, Windows VS 2022,
  Inno Setup 6, and authenticated `gh`. Never `git add -A` or `commit -a`
  while the user's `config.example.json` edits are in the working copy: that
  commits them over the release defaults. Windows builds run from WSL through
  `/mnt/c/Windows/System32/WindowsPowerShell/v1.0/powershell.exe`; use a
  separate build folder. Do not stop the running Host without permission or
  overwrite its locked exe. Developer launches use `config.development.json`
  with startup disabled; follow `PROJECT.md`. Linux build: `build-linux/`;
  model listing: `--headless`.
- Open questions: none for the alpha; deferred product choices remain in
  `roadmap.md`. Linux Qwen runner-folder loading is unverified; previous-only
  restoration is undesigned.
- Discarded as noise: Linux `config_persistence` and `unicode_cli_paths`
  failures come from the user-edited example (both pass on the committed
  one); personal Kokoro selection does not establish shipping defaults.
