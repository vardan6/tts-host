# Active Context

- Mode: implementation; gap-free utterance stream AFK slices done.
- Phase: review/finalize `roadmap-slices` before the Linux adapter.
- State: per-utterance stream (`71d4657`) and Playback tab tuning committed.
- Next action: HITL — with the user's permission, rerun the audible
  `tts-host-playback-gap-probe` (now has a `stream` condition) and record it
  under [finding 16](docs/reviews/code-review-2026-10-03-roadmap-slices-branch.md#finding-16).
  Pending native checks: tab layout incl. new Playback controls, first-run
  dialog appearance; isolated-environment sign-in acceptance.
- Verification: 46/46 CTests on Windows and Linux (`build-linux` boost override
  now points at `cache/fetchcontent/windows-vs2022/boost_headers-src`). Resident reuse, warm-up cancellation, shared Settings
  lifetime, desktop fatal dialog, and Unicode spool cleanup covered. Native
  audible/browser acceptance and Settings-to-Now-Playing skip check remain;
  Now Playing visuals accepted. Real-model performance measurement remains pending.
- Blockers/env: never change developer login startup (`PROJECT.md`); launch with
  `config.development.json`. Quit Host before replacing its binary. Managed
  Chrome disallows Developer mode; use the user's permitted browser.
  Never confirm the first-run dialog with yes on this machine; `compile-win.ps1`
  pre-creates `%LOCALAPPDATA%\TTS Host\startup-choice.marker`.
- Open questions: bake-off execution, Qwen runtime
  and quantization, release layout. Model manager currently has one resident slot;
  eager priority and lifecycle are in [design](docs/design/architecture.md#desktop-integration).
- Discarded as noise: native UI lookups must use process id; invoke full Windows
  PowerShell path from WSL. No #14 spool transaction fix without a recovery use
  case, no Chrome policy bypass retries. ONNX DLL resolution is a separate fix.
  Gap probe: broadband loopback detection is unreliable with other system audio;
  use the 440 Hz detector. Rerunning the probe plays audible tones, so ask first.
