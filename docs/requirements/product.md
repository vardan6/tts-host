# TTS Host — product requirements

## Name

The product is **TTS Host**. The repository, the installed directory, and the
host executable use `tts-host`. "Host" names the long-lived desktop process that
owns configuration, discovery, and the local API; it does not mean self-hosting.
Inference processes are "runners" — see `docs/design/architecture.md`.

## Product goal

Provide a text-to-speech application that non-developers can run without
installing Python, packages, or development tools, and that works with no
network connection. Applications on the same computer can synthesize speech
through a stable local API, while ordinary users can read selected or copied
text through desktop controls and shortcuts.

The application must be fully usable offline. No account, no credential, and no
network call is required for any core function. Remote providers are out of
scope for the first release; if any is ever added it is opt-in and never a
default.

## Language priority

English is the first-class language and the only one with hard acceptance
criteria. Russian and Armenian are supported as best-effort: they must not
degrade English quality or alter English defaults, and their quality does not
gate a release. Language-specific models are acceptable.

Armenian's only offline candidate is non-commercially licensed. That is
acceptable — model files are distributed separately from the application, so the
licence constrains the user's download choice rather than the product's
distribution. The licence is shown before download.

## Supported environments

- Windows 11 is the first-class target. The concept is proven and polished there
  before other platforms are built.
- Linux and macOS remain product targets and stay in the design, but are
  planned-not-built in the first release. Platform-specific builds and
  acceleration backends are acceptable.
- WSL must never be required on Windows.
- End users must not need a separately installed Python interpreter or Python
  package environment.
- Each supported platform must offer a self-contained portable distribution.

## Models

- Multiple TTS models can be installed concurrently and switched conveniently.
- A high-quality model is the default; a lightweight model remains available for
  CPU-only machines and short snippets.
- The application ships with a working model so it speaks on first launch with
  no download and no network.
- A configurable model directory is scanned automatically.
- Every discovered, compatible model is listed with its languages, voices,
  engine, device support, size, and load state when known.
- Users can add a model through the UI or by placing a model package in a model
  directory.
- Models can be loaded eagerly, on first use, or explicitly. Idle unloading is
  configurable globally and overridable per model/profile.
- Invalid or incomplete model packages are reported with an actionable reason.
- Merely adding a model package must never execute Python or other code supplied
  by that package.
- If no model is installed, the application still starts and reports the
  condition with an actionable message; it must not fail to launch.

### Acceptance criteria for default model selection

Measured on the reference machine (RTX 3070 Laptop, 8 GB) using the agreed
technical and long-form English passages:

| Profile | Time to first audio | Real-time factor | Memory |
|---------|--------------------:|-----------------:|--------|
| fast    | ≤ 300 ms | ≤ 0.3 | ≤ 2 GB RAM, CPU only |
| quality | ≤ 1.5 s  | ≤ 1.0 | ≤ 6 GB VRAM |

A real-time factor above 1.0 cannot sustain streaming playback and is a hard
failure. Blind A/B listening breaks ties between candidates that pass; it never
overrides a hard failure.

## Speech behaviour

- A new read request interrupts whatever is currently speaking. Queueing is
  available but is not the default.
- Long text begins playing quickly rather than after full synthesis, and can be
  cancelled at any point.
- Markdown and HTML input are read correctly by default, not read as literal
  markup: code blocks are announced and skipped, inline code is read, links are
  read as their text, tables are skipped with a marker, headings are read with a
  leading pause. This is not user-configurable in the first release.
- The language of a request is taken from an explicit request parameter; absent
  that, from the text's script; absent that, from the configured default.
- Audio plays through the host by default. Clients may instead request audio
  data.
- Audio follows the operating system's default output device and follows changes
  to it, unless a device is pinned in configuration.

## Configuration and controls

- JSON is the canonical user-editable configuration format; see
  `docs/adr/0004-json-canonical-config.md`.
- A versioned JSON Schema documents and validates the configuration.
- The desktop UI edits the same JSON configuration; UI settings must not become
  a second source of truth.
- Every setting is reachable from the settings window. The tray menu duplicates
  only the frequently changed subset.
- Configuration changes take effect without a restart wherever possible.
  Settings that genuinely require a restart say so rather than being silently
  ignored.
- Model selection is available through at least: a default model/profile; a
  default per language; a per-request API selection; and desktop/tray controls.
- The application exposes model discovery, load, unload, status, voice,
  language, and device information.
- Configuration errors identify the JSON path and preserve the last valid
  configuration.
- Unsupported schema versions are rejected with an actionable error without
  rewriting the file. Any future migration must be explicit, atomic, and retain
  the pre-migration configuration.

## Distribution and usability

- The application is licensed Apache-2.0 and developed in a public repository.
- Model files are distributed separately from the core application unless their
  licence explicitly permits bundling.
- The application can download supported models with progress, resumability,
  checksum verification, licence disclosure, and a destination choice.
- Portable mode keeps configuration, model packages, logs, and other mutable
  data in user-visible portable locations. Installed mode uses normal per-user
  OS data directories.
- Windows releases should distinguish CPU/generic and NVIDIA-capable packages if
  bundling every acceleration runtime would make one distribution unreasonably
  large.
- The application runs in the background and is reachable at a stable local
  address without the user starting it manually.
- The local API binds to loopback and requires no credential; it must not be
  exposed beyond the machine without explicit configuration and a warning. See
  `docs/adr/0003-no-authentication.md`.
- Updates are performed by replacing the installed files. The application checks
  for a newer release and links to it; it does not update itself.
- Core tray and settings workflows are keyboard-operable and expose names,
  roles, state, and status through each platform's accessibility APIs.

## Initial interfaces

- Local HTTP API with an OpenAI-compatible speech endpoint where practical.
- A native streaming interface for low-latency playback and cancellation.
  Compatibility with the OpenAI endpoint must not limit it.
- Command-line client accepting direct text, standard input, and clipboard text.
- Desktop tray for model/profile selection, playback, status, and output device,
  plus a settings window reachable independently of the tray.
- A global shortcut that reads the current selection from whatever application
  has focus.
- A browser extension client.

## Explicit non-goals for the first release

- Training or fine-tuning models.
- Automatically supporting arbitrary model architectures based only on a weight
  file extension.
- Requiring Docker, WSL, Git, Python, or a compiler on an end-user machine.
- Bundling every available model into the application download.
- Remote or cloud speech providers.
- Authentication, accounts, multi-user support, or LAN serving.
- A Windows Explorer shell context-menu entry — that is a separate shell
  extension, not part of this application.
- A floating playback-control widget. The API stays client-agnostic so one can
  be built separately later.
- Self-updating.
- Full text normalization (number, date, currency, and abbreviation expansion)
  beyond what the models already handle.
