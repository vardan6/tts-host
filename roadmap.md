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
- [ ] **AFK — First audio through the runner protocol:** implement the JSON-RPC
  control channel and framed audio channel, ship the stub runner and the Kokoro
  runner, synthesize text to a WAV response, and drive the whole path from an
  end-to-end test against the stub in CI.
- [ ] **HITL — English model bake-off:** on the RTX 3070 Laptop GPU, compare
  Kokoro, Qwen3-TTS 0.6B/1.7B at candidate quantizations, and the strongest
  lightweight alternative against the acceptance criteria in
  `docs/requirements/product.md`; settle llama.cpp versus upstream PyTorch for
  the Qwen runner and select the fast and quality defaults.
- [ ] **AFK — Model manager and settings window:** model/profile switching,
  load/unload, idle timeout, directory watching, import, catalogue download with
  progress and checksums, and licence display — through JSON and the first
  desktop UI. **Decides the UI toolkit and settles its licensing.**
- [ ] **AFK — Everyday desktop playback:** tray controls, host-side playback,
  chunked streaming, interrupt and queue semantics, markdown/HTML normalization,
  output-device selection, CLI text/stdin/clipboard support, and start-at-login.
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
  configuration, manifests, API, and conformance tests.

## Open decisions

- Desktop UI toolkit, and its licensing compliance — decided in the model
  manager slice, deliberately kept off the critical path until then.
- Upstream PyTorch versus llama.cpp for the Qwen quality runner, and the
  quantization level — an output of the bake-off, not an input.
- Whether a WSL headless distribution is published, or `--headless` merely makes
  it possible.

## Deliberately deferred

- Remote and cloud providers. Revisit only if local quality proves inadequate.
- Full text normalization (numbers, dates, currency, abbreviations).
- Token authentication and LAN serving. The config vocabulary is reserved;
  see `docs/adr/0003-no-authentication.md`.
- Self-updating.
