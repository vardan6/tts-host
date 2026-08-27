# Roadmap

Each item is a thin, independently verifiable product slice. Requirements and
architecture live under `docs/`; this file records sequence only.

- [ ] **AFK — Portable discovery-to-audio path:** launch a native C++ Windows
  development build without an external language runtime, load validated JSON
  configuration, discover one packaged lightweight model, synthesize text to a
  WAV response, and report actionable discovery/configuration errors.
- [ ] **HITL — English model bake-off:** on the RTX 3070 Laptop GPU, compare
  Kokoro, Qwen3-TTS 0.6B/1.7B, and the strongest lightweight alternative using
  agreed technical and long-form passages; select fast and quality defaults based
  on listening, time-to-first-audio, real-time factor, RAM, and VRAM.
- [ ] **AFK — Usable model manager:** add model/profile switching, load/unload,
  idle timeout, directory watching, model import/download, checksums, and license
  display through both JSON and a basic desktop UI.
- [ ] **AFK — Everyday desktop playback:** add tray controls, local playback,
  queue/cancellation, CLI text/stdin/clipboard support, and start-at-login.
- [ ] **HITL — Windows selection reading:** validate UI Automation and protected
  clipboard fallback in OneNote, Windows Terminal, browsers, and representative
  editors before freezing default global shortcuts.
- [ ] **AFK — Browser reading client:** add selection/article reading, playback
  controls, and safe authenticated loopback access.
- [ ] **HITL — Russian and Armenian acceptance:** compare Qwen Russian and offline
  Armenian candidates with native-speaker samples; decide whether an optional
  Gemini cloud provider is valuable.
- [ ] **AFK — Cross-platform releases:** produce and smoke-test native Linux and
  Apple Silicon macOS artifacts using the same configuration, manifests, API, and
  conformance tests.

## Open decisions

- Product name and public repository license.
- Final acceptance of the recommended C++ host, desktop UI toolkit, and exact
  host/runner protocol.
- Qt open-source licensing compliance and the project's own public license.
- Upstream PyTorch versus llama.cpp for the Qwen quality runner.
- Portable-mode marker and default mutable-data locations.
- Whether remote providers belong in the first public release or a later plugin.
