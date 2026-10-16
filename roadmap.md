# Roadmap

Open work only, in order. Requirements and architecture live under `docs/`;
completed slices are recorded in `progress.md` and git history.

Current state: the Windows desktop prototype and both branch reviews
([October 3](docs/reviews/code-review-2026-10-03-roadmap-slices-branch.md#triage--2026-10-03),
[October 14](docs/reviews/code-review-2026-10-14-checkpoint.md#triage--2026-10-14))
are complete and merged into `main` (PR #2). Windows acceptance is complete on user confirmation (October 16).

Native checks that require the user are collected for combined acceptance and
do not block independent slices; never mark them passed from a build alone.
Synthetic Copy adapters require technical compatibility evidence, not a separate
user approval. Platform ports are later work, Linux before macOS per
[ADR 0007](docs/adr/0007-native-ui-per-platform.md).

## Windows acceptance

- [x] Combined Windows desktop acceptance — closed on user confirmation;
  Ctrl+F8 retained and recorded UI findings accepted.
- [x] Windows start-at-login acceptance — closed on user confirmation; agent
  did not perform isolated-machine testing.
- [x] Audible boundary gap — measured with the user; no audible artifacts reported.

Evidence, measurements, and final closure are in the
[October 16 acceptance report](docs/reviews/windows-acceptance-2026-10-16.md).
Synthetic Copy adapter compatibility remains separate open work below.

## Product

- [x] **AFK — Qwen end-to-end synthesis (evaluation):** local package discovery,
  reference voice, resident synthesis, cancellation and recovery verified on
  Windows and Linux with the provisional configuration — see
  [engine design](docs/design/architecture.md#engines-and-models) and
  [package design](docs/design/architecture.md#model-packages-and-discovery).
- [ ] **HITL — English model bake-off:** after Qwen synthesis integration,
  measure Kokoro, Qwen3-TTS 0.6B/1.7B at candidate quantizations, and the strongest
  lightweight alternative through the Host on the RTX 3070 Laptop GPU against
  the [acceptance criteria](docs/requirements/product.md#english-bake-off-corpus).
  Extend candidate runner support and obtain reliable peak VRAM measurements
  (the evaluation runner currently reports unavailable); compare llama.cpp versus
  upstream PyTorch before selecting the final Qwen variant/runtime and fast/quality
  defaults. `--stats` and the frozen corpus already exist — see
  [engine design](docs/design/architecture.md#engines-and-models).
  - [ ] **AFK — Qwen catalogue and local import:** add the selected Qwen package
    to the pinned catalogue and verify both Settings import and catalogue
    installation produce a selectable, speaking model — see
    [catalogue design](docs/design/architecture.md#download-catalogue) and
    [distribution requirements](docs/requirements/product.md#distribution-and-usability).
  - [ ] **AFK — Qwen speech-speed control:** after selecting the release runner,
    support the typed 0.5–2.0 synthesis multiplier and sentence-boundary speed
    changes without pitch-changing resampling; the evaluation runner currently
    accepts only 1.0 — see
    [playback design](docs/design/architecture.md#playback-control-and-position).
  - [ ] **HITL — Decide model revision and replacement storage:** capture the
    content-revision representation, installed/portable storage and discovery
    precedence, and runner/runtime release layout before replacement or
    installer work — see
    [update design](docs/design/architecture.md#model-installation-and-updates)
    and [distribution design](docs/design/architecture.md#distribution).
  - [ ] **AFK — Manual catalogue model update:** expose current/update status
    and user-triggered staged replacement, preserving the working package on
    failure and recovering interrupted commits; verify resident-model handling
    and stable profile references — see
    [update design](docs/design/architecture.md#model-installation-and-updates).
  - [ ] **AFK — Two-model release package:** after the release-layout decision,
    package Kokoro, selected Qwen, all voice/tokenizer assets and runtimes;
    fail packaging if required files are absent and verify both models speak
    from a clean extracted artifact with networking disabled — see
    [distribution design](docs/design/architecture.md#distribution).
  - [ ] **AFK — Offline Windows installer:** after the two-model artifact and
    storage decision, install both models with no network or developer runtime;
    preserve model updates and added packages across application upgrades — see
    [distribution requirements](docs/requirements/product.md#distribution-and-usability)
    and [distribution design](docs/design/architecture.md#distribution).
  - [ ] **HITL — Clean Windows two-model acceptance:** verify offline install,
    both models speaking, model selection, local/catalogue additions, and a
    manual update on supported hardware — see
    [distribution requirements](docs/requirements/product.md#distribution-and-usability).
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
- Windows installer and release-layout work is tracked under the English
  model bake-off above; do not duplicate its implementation slices.

## Open decisions

- Upstream PyTorch versus llama.cpp for the Qwen quality runner, and the
  quantization level — an output of the bake-off, not an input.
- Whether a WSL headless distribution is published, or `--headless` merely makes
  it possible.
- Whether played-text history may retain spoken text.
- Release layout (`runners/`/`runtimes/`), installed/portable model storage and
  duplicate-ID precedence, plus package content revisions before model updates
  and the installer.

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
