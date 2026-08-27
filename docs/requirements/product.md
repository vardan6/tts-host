# TTS Host — product requirements

## Name

The product is **TTS Host**. The repository, the installed directory, and the
host executable use `tts-host`. "Host" names the long-lived desktop process that
owns configuration, discovery, and the local API; it does not mean self-hosting.
Inference processes are "runners" — see `docs/design/architecture.md`.

## Product goal

Provide a local-first text-to-speech application that non-developers can run
without installing Python, packages, or development tools. Applications on the
same computer can synthesize speech through a stable local API, while ordinary
users can read selected or copied text through desktop controls and shortcuts.

## Supported environments

- Windows 11 is the first-class initial target. The application and inference
  engines must run natively on Windows; WSL must not be required.
- Linux and macOS are supported product targets. Platform-specific builds and
  acceleration backends are acceptable.
- A WSL/Linux deployment may also be offered, provided it uses the same API and
  configuration concepts as the native application.
- End users must not need a separately installed Python interpreter or Python
  package environment.
- Each supported platform must offer a self-contained portable distribution.
  An installer may additionally provide shortcuts, start-at-login, and normal
  uninstall behavior.

## Models

- Multiple TTS models can be installed concurrently and switched conveniently.
- English synthesis quality is the first priority. Russian and Armenian support
  must not reduce English quality; language-specific models are acceptable.
- A configurable model directory is scanned automatically.
- Every discovered, compatible model is listed in the application with its
  languages, voices, engine, device support, size, and load state when known.
- Users can add a model through the UI or by placing a model package in a model
  directory.
- Models can be loaded eagerly, on first use, or explicitly. Idle unloading is
  configurable globally and overridable per model/profile.
- Invalid or incomplete model packages are reported with an actionable reason.
- Merely adding a model package must never execute Python or other code supplied
  by that package.

## Configuration and controls

- JSON is the canonical user-editable configuration format.
- A versioned JSON Schema documents and validates the configuration.
- The desktop UI edits the same JSON configuration; UI settings must not become
  a second source of truth.
- Model selection is available through at least:
  - a default model/profile;
  - a default per language;
  - a per-request API selection;
  - convenient desktop/tray controls.
- The application exposes model discovery, load, unload, status, voice, language,
  and device information.
- Configuration errors identify the JSON path and preserve the last valid
  configuration.

## Distribution and usability

- Model files are distributed separately from the core application unless their
  license explicitly permits bundling.
- The application can download supported models with progress, resumability,
  checksum verification, license disclosure, and a destination choice.
- Portable mode keeps configuration, model packages, logs, and other mutable data
  in user-visible portable locations. Installed mode uses normal per-user OS data
  directories.
- Windows releases should distinguish CPU/generic and NVIDIA-capable packages if
  bundling every acceleration runtime would make one distribution unreasonably
  large.
- The local API binds to loopback by default and must not be exposed to the LAN
  without explicit configuration and a warning.

## Initial interfaces

- Local HTTP API with an OpenAI-compatible speech endpoint where practical.
- Streaming interface for low-latency playback and cancellation.
- Command-line client accepting direct text, standard input, and clipboard text.
- Desktop tray UI for model/profile selection, playback, status, settings, and
  model management.
- Later Windows-wide selection reading, followed by a browser extension.

## Explicit non-goals for the first release

- Training or fine-tuning models.
- Automatically supporting arbitrary model architectures based only on a weight
  file extension.
- Requiring Docker, WSL, Git, Python, or a compiler on an end-user machine.
- Bundling all available models into the application download.

