# Active Context

- Mode: implementation; Windows start-at-login AFK slices done.
- Phase: review/finalize `roadmap-slices` before the Linux adapter.
- State: Settings tabs, OS-owned startup switch, and first-run question
  committed (`cd9e592`, `2894c0b`, `93025fc`); ADR 0008 and design docs
  (incl. 2026-10-07 login-startup detail update) remain uncommitted.
- Next action: `/next-slice` → **Per-utterance stream**
  ([utterance stream](docs/design/architecture.md#utterance-audio-stream)),
  then Playback tab tuning. Pending native checks: tab layout and first-run
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
