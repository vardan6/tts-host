# Active Context

- Mode: release preparation on `release-prep/v0.1.0-alpha.1`; release
  variant, script, defaults, and the extension in the release tree are
  committed (2026-11-03).
- Phase: "First public alpha (Windows)" in `roadmap.md`, a draft
  standard-only `v0.1.0-alpha.1`.
- Next atomic step: `/next-slice` → the Settings version line.
- Next after that: a Windows CTest run on a clean checkout, then PR → `main`
  and the HITL dry run.
- Verification: Linux suite passes except `config_persistence` and
  `unicode_cli_paths`, which fail only on the user-edited example and pass on
  the committed one. Windows standard variant, with `extension/`, built and
  spoke from its extracted zip (`build-release/standard-check`, log in `logs/`). Windows
  CTest, the NVIDIA variant, and `release.ps1` itself are not yet run.
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
