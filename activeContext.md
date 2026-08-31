# Active Context

- Mode: session closed
- Phase/slice: Model manager and settings window (in progress in a parallel
  session) plus everyday-desktop-playback lookahead overlap (this session).
  Windows tray, Settings… entry, settings shell, output device, server
  host/port, and read-only model status/licence display are implemented;
  chunked-streaming playback now overlaps the next chunk's synthesis with the
  current chunk's playback (`src/main.cpp`, background thread joined before
  the next chunk plays).
- State: 22/22 CTests pass on WSL. `compile-win.ps1` now stops on the first
  native command failure, and the `LoadCursorW` Unicode-resource build error
  is fixed. The Win32 UI and audio paths, and the lookahead-overlap timing,
  still require a native Windows rebuild, CTest run, and manual check; WSL
  only covers their non-Windows paths.
- Next atomic step: choose a small model-selection control, likely a settings
  combo for the configured default English profile (`languageDefaults.en`),
  using existing `profiles`; it should write canonical JSON and clearly state
  that restart is required until live reload exists.
- Next-after-next: decide whether a config file watcher has enough live host
  state to justify building it; global-hotkey controls remain premature until
  capture behavior is validated in the Windows-selection HITL slice.
- Blockers/environment: native Windows verification needs `./compile-win.ps1`
  after parallel work is quiescent. WSL lacks eSpeak-NG, audio hardware, and a
  GUI, so real Kokoro, playback/device pinning, tray/settings behavior, and
  bake-off measurements cannot be verified here.
- Open questions: Qwen PyTorch versus llama.cpp/quantization; whether a WSL
  headless distribution is published; runner/runtimes release layout; and
  release-asset preparation ownership (CMake versus Python).
- Discarded as noise: hotkey settings before capture exists; a live-reload
  watcher before a long-lived config-driven host state; ad hoc benchmark
  tooling instead of `stats`; treating the release audit's recommendation
  as an adopted architecture decision; interrupt/queue semantics before the
  not-yet-built local API server exists to issue a second request against;
  tray playback controls (play/pause/stop) before the tray has any active
  playback session to control; and start-at-login, which
  `docs/design/architecture.md`'s Distribution section explicitly defers to
  the installer, not this roadmap item.
