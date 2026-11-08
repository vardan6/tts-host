# Active Context

- Mode: release preparation on `release-prep/v0.1.0-alpha.1`; all alpha
  implementation slices committed (2026-11-03); the Windows test gate passes
  (2026-11-04); standard dry run passed (2026-11-08).
- Phase: "First public alpha (Windows)" in `roadmap.md`, a draft
  standard-only `v0.1.0-alpha.1`.
- Next atomic step: `/cross-review` of `main..HEAD` in another provider's
  session, then `/review-triage`.
- Next after: PR → `main`, then the real run
  `scripts/release.ps1 -Version 0.1.0-alpha.1 -Variants standard` from `main`.
- Verification: dry run 3 (`1e09659`) passed end to end: release commit,
  Windows CTest 55/55, setup exe and portable zip in
  `build-release/dist/dry-run/`; no tag or ref kept.
  Linux suite passes except `config_persistence` and `unicode_cli_paths`,
  which fail only on the user-edited example.
- Blockers/environment: a release run needs a clean tree (stash the user's
  `config.example.json` edits before the real run, pop after), Windows VS 2022,
  Inno Setup 6, and authenticated `gh`. Never `git add -A` or `commit -a`
  while the user's `config.example.json` edits are in the working copy: that
  commits them over the release defaults. Windows builds run from WSL through
  `/mnt/c/Windows/System32/WindowsPowerShell/v1.0/powershell.exe`; use a
  separate build folder. Do not stop the running Host without permission or
  overwrite its locked exe. Developer launches use `config.development.json`
  with startup disabled; follow `PROJECT.md`. Linux build: `build-linux/`;
  model listing: `--headless`.
- Open questions: whether `.gitignore` should also ignore
  `config.example.json.last-valid` (the app's save backup; one was found
  untracked and removed). Otherwise none for the alpha; deferred product choices remain in
  `roadmap.md`. Linux Qwen runner-folder loading is unverified; previous-only
  restoration is undesigned.
- Discarded as noise: Linux `config_persistence` and `unicode_cli_paths`
  failures come from the user-edited example (both pass on the committed
  one); personal Kokoro selection does not establish shipping defaults.
  `tts_host_model_update` is not a leaked handle: a scanner locks fresh
  files in `%TEMP%` (probe: `%TEMP%\tts-rename-probe.ps1`). MSVC's MSB8029
  warning about building under `%TEMP%` is harmless for the gate.
