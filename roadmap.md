# Roadmap

Each item is a thin, independently verifiable product slice. Requirements and
architecture live under `docs/`; this file records sequence only.

- [x] **AFK — Host starts and reads its configuration:** `tts-host --headless`
  launches from a native Windows build with no external runtime, loads and
  schema-validates `config.json`, reloads it on save, and reports configuration
  errors by JSON path while keeping the last valid document.
- [x] **AFK — Registry discovers a model package:** scan configured directories,
  validate `model.json` against the schema, reject escaping paths, and list
  discovered and unsupported packages through the CLI with actionable reasons.
- [x] **AFK — First audio through the runner protocol:** implement the JSON-RPC
  control channel and framed audio channel, ship the stub runner and the Kokoro
  runner, and synthesize text to a WAV response.
  - [x] JSON-RPC control channel (`Content-Length` framing) and framed binary
    audio channel.
  - [x] Stub runner: `initialize`/`synthesize` handshake and deterministic
    stub audio over the inherited audio pipe.
  - [x] Host launches runner subprocesses cross-platform (`RunnerSession`)
    and writes a real WAV file end to end against the stub runner.
  - [x] Runner selection by engine (`--model`, `tts-host-<engine>-runner`
    naming convention).
  - [x] ONNX Runtime vendored (v1.29.0, win-x64/linux-x64) and the toolchain
    proven with a smoketest.
  - [x] Real `tts-host-kokoro-onnx-runner` executable speaks the protocol
    against a placeholder ONNX model.
  - [x] `load` request wires a registry package's model path into the
    runner before synthesis.
  - [x] espeak-ng vendored and real text-to-phoneme input — see
    [design](docs/design/architecture.md#text-to-phoneme-espeak-ng) and
    [ADR 0006](docs/adr/0006-espeak-ng-vendoring-and-phoneme-mapping.md).
    - [x] Vendor espeak-ng for Windows (MSI fetch + extract via CMake) and add
      the Linux dev dependency to `scripts/setup-dev-env.sh`.
    - [x] Subprocess invocation producing IPA phonemes for arbitrary text.
    - [x] Map espeak-ng IPA output to Kokoro's phoneme vocabulary (table
      ported from misaki) and wire into `run_kokoro_synthesis`, replacing the
      hardcoded phoneme sequence.
  - [x] Real Kokoro-82M ONNX weights and voice embeddings replace the
    placeholder model.
  - [x] Fix the host/runner audio-pipe deadlock: both runners now write their
    control response before the audio frame, so the host is already draining
    the audio pipe by the time a large frame is written.
- [ ] **HITL — English model bake-off:** on the RTX 3070 Laptop GPU, compare
  Kokoro, Qwen3-TTS 0.6B/1.7B at candidate quantizations, and the strongest
  lightweight alternative against the acceptance criteria in
  `docs/requirements/product.md`; settle llama.cpp versus upstream PyTorch for
  the Qwen runner and select the fast and quality defaults.
  - [x] `stats` capability (peak RSS, peak VRAM, time-to-first-chunk, sample
    count) implemented end to end — runner protocol, Kokoro and stub runners,
    and a `tts-host --stats` CLI flag — per
    [ADR 0002](docs/adr/0002-runner-protocol.md), so the comparison doesn't
    need throwaway measurement tooling.
  - [ ] Measure Kokoro, Qwen3-TTS 0.6B/1.7B, and the strongest lightweight
    alternative on the RTX 3070 Laptop GPU against the acceptance criteria and
    settle the fast/quality defaults.
- [ ] **AFK — Model manager and settings window:** model/profile switching,
  load/unload, idle timeout, directory watching, import, catalogue download with
  progress and checksums, and licence display — through JSON and the first
  desktop UI, built against native platform APIs per
  [ADR 0007](docs/adr/0007-native-ui-per-platform.md). Windows first; Linux and
  macOS are separate later slices.
  - [x] Windows tray icon: running `tts-host` without `--headless` shows a
    Shell_NotifyIcon tray icon with a right-click context menu (`tray_icon.hpp/cpp`)
    and blocks on a message loop until Quit is chosen; non-Windows throws a
    clear not-implemented error. No model switching or other menu items yet —
    see [design](docs/design/architecture.md#desktop-integration).
  - [x] Tray "Settings…" menu item: opens the settings window
    (`run_settings_window`) from the tray, blocking the tray's own message
    loop until it closes — no threading yet, so the tray icon stops
    responding to clicks while settings is open. `run_settings_window` now
    tolerates being called more than once per process
    (`ERROR_CLASS_ALREADY_EXISTS`), which repeated tray clicks require.
  - [x] Default model selection from `profiles`/`languageDefaults`: synthesis
    without `--model`/`--runner` selects the configured default model and its
    engine runner instead of falling back to the test-only stub runner
    (`src/main.cpp` `resolve_runner_selection`) — see
    [audit](docs/reviews/2026-08-31-release-packaging-audit.md#triage) row 1.
  - [x] Windows settings-window shell: `tts-host --settings` opens a plain
    native window and blocks until closed (`settings_window.hpp/cpp`),
    independent of the tray; non-Windows throws a clear not-implemented
    error. No config-editing controls or model manager yet — see
    [design](docs/design/architecture.md#desktop-integration).
  - [x] Output-device control: the settings window lists active WASAPI
    render endpoints (`list_output_devices` in `playback_sink.hpp/cpp`) plus
    "System Default" in a combo box, preselects `audio.outputDevice`, and
    writes the selection back to `config.json` on change — see
    [design](docs/design/architecture.md#desktop-integration) and
    [requirements](docs/requirements/product.md#configuration-and-controls).
    The host-side live-reload file watcher described in
    [design](docs/design/architecture.md#live-reload) is not built yet, so a
    running tray/settings session won't pick up the change until restarted.
    Model manager and hotkeys controls remain.
  - [x] Server host/port controls: the settings window adds host and port
    edit boxes (`kServerHostEditId`/`kServerPortEditId` in
    `settings_window.cpp`), preselects `server.host`/`server.port`, and
    writes changes back to `config.json` on focus loss (invalid or empty
    values are discarded, keeping the last valid one). A static label states
    that a restart is required — see
    [design](docs/design/architecture.md#live-reload) and
    [requirements](docs/requirements/product.md#configuration-and-controls).
    The window does not enforce the restart itself. Model manager and
    hotkeys controls remain.
  - [x] Installed-model status and licence display: the settings window lists
    each compatible package's name, id, licence, and licence URL, and reports
    unsupported or incomplete package paths with their actionable registry
    reason. The display is read-only; selection, loading, and downloading
    remain model-manager work.
- [ ] **AFK — Everyday desktop playback:** tray controls, host-side playback,
  chunked streaming, interrupt and queue semantics, markdown/HTML normalization,
  output-device selection, CLI text/stdin/clipboard support, and start-at-login.
  - [x] Markdown normalization in the host, applied to every synthesis request
    before it reaches a runner — see
    [design](docs/design/architecture.md#speech-pipeline).
  - [x] HTML normalization: the same five rules over a different syntax.
  - [x] CLI text/stdin/clipboard support: `--stdin` and `--clipboard` as
    alternative text sources to `--synthesize`, feeding the same
    `synthesize_to_wav` path.
  - [x] Host-side playback: `--play` plays synthesized audio through the
    system default output device (`PlaybackSink`/`SystemPlaybackSink`,
    WASAPI). Windows only in this slice; other platforms throw a clear error
    — see [design](docs/design/architecture.md#speech-pipeline). No
    interrupt/queue semantics yet.
  - [x] Output-device selection: config's `audio.outputDevice` reaches
    `SystemPlaybackSink` end to end — `"system-default"` keeps the OS default
    device, any other value pins playback to the WASAPI endpoint whose
    friendly name matches, per
    [requirements](docs/requirements/product.md#configuration-and-controls).
    No live default-device-change following yet.
  - [x] Chunked streaming: the host splits normalized text into
    sentence-scale chunks (`text_splitter.hpp/cpp`) and synthesizes each as a
    separate back-to-back request within one runner session, playing each
    chunk as soon as it is ready rather than after the whole text finishes —
    see [design](docs/design/architecture.md#speech-pipeline).
  - [x] Lookahead overlap: each chunk plays on a background thread
    (`src/main.cpp`, joined before the next chunk starts playing) so the next
    chunk's runner round trip happens while the current one plays instead of
    after, per the "small lookahead" in
    [design](docs/design/architecture.md#speech-pipeline). Playback errors
    (including the non-Windows not-implemented error) are captured on the
    background thread and rethrown on the main thread at the next join point,
    preserving prior CLI error behavior — proven by the existing
    `tts_host_play_not_implemented_on_this_platform` and
    `tts_host_play_forwards_pinned_output_device` CTests, unchanged. Real
    overlap timing needs a native Windows manual check (no audio hardware in
    WSL). No interrupt/queue semantics yet.
- [ ] **HITL — Windows selection reading:** validate UI Automation and protected
  clipboard fallback in OneNote, Windows Terminal, browsers, and representative
  editors before freezing default global shortcuts.
- [ ] **AFK — Browser reading client:** Chrome extension with selection and
  article reading, playback controls, and the CORS origin allowlist that admits
  it.
- [ ] **HITL — Russian and Armenian acceptance:** compare Qwen Russian and the
  offline Armenian candidate with native-speaker samples; confirm best-effort
  quality is adequate and that English defaults are unaffected.
- [ ] **AFK — Cross-platform releases and installer:** native Linux and Apple
  Silicon macOS artifacts plus a Windows installer, using the same
  configuration, manifests, API, and conformance tests. Findings, triage, and a
  proposed sequence are in the
  [release packaging audit](docs/reviews/2026-08-31-release-packaging-audit.md);
  its release-root architecture is a recommendation, so adopting it needs an ADR
  and the `runners/`/`runtimes/` layout decided first.

## Open decisions

- Upstream PyTorch versus llama.cpp for the Qwen quality runner, and the
  quantization level — an output of the bake-off, not an input.
- Whether a WSL headless distribution is published, or `--headless` merely makes
  it possible.

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
