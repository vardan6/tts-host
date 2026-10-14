# Roadmap

Open work only, in order. Requirements and architecture live under `docs/`;
completed slices are recorded in `progress.md` and git history.

Current state: the Windows desktop prototype and both branch reviews
([October 3](docs/reviews/code-review-2026-10-03-roadmap-slices-branch.md#triage--2026-10-03),
[October 14](docs/reviews/code-review-2026-10-14-checkpoint.md#triage--2026-10-14))
are complete and merged into `main` (PR #2). Native acceptance is still open.

Native checks that require the user are collected for combined acceptance and
do not block independent slices; never mark them passed from a build alone.
Synthetic Copy adapters require technical compatibility evidence, not a separate
user approval. Platform ports are later work, Linux before macOS per
[ADR 0007](docs/adr/0007-native-ui-per-platform.md).

## Windows acceptance

- [ ] **HITL — Combined Windows desktop acceptance:** one short native
  walkthrough — see [design](docs/design/architecture.md#desktop-integration)
  and [design](docs/design/architecture.md#playback-control-and-position).
  Explain unsupported cases, collect UI feedback, and freeze default shortcuts.
  - [ ] Selected/empty text in Windows Terminal/Codex, an editor's embedded
    terminal, OneNote, and a browser; correct source targeting; any enabled
    Copy adapters.
  - [ ] Audible pause/resume, Stop clearing the queue, close-to-hide, seek
    positions and generated boundaries, Settings-to-Now-Playing skip.
  - [ ] Now Playing **Read selection** of a real external target; visual
    acceptance of the taller window with read controls.
  - [ ] Global shortcut toggle: checked tray menu, suspend/resume, and Settings
    round trip.
  - [ ] Live external `config.json` edit picked up by the running tray
    ([design](docs/design/architecture.md#live-reload)).
  - [ ] Live catalogue download in Settings, including Activity progress.
  - [ ] Browser: article reading context-menu action reaches the Host; control
    page state updates, pause/resume/stop, reconnect, and close-without-stop
    ([design](docs/design/architecture.md#local-api)).
  - [ ] First-run question and tab visuals; tray and application icon appearance.
- [ ] **HITL — Windows start-at-login acceptance:** Settings toggle,
  system-page round trip, and sign-in in an isolated Windows environment; do
  not enable login startup on the developer machine — see
  [design](docs/design/architecture.md#login-startup) and
  [ADR 0008](docs/adr/0008-os-owned-login-startup.md).
- [ ] **HITL — Re-measure the boundary gap:** rerun the audible
  `tts-host-playback-gap-probe` after the per-utterance stream change, with the
  user's permission — see
  [design](docs/design/architecture.md#utterance-audio-stream).

## Product

- [ ] **HITL — English model bake-off:** on the RTX 3070 Laptop GPU, measure
  Kokoro, Qwen3-TTS 0.6B/1.7B at candidate quantizations, and the strongest
  lightweight alternative against the
  [acceptance criteria](docs/requirements/product.md#english-bake-off-corpus);
  settle llama.cpp versus upstream PyTorch for the Qwen runner and select the
  fast and quality defaults. `--stats` and the frozen corpus already exist.
- [ ] **AFK — Synthetic Copy compatibility verification:** validate
  focused-control identification, modifier handling, and clipboard
  ownership/preservation for each adapter before enabling it. Unknown targets
  stay manual-copy-only — see
  [design](docs/design/architecture.md#desktop-integration).
- [ ] **HITL — Plan played-text history and visible queue:** user idea
  (2026-10-14): list played/playing reads, step back/forward and replay a
  chosen one, and show the queue. Needs `/planning-capture` first; a history
  holds spoken text, which the Activity record deliberately never does.
  Queued requests already carry a source label (`SpeechRequest` in
  `src/main.cpp`).
- [ ] **HITL — Russian and Armenian acceptance:** compare Qwen Russian and the
  offline Armenian candidate with native-speaker samples; confirm best-effort
  quality is adequate and that English defaults are unaffected.

## Cross-platform releases

Native Linux and Apple Silicon macOS artifacts plus a Windows installer, using
the same configuration, manifests, API, and conformance tests. Findings and a
proposed sequence are in the
[release packaging audit](docs/reviews/2026-08-31-release-packaging-audit.md);
its release-root architecture is a recommendation, so adopting it needs an ADR
and the `runners/`/`runtimes/` layout decided first.

- [ ] **AFK — Native Linux selection adapter:** KDE Plasma Wayland direct
  accessibility capture and actionable unavailable-capability reporting; use
  Kubuntu for initial desktop acceptance and retain Rocky 8/9 KDE as
  compatibility targets. Never assume Windows active-window or injection
  behavior — see [design](docs/design/architecture.md#desktop-integration)
  and [design](docs/design/architecture.md#platform-strategy).
- [ ] **HITL — Linux KDE Wayland acceptance:** focused/empty/protected
  selections and unsupported applications on Kubuntu; compatibility on the
  user's Rocky 8/9 KDE sessions — see
  [design](docs/design/architecture.md#platform-strategy).
- [ ] **AFK — Linux X11 selection compatibility:** native X11-session capture
  after the KDE Wayland adapter; fail closed for unavailable targets — see
  [design](docs/design/architecture.md#platform-strategy).
- [ ] **AFK — Native macOS selection adapter:** macOS accessibility
  permission/status and focused-selection capture; no synthetic Windows-style
  copy fallback — see [design](docs/design/architecture.md#desktop-integration)
  and [design](docs/design/architecture.md#platform-strategy).
- [ ] **AFK — Linux and macOS login startup:** XDG Autostart (Background
  portal under Flatpak) and `SMAppService.mainApp`, with the same Settings
  states as Windows — see [design](docs/design/architecture.md#login-startup).
- [ ] **AFK — Windows installer and release layout.**

## Open decisions

- How to execute the English bake-off before candidate integration: use a
  consistent external benchmark harness or implement TTS Host runners for the
  candidates first. Qwen and LuxTTS were evaluated through external pilot paths;
  neither is integrated in TTS Host.
- Upstream PyTorch versus llama.cpp for the Qwen quality runner, and the
  quantization level — an output of the bake-off, not an input.
- Whether a WSL headless distribution is published, or `--headless` merely makes
  it possible.
- Whether played-text history may retain spoken text.
- Release layout (`runners/`/`runtimes/`), before the installer and ports.

## Deliberately deferred

- CI pipeline (GitHub Actions or equivalent) running the CTest suite,
  including the stub end-to-end test. Local CTest already covers this; revisit
  once the application works end-to-end with real inference and is worth
  protecting from regressions.
- Remote and cloud providers. Revisit only if local quality proves inadequate.
- Full text normalization (numbers, dates, currency, abbreviations).
- Token authentication and LAN serving. The config vocabulary is reserved;
  see `docs/adr/0003-no-authentication.md`.
- Self-updating.
- Spool recovery (October 3 finding 14): rejected without a use case — see
  [validation](docs/reviews/code-review-2026-10-03-roadmap-slices-branch.md#finding-14).
