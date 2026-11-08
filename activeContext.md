# Active Context

- Mode/phase: first public Windows alpha preparation on
  `release-prep/v0.1.0-alpha.1`.
- State: the cross-provider review of `b4ce040..48bcf32`
  ([findings](docs/reviews/code-review-2026-11-08-release-preparation.md)) is
  triaged and all three findings are fixed (2026-11-08): no repair download
  without the runner, no fetch on a dry run, and the
  `tts_host_release_script` Windows CTest.
- Next atomic step: PR `release-prep/v0.1.0-alpha.1` → `main`.
- Next after: real standard-only alpha run from `main`, isolated Windows
  acceptance, user publication.
- Verification: dry run 4 (`8edb990`) passed: Windows CTest 56/56 including
  `tts_host_release_script`, setup exe and portable zip built. Linux model-list and model-update tests pass; the new
  model-list case fails with the fix reverted. `tests/release_script_tests.ps1`
  passes on Windows and fails its dry-run checks with the old fetch restored.
  Linux `local_api` fails only because WSL loopback connects are refused right
  now (a plain Python loopback connect fails the same way).
- Blockers/env: user's `config.example.json` edits remain uncommitted; never
  `git add -A` or `commit -a`.
  Release requires a clean tree, Windows VS 2022, Inno Setup 6, authenticated
  `gh`; Windows commands from WSL use
  `/mnt/c/Windows/System32/WindowsPowerShell/v1.0/powershell.exe`. Linux build:
  `build-linux/`. Do not stop the running Host without permission or overwrite
  its locked exe. Developer launches use `config.development.json` with startup
  disabled; follow `PROJECT.md`.
- Open questions: whether to ignore `config.example.json.last-valid` remains undecided.
  Deferred product decisions remain in `roadmap.md`; Linux Qwen runner-folder
  loading is unverified and previous-only restoration is undesigned.
- Discarded as noise: missing-command PowerShell hypothesis was rejected by
  a probe; personal-example Linux persistence/Unicode failures do not describe
  committed defaults. Scanner-lock investigation and earlier dry-run failures
  are recorded in `progress.md`; no need to repeat them.
