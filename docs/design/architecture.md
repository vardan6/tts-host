# Product architecture

## Shape of the application

The application is a native desktop host with a local service, not a developer
server wrapped in installation instructions.

```text
Desktop/tray UI ─┐
CLI ─────────────┼── local service ── model registry ── runner process ── model
Browser client ──┤         │
Other apps ──────┘         └── playback, queue, cancellation, lifecycle
```

The host owns configuration, discovery, API compatibility, request scheduling,
audio delivery, and model lifecycle. Inference runners own framework-specific
code. Keeping runners behind a process boundary allows native GGUF, ONNX, and
packaged Python/PyTorch implementations to coexist without dependency conflicts.
A runner crash must not terminate the desktop host.

The runner protocol should be versioned and transport neutral. JSON control
messages plus framed binary audio over standard streams or a loopback socket are
both suitable; choose one during the first vertical slice.

## Distribution

The preferred release artifact is a self-contained directory, zipped for
portable use. A single self-extracting executable is not the canonical format:
large ML libraries make it slower to start and harder to inspect, update, and
repair.

### Host-language recommendation (pending acceptance)

Use C++ for the long-lived product host: desktop/tray UI, configuration, model
registry, local API, process supervision, audio playback, request queue, and
platform integration. Do not require every inference engine to be rewritten in
C++. Model runners remain separate executables and may be native C++, ONNX
Runtime, or a self-contained Python/PyTorch compatibility pack.

This recommendation fits the product's dominant constraints better than a
Python host: small predictable startup, straightforward native Windows
distribution, direct access to OS integration, and a credible Windows/Linux/macOS
binary story. It costs more implementation effort and does not solve model
compatibility by itself; most newly released research models still arrive with
Python inference first. The runner boundary preserves access to those models.

Proposed host stack:

- C++20 or newer with CMake.
- Qt 6 Core/UI/Multimedia for cross-platform desktop, tray, audio, and process
  integration, subject to the chosen project's compliance with Qt's LGPL or a
  commercial license.
- Boost.Asio/Beast for loopback HTTP and WebSocket service functionality. Avoid
  Qt HTTP Server unless the project intentionally accepts its GPLv3 or commercial
  licensing terms.
- A small C++ JSON library plus explicit JSON Schema validation for `config.json`
  and model manifests.
- Out-of-process runners rather than in-process third-party DLL plugins, avoiding
  C++ ABI conflicts and isolating crashes.

Proposed Windows layout:

```text
tts-app/
  tts-app.exe
  runtimes/
  runners/
  schemas/
  models/
  config.json
  portable.marker
```

The primary host is compiled natively on each target OS. If a Python-only model
is needed, its compatibility runner can be built using Nuitka standalone mode so
the user still never installs Python. Start with directory-based distributions
rather than one-file builds.

Inference runners need not all be Python. In particular, llama.cpp now has native
Qwen3-TTS support and portable CPU, CUDA, Metal, Vulkan, and other backends. It is
a promising Qwen quality-runner candidate, subject to an audio-quality, feature,
VRAM, and latency comparison with the upstream PyTorch implementation.

References:

- https://doc.qt.io/qt-6/qt-intro.html
- https://doc.qt.io/qt-6/licensing.html
- https://www.boost.org/library/latest/beast/
- https://nuitka.net/user-documentation/user-manual.html
- https://github.com/ggml-org/llama.cpp/blob/master/tools/tts/README.md
- https://github.com/ggml-org/llama.cpp/wiki/Feature-matrix

## Configuration

`config.json` is canonical. `config.schema.json` ships beside the application.
Installed mode stores user configuration in the platform's per-user application
data directory; portable mode stores it beside the application. Writes are
atomic, versioned, and retain the last valid copy.

Illustrative shape, not yet a frozen schema:

```json
{
  "$schema": "./schemas/config.schema.json",
  "schemaVersion": 1,
  "server": {
    "host": "127.0.0.1",
    "port": 7861,
    "authentication": "automatic"
  },
  "modelRegistry": {
    "directories": ["./models"],
    "watchForChanges": true,
    "idleUnloadSeconds": 600,
    "maximumLoadedGpuModels": 1
  },
  "profiles": {
    "fast": {
      "model": "kokoro-en-v1",
      "device": "cpu",
      "loadPolicy": "eager"
    },
    "quality": {
      "model": "qwen3-tts-1.7b",
      "device": "auto",
      "loadPolicy": "onDemand"
    }
  },
  "languageDefaults": {
    "en": "quality",
    "ru": "quality",
    "hy": "armenian"
  }
}
```

The settings UI displays validation errors and edits this document rather than
maintaining an independent settings database.

## Model packages and discovery

File extensions do not identify enough information to safely load arbitrary TTS
models. Each model package therefore contains a data-only `model.json` manifest.

```text
models/
  qwen3-tts-1.7b/
    model.json
    model.gguf
    voices/
  kokoro-en-v1/
    model.json
    model.onnx
    voices/
```

Illustrative manifest:

```json
{
  "$schema": "../../schemas/model.schema.json",
  "schemaVersion": 1,
  "id": "qwen3-tts-1.7b",
  "displayName": "Qwen3 TTS 1.7B",
  "engine": "qwen3-gguf",
  "languages": ["en", "ru"],
  "files": {
    "model": "model.gguf"
  },
  "license": {
    "name": "Apache-2.0",
    "url": "https://huggingface.co/Qwen"
  }
}
```

Discovery scans configured directories, validates manifests, verifies referenced
paths stay inside the package, and asks the registered engine adapter to inspect
capabilities. A guided importer may recognize selected well-known formats and
create a manifest. Unknown weights remain visible as unsupported rather than
being guessed or loaded with remote code.

Engine plugins are trusted application components installed separately from
data-only model packages. This prevents a downloaded model from gaining code
execution simply by being copied into `models/`.

## Initial engine candidates

- Kokoro-82M: lightweight English default/fast profile; evaluate ONNX and packaged
  upstream implementations.
- Qwen3-TTS 1.7B: primary local quality candidate for English and Russian;
  compare llama.cpp GGUF with upstream PyTorch on the target RTX 3070 Laptop GPU.
- LuxTTS: optional lightweight voice-cloning comparison.
- MMS Armenian: offline Armenian fallback, with its non-commercial license shown
  clearly to the user.
- Gemini TTS: optional remote provider, never represented as a local model. It is
  useful as an opt-in quality comparison and supports English, Russian, and
  Armenian, but requires network access and credentials.

Gemma 3n/4 are not TTS engines. They accept audio and generate text, making them
relevant to future transcription or text cleanup but not speech synthesis.
Google's separately named Gemini TTS models are cloud API services.

References:

- https://ai.google.dev/gemma/docs/gemma-3n/model_card
- https://ai.google.dev/gemini-api/docs/speech-generation

## Platform strategy

- Windows: first implementation and validation target; native executable, native
  audio and global-hotkey integration, CPU and NVIDIA release variants.
- Linux: native package using the same host and runner protocol; CUDA/CPU first.
- macOS: signed/notarized app bundle eventually; Apple Silicon and Metal first.
- WSL: optional headless distribution using the same service contract. It is not
  part of the Windows desktop dependency chain.

Platform-specific builds may contain different runner binaries while exposing
the same model/profile and API concepts.
