# Roadmap

Each item is a thin, independently verifiable product slice. Requirements and
architecture live under `docs/`; this file records sequence only.

Current checkpoint: next work is [Branch review follow-up → Next](#next)
(download progress coalescing); finalize
`roadmap-slices` review afterward. Pending acceptance and later product slices
remain unchecked; the premature PR #1 merge recorded in `activeContext.md` does
not declare them complete. Linux remains the next platform.

Desktop prototype order: Windows direct selection → Now Playing pause/resume/
stop → generated-audio seek → browser selection → one combined Windows acceptance
session. Each implementation slice includes automated checks; intermediate demos
are optional feedback, not approval gates. Native checks that require the user
are collected for combined acceptance and do not block independent slices.
Synthetic Copy adapters require technical compatibility evidence, not a separate
user approval. Unsupported adapters stay disabled without blocking playback or
browser work. Platform ports remain later work, Linux before macOS per ADR 0007.

Developer workflow:

- [x] **AFK — Incremental Windows build and run loop:** keep separate `build/`
  and `build-linux/` trees; make `compile-win.ps1` configure when needed, then
  incrementally build, run CTest, and launch the desktop app — see
  [requirements](docs/requirements/developer-workflow.md) and
  [design](docs/design/developer-workflow.md#incremental-windows-build-and-run).
- [x] **AFK — Explicit Windows clean and fresh operations:** expose a generated
  clean target and a `fresh-win.ps1` full build that preserves the separate
  FetchContent cache unless explicitly reset — see
  [requirements](docs/requirements/developer-workflow.md) and
  [design](docs/design/developer-workflow.md#clean-and-fresh-semantics).

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
  - [x] Freeze the technical and long-form English benchmark passages in the
    [acceptance criteria](docs/requirements/product.md#english-bake-off-corpus),
    so every candidate has the same measurement and listening corpus.
  - [ ] Measure Kokoro, Qwen3-TTS 0.6B/1.7B, and the strongest lightweight
    alternative on the RTX 3070 Laptop GPU against the acceptance criteria and
    settle the fast/quality defaults.
- [ ] **AFK — Model manager and settings window:** model/profile switching,
  load/unload, idle timeout, directory watching, import, catalogue download with
  progress and checksums, licence display, and hotkey controls after selection
  capture is settled — through JSON and the first
  desktop UI, built against native platform APIs per
  [ADR 0007](docs/adr/0007-native-ui-per-platform.md). Windows first; Linux and
  macOS are separate later slices.
  - [x] Windows tray icon: running `tts-host` without `--headless` shows a
    Shell_NotifyIcon tray icon with a right-click context menu (`tray_icon.hpp/cpp`)
    and blocks on a message loop until Quit is chosen; non-Windows throws a
    clear not-implemented error — see
    [design](docs/design/architecture.md#desktop-integration).
  - [ ] TTS document/speaker icon: embedded application and tray artwork is
    implemented; verify its appearance after a native Windows rebuild.
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
  - [x] Preferred-profile fallback: a profile can name `fallbackProfile`; when
    its model cannot be resolved or its runner is unavailable, synthesis uses
    the named profile and reports why. English now tries `quality` before
    CPU-only `fast` (Kokoro), so the fallback path is usable before a GPU runner
    is selected by the bake-off.
  - [x] Request language selection: `language_selection.hpp/cpp` resolves a
    request's language from an explicit `--language` tag, else the text's
    script (Latin → `en`, Cyrillic → `ru`, Armenian → `hy`, dominant script
    wins, ties and unconfigured languages fall back), else `en`; that language
    names the `languageDefaults` profile whose model and engine runner serve
    the request, replacing the hardcoded `languageDefaults.en` lookup in
    `src/main.cpp`. Normalization now runs before runner selection so
    detection sees speakable text. Headless synthesis prints the chosen
    language, its source, the profile, and the model — see
    [design](docs/design/architecture.md#speech-pipeline) and
    [requirements](docs/requirements/product.md#speech-behaviour). The
    Settings loads models by id; tray speech follows the request profile.
  - [x] Windows settings-window shell: `tts-host --settings` opens a plain
    native window and blocks until closed (`settings_window.hpp/cpp`),
    independent of the tray; non-Windows throws a clear not-implemented
    error — see
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
  - [x] Server host/port controls: the settings window adds host and port
    edit boxes (`kServerHostEditId`/`kServerPortEditId` in
    `settings_window.cpp`), preselects `server.host`/`server.port`, and
    writes changes back to `config.json` on focus loss (invalid or empty
    values are discarded, keeping the last valid one). A static label states
    that a restart is required — see
    [design](docs/design/architecture.md#live-reload) and
    [requirements](docs/requirements/product.md#configuration-and-controls).
    The window does not enforce the restart itself.
  - [x] Installed-model status and licence display: the settings window lists
    each compatible package's name, id, licence, and licence URL, and reports
    unsupported or incomplete package paths with their actionable registry
    reason. The display is read-only.
  - [x] Default-English-profile control: the settings window adds a combo box
    (`kDefaultProfileComboId` in `settings_window.cpp`) listing every
    `profiles` key, preselects `languageDefaults.en`, and writes the selection
    back to `config.json` on change — see
    [design](docs/design/architecture.md#desktop-integration) and
    [requirements](docs/requirements/product.md#configuration-and-controls). A
    static label states that a restart is required.
  - [x] Model load/unload: `ModelSessionManager` (`model_session.hpp/cpp`)
    keeps at most one model resident in a live runner process — load resolves
    the package, launches its engine runner, and completes the
    initialize/load handshake; unload terminates that process, which is what
    frees the weights with one runner process per model.
    The settings window drives it through a model combo box, Load/Unload
    buttons, and a status line; `run_settings_window`/`run_tray_icon` now
    take the runner directory. Residency lasts only as long as the owning
    process, so CLI synthesis still launches its own runner — see
    [design](docs/design/architecture.md#desktop-integration) and
    [requirements](docs/requirements/product.md#configuration-and-controls).
  - [x] Runner-protocol `unload` request: the method
    [ADR 0002](docs/adr/0002-runner-protocol.md) lists among the initialize
    capabilities now has a wire contract (`RunnerUnloadRequest`/`Response`, no
    params — one model per runner process) and is implemented by both runners;
    the Kokoro runner releases its ONNX session and voice table, so the process
    stays alive and reloadable. `ModelSessionManager::unload` sends it
    best-effort before terminating the runner.
  - [x] Model package import: `import_model_package` (`model_import.hpp/cpp`)
    copies a package directory into the first configured
    `modelRegistry.directories` entry, reusing the registry's own validation so
    invalid, incomplete (declared `files.*` missing), duplicate-id, and
    already-installed packages are refused with the reason `--list-models`
    would print, and nothing is copied unless the staged copy validates in its
    new location — see
    [design](docs/design/architecture.md#model-packages-and-discovery) and
    [requirements](docs/requirements/product.md#models). Driven by
    `tts-host --headless --import-model <path>` and the settings window's
    Import… button (`kImportModelButtonId`, an `IFileOpenDialog` folder
    picker), which refreshes the model combo and licence display. A copied
    manifest's relative `$schema` would not resolve at the new depth, so import
    repoints it at the installation's own `schemas/model.schema.json`.
  - [x] Idle timeout: unload the resident model after actual speech activity
    ends, with a global timeout and optional profile override; active/paused
    speech stays resident and the tray polls even with Settings closed — see
    [design](docs/design/architecture.md#desktop-integration).
  - [x] Directory watching: `registry_scan_changed` (`model_registry.hpp/cpp`)
    compares two registry scans by discovered package id/path and unsupported
    entry path/reason; when `modelRegistry.watchForChanges` is true, the
    settings window polls `modelRegistry.directories` every 5 s via a second
    `WM_TIMER` (same mechanism as idle timeout) and refreshes the model
    combo/licence display only when the scan actually changed, so a package
    dropped in by something other than Import… still shows up without
    restarting and without resetting the combo selection on every poll — see
    [design](docs/design/architecture.md#desktop-integration).
  - [x] Download catalogue: `model_catalogue.hpp/cpp` holds the curated set
    compiled into the binary — no runtime fetch, so it adds no trust root
    beyond the binary itself — with each entry's licence, per-file HTTPS URL,
    pinned SHA-256, and size. `validate_catalogue` refuses an entry with no
    licence to disclose, a non-HTTPS URL, an unpinned or malformed checksum, a
    zero size, a duplicate id, or a file path escaping the package root, and
    the catalogue's own test applies it to the shipped entries.
    `catalogue_entry_installed` matches an entry id against a registry scan.
    `tts-host --headless --list-catalogue` prints each entry with licence
    (non-commercial badged), download size, and `GET`/`HAVE` — see
    [design](docs/design/architecture.md#download-catalogue) and
    [requirements](docs/requirements/product.md#distribution-and-usability).
    Kokoro-82M is the only entry: it is bundled, so this is how the package is
    restored, and its URLs and checksums are the ones
    `tools/fetch_kokoro_weights.py` already fetches. Qwen3-TTS, LuxTTS, and MMS
    Armenian wait on the bake-off choosing their artifacts.
  - [x] Catalogue download UI: the settings window adds a Catalogue combo box
    (`kCatalogueComboId` in `settings_window.cpp`) listing every compiled-in
    entry with the same GET/HAVE, size, and licence summary
    `--list-catalogue` prints, and a Download button
    (`kDownloadModelButtonId`) that calls `download_catalogue_entry` into the
    first configured `modelRegistry.directories` entry, the same destination
    `import_model_package` uses — see
    [design](docs/design/architecture.md#desktop-integration) and
    [requirements](docs/requirements/product.md#distribution-and-usability).
    Progress and failures (already installed, no configured directory,
    transport/checksum errors) are reported on the shared model status line;
    a successful download refreshes the model and catalogue views the same
    way Import… does.
  - [x] Catalogue download itself: `download_catalogue_entry`
    (`catalogue_download.hpp/cpp`) fetches an entry's files over HTTPS with
    progress reporting and resumability (a partially-downloaded file resumes
    from its on-disk size via an HTTP Range request rather than refetching),
    verifies each against its pinned SHA-256 (a self-contained SHA-256,
    deleting a file that fails verification so the next attempt starts it
    over), writes a generated `model.json` (the catalogue carries the
    metadata; the remote host serves only weight files), and installs the
    verified package — see
    [requirements](docs/requirements/product.md#distribution-and-usability).
    The outbound HTTPS client is WinHTTP (`fetch_url_to_file`), Windows-first
    per product direction; other platforms throw a clear not-implemented
    error, the same convention as the tray, settings window, and playback
    sink. Driven by `tts-host --headless --download-model <id>
    [--model-directory <path>]`, the destination choice among
    `modelRegistry.directories`.
  - [x] Windows tray selection validation: native Windows confirmed that the
    configured selection chord and **Read clipboard** speak through the selected
    endpoint without closing the runner. The Copy-only capture route is an
    interim implementation, replaced by the selection-capture slices below —
    see [design](docs/design/architecture.md#desktop-integration).
  - [ ] Hotkey settings and tray controls: add a configurable selection shortcut
    and the global toggle after the basic selection test is verified on Windows
    — see [design](docs/design/architecture.md#desktop-integration) and
    [requirements](docs/requirements/product.md#configuration-and-controls).
    - [x] Configurable selection shortcut: Settings captures a pressed chord
      into `hotkeys.readSelection`, probes availability through `RegisterHotKey`
      before saving, and the tray unregisters while Settings is open then
      applies the saved binding when it closes. The window routes its
      user-visible status through one scrollable activity log; an empty shortcut
      disables it.
    - [x] **AFK — Global shortcut lifecycle verification:** use a testable runtime
      registration state in the tray; cover suspend/resume, Settings preserving
      suspension, retry after registration conflict, shutdown release, and
      startup registration. Linux and Windows Release checks pass — see
      [design](docs/design/architecture.md#desktop-integration) and
      [requirements](docs/requirements/product.md#configuration-and-controls).
    - [ ] **HITL — Global shortcut toggle acceptance:** verify the checked tray
      menu, suspend/resume, and Settings round-trip in combined Windows acceptance.
  - [x] **AFK — Windows direct-selection prototype:** replace unconditional
    synthetic Copy with focused UI Automation capture through the existing
    hotkey-to-speech path. Unsupported/empty/timed-out capture leaves speech and
    the target untouched and offers manual copy plus Read clipboard. Cover stale
    targets, empty selection, protected fields, and absence of injected input — see
    [design](docs/design/architecture.md#desktop-integration) and
    [requirements](docs/requirements/product.md#speech-behaviour).
  - [ ] **AFK — Synthetic Copy compatibility verification:** validate focused-control
    identification, modifier handling, and clipboard ownership/preservation for
    each adapter before enabling it. Unknown targets stay manual-copy-only — see
    [design](docs/design/architecture.md#desktop-integration).
  - [x] **AFK — Windows desktop selection capture policy:** add the configured
    `automatic` / UI-Automation-only / clipboard-only policy and a focused UI
    Automation `TextPattern` selection read. In automatic mode, use the safe
    Copy fallback only after UI Automation has no text and the adapter passes
    compatibility verification. Unsupported fallback remains unavailable with
    a manual-copy explanation; it does not block direct-capture settings.
    Expose the method and
    actionable failure in the activity log — see
    [design](docs/design/architecture.md#desktop-integration) and
    [requirements](docs/requirements/product.md#speech-behaviour).
  - [x] **AFK — Browser selected-text reader:** ship the minimum Chrome
    extension and loopback ingress needed for its page content script to send
    DOM-selected text to the Host without clipboard mutation; include only the
    extension's explicit origin in the CORS allowlist — see
    [design](docs/design/architecture.md#local-api) and
    [design](docs/design/architecture.md#desktop-integration).
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
  - [x] Windows WASAPI format negotiation: a native tray request reaches
    playback but the endpoint rejects Kokoro's submitted PCM format with
    `AUDCLNT_E_UNSUPPORTED_FORMAT` (`0x88890008`); negotiate a shared-mode
    format and convert PCM when it differs before retesting selection reading
    — see [design](docs/design/architecture.md#speech-pipeline) and
    [requirements](docs/requirements/product.md#speech-behaviour). The
    platform-independent conversion tests and native Windows target compile
    pass; audible endpoint validation is part of the remaining tray-selection
    test.
  - [x] Chunked streaming: the host splits normalized text into
    sentence-scale chunks (`text_splitter.hpp/cpp`) and synthesizes each as a
    separate back-to-back request within one runner session, playing each
    chunk as soon as it is ready rather than after the whole text finishes —
    see [design](docs/design/architecture.md#speech-pipeline).
  - [x] **AFK — Bound Kokoro synthesis after phonemization:** partition an
    over-limit phoneme sequence into ordered, lossless batches of at most 510
    ids in the Kokoro runner; emit their PCM through the existing multi-frame
    audio stream for the original request; and cover a long, unpunctuated
    clipboard-style input plus the 510-id boundary without model weights in
    CI — see [requirements](docs/requirements/product.md#speech-behaviour)
    and [design](docs/design/architecture.md#engine-bounded-synthesis).
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
    WSL).
  - [x] Interrupt and opt-in queue semantics: tray requests now run through a
    single background scheduler, so the Win32 message loop stays responsive.
    **Read clipboard** and the configured selection hotkey interrupt the active utterance by
    default (playback stops promptly; synchronous runner work ends at its
    current sentence); **Queue clipboard** is the explicit opt-in that waits
    behind active work. Two utterances never play simultaneously — see
    [design](docs/design/architecture.md#speech-pipeline) and
    [requirements](docs/requirements/product.md#speech-behaviour).
  - [x] **AFK — Now Playing playback and speed controls:** replace the scheduler's
    cancellation-only active utterance with the controller needed to expose
    accurate state and pause/resume/stop from a modeless tray-launched Now
    Playing window; add the persisted 0.5×–2.0× speed control and apply active
    changes at the next sentence boundary without pitch-changing resampling;
    retain existing interrupt and queue behavior — see
    [design](docs/design/architecture.md#playback-control-and-position) and
    [requirements](docs/requirements/product.md#speech-behaviour).
    - [x] Playback-controller foundation: `PlaybackController` owns one active
      lifecycle (`preparing`/`playing`/`paused`/`stopping`/`idle`) and shares a
      pause/cancel token between tray synthesis and `PlaybackSink`; existing
      interrupt and queue replacement behavior remains serialized.
    - [x] Modeless tray-launched Now Playing UI with Pause/Resume/Stop wired to
      the controller; closing it hides without stopping speech.
    - [x] Persisted default and active 0.5×–2.0× speed control: `audio.speechSpeed`
      defaults to 1.0×, Settings persists it, Now Playing changes an active
      utterance, and the host discards stale unplayed lookahead before replaying
      that sentence at the requested typed runner speed.
  - [x] **AFK — Generated-audio seek controls:** retain active-utterance PCM
    and add the configurable 1–30 s seek interval plus enabled-only backward
    and forward controls to Now Playing with bounded RAM/disk retention;
    test sample-rate conversion, paused seek, and storage exhaustion.
    Do not guess a text offset or
    re-synthesize audio for a seek — see
    [design](docs/design/architecture.md#playback-control-and-position) and
    [requirements](docs/requirements/product.md#configuration-and-controls).
  - [ ] **AFK — Windows start at login:** OS-owned login entry with a Settings
    switch and a first-run question — see
    [design](docs/design/architecture.md#login-startup),
    [ADR 0008](docs/adr/0008-os-owned-login-startup.md), and
    [requirements](docs/requirements/product.md#configuration-and-controls).
    - [x] Application option and isolated native registry verification:
      enable/disable, quoted executable/config paths, repeated changes, and
      preservation of unrelated entries without changing login settings.
    - [x] **AFK — Settings tabs:** regroup the existing Settings controls into
      General, Playback, Models, and Server tabs above the activity log; no
      behaviour change — see
      [design](docs/design/architecture.md#desktop-integration).
    - [x] **AFK — OS-owned Windows startup switch:** stop launch-time
      registration; the General tab switch reads the Run and
      `StartupApproved` state on open, shows on/off/system-disabled/other-copy,
      writes only the Run entry, and opens `ms-settings:startupapps` — see
      [design](docs/design/architecture.md#login-startup) and
      [ADR 0008](docs/adr/0008-os-owned-login-startup.md). Tests use an
      isolated registry key.
    - [x] **AFK — First-run startup question:** ask once per data directory,
      pre-ticked from `startup.launchOnLogin`; write the marker; development
      launches pre-create it — see
      [design](docs/design/architecture.md#login-startup) and
      [developer workflow](docs/design/developer-workflow.md).
    - [ ] Settings toggle, system-page round trip, and sign-in acceptance in an
      isolated Windows environment; do not enable login startup on the
      developer machine.
  - [ ] **AFK — Gap-free utterance audio stream:** one endpoint client per
    utterance with event-driven refill and a single end-of-utterance drain —
    see [design](docs/design/architecture.md#utterance-audio-stream) and
    [finding 16](docs/reviews/code-review-2026-10-03-roadmap-slices-branch.md#finding-16).
    - [x] **AFK — Per-utterance stream:** keep one client across sentences,
      including underrun silence, pause, Stop, seek, and replacement; cover with
      the fake endpoint in `tts_host_synthesis_pipeline`.
    - [x] **AFK — Playback tab tuning:** add `audio.bufferMilliseconds`,
      `sentencePauseMilliseconds`, and `lookaheadSentences` to the schema and
      the Playback tab with **Reset to defaults** — see
      [design](docs/design/architecture.md#utterance-audio-stream) and
      [requirements](docs/requirements/product.md#configuration-and-controls).
    - [ ] **HITL — Re-measure the boundary gap:** rerun the audible
      `tts-host-playback-gap-probe` after the stream change, with the user's
      permission.
- [x] **HITL — Now Playing dialog acceptance:** user accepted the native
  dialog and requested that its testing be considered done (2026-10-03).
- [ ] **HITL — Combined Windows desktop acceptance:** after the selection,
  Now Playing, seek, and browser prototypes, run one short native walkthrough:
  Now Playing dialog acceptance is complete; remaining checks cover
  selected/empty text in Windows Terminal/Codex, an editor's embedded terminal,
  OneNote, and a browser; correct source targeting; audible pause/resume and
  Stop clearing the queue; close-to-hide; seek positions and generated boundaries.
  Exercise the tray shortcut toggle and any enabled Copy adapters; explain
  unsupported cases, collect UI feedback, and freeze default shortcuts here.
  Automated evidence belongs to each
  AFK slice; never mark native acceptance passed from a build alone — see
  [design](docs/design/architecture.md#desktop-integration) and
  [design](docs/design/architecture.md#playback-control-and-position).
- [ ] **AFK — Browser reading client expansion:** add article reading and
  browser-local playback controls after the selected-text extension/loopback
  path is proven — see [design](docs/design/architecture.md#local-api) and
  [design](docs/design/architecture.md#desktop-integration).
  - [ ] Article reading: add a page context-menu action that submits the
    longest non-empty visible `<article>`/`<main>` text, falling back to page
    body text. The implementation is present; native Chrome-to-Host interaction
    verification remains.
  - [x] **AFK — Browser-local playback controls:** extension control page shows active
    Host state and played/generated position, with pause/resume/stop through
    native WebSocket; Stop clears queued reads and closing the page keeps speech.
  - [ ] **HITL — Browser-local controls acceptance:** verify native Chrome state updates,
    pause/resume/stop, reconnect, and close-without-stop during Windows acceptance.
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
  - [ ] **AFK — Native macOS selection adapter:** implement macOS accessibility
    permission/status and focused-selection capture; no synthetic Windows-style
    copy fallback — see [design](docs/design/architecture.md#desktop-integration)
    and [design](docs/design/architecture.md#platform-strategy).
  - [ ] **AFK — Native Linux selection adapter:** implement KDE Plasma Wayland
    direct accessibility capture and actionable unavailable-capability reporting;
    use Kubuntu for initial desktop acceptance and retain Rocky 8/9 KDE as
    compatibility targets. Never assume Windows active-window or injection
    behavior — see
    [design](docs/design/architecture.md#desktop-integration) and
    [design](docs/design/architecture.md#platform-strategy).
  - [ ] **HITL — Linux KDE Wayland acceptance:** verify focused/empty/protected
    selections and unsupported applications on Kubuntu; check compatibility on
    the user's Rocky 8/9 KDE sessions — see
    [design](docs/design/architecture.md#platform-strategy).
  - [ ] **AFK — Linux X11 selection compatibility:** add native X11-session
    capture after the KDE Wayland adapter; fail closed for unavailable targets —
    see [design](docs/design/architecture.md#platform-strategy).
  - [ ] **AFK — Linux and macOS login startup:** XDG Autostart (Background
    portal under Flatpak) and `SMAppService.mainApp`, with the same Settings
    states as Windows — see
    [design](docs/design/architecture.md#login-startup).

## Branch review follow-up

Findings come from the [October 3 review](docs/reviews/code-review-2026-10-03-roadmap-slices-branch.md#triage--2026-10-03) (with its
[captured plan](docs/reviews/code-review-2026-10-03-roadmap-slices-branch.md#captured-implementation-plan)) and the
[October 14 checkpoint review](docs/reviews/code-review-2026-10-14-checkpoint.md#triage--2026-10-14). Each fix includes
regressions for its failure mode.

### Next

- [ ] **AFK — Coalesce download activity progress:** throttle transient updates
  so model downloads do not repaint the full log per network read or evict
  recent actionable messages. See
  [finding 4](docs/reviews/code-review-2026-10-14-checkpoint.md#finding-4).
- [ ] **HITL — Plan played-text history and visible queue:** user idea
  (2026-10-14): list played/playing reads, step back/forward and replay a
  chosen one, and show the queue. Needs `/planning-capture` first; a history
  holds spoken text, which the Activity record deliberately never does.
  Queued requests already carry a source label (`SpeechRequest` in
  `src/main.cpp`).

### Completed

- [x] **AFK — Now Playing Stop clears queued speech:** [Oct 14 finding 1](docs/reviews/code-review-2026-10-14-checkpoint.md#finding-1).
- [x] **AFK — Now Playing read actions and source targeting:** [Oct 14 finding 3](docs/reviews/code-review-2026-10-14-checkpoint.md#finding-3), [design](docs/design/architecture.md#desktop-integration).
- [x] **AFK — Reload external configuration edits:** [Oct 14 finding 2](docs/reviews/code-review-2026-10-14-checkpoint.md#finding-2), [design](docs/design/architecture.md#live-reload).
- [x] **AFK — Stop and seek reliably wake paused playback:** [finding 4](docs/reviews/code-review-2026-10-03-roadmap-slices-branch.md#finding-4).
- [x] **AFK — Speech uses a consistent configuration snapshot:** [finding 1](docs/reviews/code-review-2026-10-03-roadmap-slices-branch.md#finding-1).
- [x] **AFK — Settings and tray shutdown preserve window lifetimes:** [finding 6](docs/reviews/code-review-2026-10-03-roadmap-slices-branch.md#finding-6).
- [x] **AFK — Settings preserves valid configuration on save failure:** [finding 8](docs/reviews/code-review-2026-10-03-roadmap-slices-branch.md#finding-8).
- [x] **AFK — A conflicting Windows API listener fails clearly:** [finding 2](docs/reviews/code-review-2026-10-03-roadmap-slices-branch.md#finding-2).
- [x] **AFK — Runner inherits only its own required handles:** [finding 10](docs/reviews/code-review-2026-10-03-roadmap-slices-branch.md#finding-10).
- [x] **AFK — Runner hang and death recover without hanging shutdown:** [finding 3](docs/reviews/code-review-2026-10-03-roadmap-slices-branch.md#finding-3).
- [x] **AFK — Unicode Windows paths work end to end:** [finding 9](docs/reviews/code-review-2026-10-03-roadmap-slices-branch.md#finding-9).
- [x] **AFK — Saved skip interval applies to the next press:** [finding 7](docs/reviews/code-review-2026-10-03-roadmap-slices-branch.md#finding-7).
- [x] **HITL — Decide startup registration synchronization:** [ADR 0008](docs/adr/0008-os-owned-login-startup.md).
- [x] **AFK — Desktop launch and fatal error reporting:** [finding 11](docs/reviews/code-review-2026-10-03-roadmap-slices-branch.md#finding-11).
- [x] **AFK — Prompt playback after seek:** [finding 12](docs/reviews/code-review-2026-10-03-roadmap-slices-branch.md#finding-12).
- [x] **AFK — Resident synthesis lifecycle:** [finding 13](docs/reviews/code-review-2026-10-03-roadmap-slices-branch.md#finding-13).
- [x] **AFK — Audit runtime regression coverage:** [finding 17](docs/reviews/code-review-2026-10-03-roadmap-slices-branch.md#finding-17).
- [x] **AFK — Correct catalogue verification note:** [finding 15](docs/reviews/code-review-2026-10-03-roadmap-slices-branch.md#finding-15).
- [x] **AFK — Measure sentence transitions:** about 80 ms (p95 about 90 ms) of
  added silence per boundary; a continuous utterance stream is justified but
  not scheduled. [finding 16](docs/reviews/code-review-2026-10-03-roadmap-slices-branch.md#finding-16).

Finding #14 is rejected; no implementation slice. See [validation](docs/reviews/code-review-2026-10-03-roadmap-slices-branch.md#finding-14).

## Open decisions

- How to execute the English bake-off before candidate integration: use a
  consistent external benchmark harness or implement TTS Host runners for the
  candidates first. Qwen and LuxTTS were evaluated through external pilot paths;
  neither is integrated in TTS Host.
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
