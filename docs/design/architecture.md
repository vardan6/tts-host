# Product architecture

## Shape of the application

The application is a native desktop host with a local service, not a developer
server wrapped in installation instructions.

```text
Tray + settings window ── in-process ──┐
                                       │
CLI ───────────────┐                   │
Browser extension ─┼── HTTP ── tts-host (single process)
Other apps ────────┘              │
                                  ├── model registry
                                  ├── playback, chunking, queue, cancellation
                                  └── runner processes ── models
```

`tts-host` is one executable. It owns configuration, discovery, API
compatibility, request scheduling, text normalization, audio delivery, and model
lifecycle. Its tray and settings window are its own front end and run in the
same process; the CLI, the browser extension, and any later external client go
over HTTP. `--headless` runs it with no UI at all and is supported from the
first release. Rationale and rejected alternatives: `docs/adr/0001-cpp-host.md`.

Inference runners own framework-specific code and run as separate processes, so
native ONNX, GGUF, and packaged Python implementations coexist without
dependency conflicts and a runner crash cannot terminate the host.

### Host stack

- C++20 or newer with CMake.
- No cross-platform UI toolkit. Tray, settings window, and audio output are
  each implemented against native platform APIs (Win32, AppKit, GTK) behind
  small per-platform interfaces, one platform at a time — see
  [ADR 0007](../adr/0007-native-ui-per-platform.md). Slices 1 and 2 are
  headless and do not depend on this.
- Boost.Asio/Beast for loopback HTTP and WebSocket. Avoid Qt HTTP Server unless
  the project intentionally accepts its GPLv3 or commercial terms. This is the
  *inbound* local API server's stack; the outbound catalogue-download fetch
  uses a separate, native-per-platform client — see
  [Download catalogue](#download-catalogue).
- A small C++ JSON library plus explicit JSON Schema validation for
  `config.json` and model manifests.
- Out-of-process runners rather than in-process third-party DLL plugins,
  avoiding C++ ABI conflicts and isolating crashes.
- GoogleTest with CTest. Unit tests cover configuration and manifest validation;
  an end-to-end test drives the CLI against the stub runner, so CI needs no GPU
  and no model weights.

Platform-specific code — audio output, global hotkeys, tray, autostart, data
directories — sits behind small interfaces rather than being spread through the
codebase. Windows is the only implemented backend in the first release; this is
what keeps the later ports a port rather than a rewrite.

## Runner protocol

Control uses JSON-RPC 2.0 with LSP-style `Content-Length` framing over runner
stdin/stdout. Audio uses a distinct host-created, one-way inherited pipe: the
runner receives its write endpoint through `TTS_HOST_AUDIO_FD` on POSIX or
`TTS_HOST_AUDIO_HANDLE` on Windows, while the host retains the read endpoint.
The audio stream is length-prefixed binary; each chunk is headed by sequence
number, sample count, and flags. Runner stderr remains diagnostic output.
`initialize` exchanges protocol version and engine capabilities. Methods:
`load`, `unload`, `synthesize`, `cancel`, `stats`.

Version 1 synthesis payloads are interleaved `pcm_s16le`. `synthesize` accepts
non-empty `params.text` and an optional typed `params.speed`, returns stream
metadata and total sample frames, and marks its final audio frame with the
end-of-stream flag. One request is active per runner initially. The protocol
ADR defines the exact wire contract.

`stats` reports peak RSS, VRAM, time-to-first-chunk, and sample count. It exists
in the first protocol version because the model bake-off must measure those
numbers, and building it later means building throwaway tooling first.

A stub runner emitting synthetic audio with no model is a permanent in-repo
fixture and the basis of the end-to-end test.

On runner death mid-synthesis the host restarts the runner and retries the
failed chunk once, then fails with an actionable error. Every restart is logged.

Full rationale: `docs/adr/0002-runner-protocol.md`.

## Speech pipeline

```text
text ── normalize ── split ── synthesize (lookahead) ── play or stream
```

- **Normalize.** Markdown and HTML are converted to speakable text in the host,
  so every client benefits: code blocks announced and skipped, inline code read,
  links reduced to their text, tables skipped with a marker, headings given a
  leading pause. Number, date, and abbreviation expansion is left to the models
  in the first release.
- **Split.** The host splits into sentence-scale chunks. Splitting lives here,
  not in clients, because it is what makes playback start in under a second and
  makes cancellation cheap — stop after the current chunk.
- **Synthesize.** Chunks are synthesized with a small lookahead and streamed
  back-to-back.
- **Play or stream.** The host plays audio through the system default output
  device by default, following device changes live; a device may be pinned in
  configuration. Clients may instead request audio data.

A new request interrupts the current utterance by default; queueing is opt-in
per request. Two utterances never play simultaneously.

Language for a request: explicit parameter, else script detection (Latin,
Cyrillic, and Armenian scripts are mutually unambiguous for the supported
languages), else the configured default.

### Playback control and position

The long-lived desktop host owns a `PlaybackController`, rather than leaving
each `PlaybackSink::play` call as an unobservable blocking operation. It owns
one active utterance, its cancellation and pause state, a source-audio timeline,
and the PCM already generated for that utterance. The controller drives WASAPI
on its worker thread; pausing stops the audio client without discarding the
current source position, resuming starts it again, and stopping cancels the
utterance and clears its queue. Convert submitted endpoint frames minus endpoint
padding into the source timeline using the negotiated sample rate and the seek
origin; never subtract frame counts from different sample rates. Position is
not wall-clock time. Seek flushes old endpoint buffers before restarting at the
new source position and preserves the paused state.

The controller retains source PCM for the current utterance while it is active,
so backward seek and seek within already-generated lookahead audio do not
invoke the model again. A forward target beyond generated audio is temporarily
unavailable; it becomes available as lookahead synthesis produces the required
audio. It never skips text by guessing a character or word offset. This keeps
seek deterministic across voices and languages while preserving the existing
fast-first-audio pipeline. `audio.playbackSkipSeconds` is an integer from one
to 30, default five, used by both seek buttons.

Retained history uses a per-utterance temporary PCM spool with a bounded RAM
cache, preserving backward seek without keeping the whole utterance in RAM.
Use an initial 64 MiB cache and 1 GiB spool ceiling; when storage is exhausted,
stop generating additional audio and show an actionable error rather than
silently evicting history. Delete the spool on stop, replacement, completion,
and orderly exit, and clean abandoned host-owned spools on startup. Pause also
applies backpressure: finish at most the in-flight synthesis request, then stop
lookahead until playback resumes. Synchronous runner cancellation still waits
for the current request; audio stop and UI feedback must not wait for it.

The controller publishes preparing/playing/paused/stopping/idle/error state and
source metadata to the UI. Capture failure leaves current speech untouched;
only successfully captured non-empty text submits an interrupting request.
Stop clears pending speech; natural completion starts the next queued request.
Closing Now Playing hides it without stopping speech. Display elapsed position
and available generated duration; do not invent a total duration before
synthesis completes. Request identities prevent late worker results from an
interrupted utterance updating the new utterance's UI or audio.

`audio.speechSpeed` is a floating-point multiplier from 0.5 through 2.0,
defaulting to 1.0. Now Playing changes the active utterance's requested speed,
but the PCM currently submitted to WASAPI finishes unchanged. At the next host
sentence boundary, the controller discards any unplayed lookahead and
re-synthesizes those still-pending chunks with the new value. This avoids
pitch-changing PCM resampling and needs no unreliable audio-to-word alignment.
The setting travels as the typed runner `synthesize.params.speed` field; do not
create a generic unvalidated engine-options map. Model-specific voice, accent,
pronunciation, and sampling controls wait until the bake-off identifies real
shipping capabilities and their typed contracts.

### Engine-bounded synthesis

The host's sentence-scale split is an engine-independent latency and
cancellation boundary; it is not proof that a chunk fits every model. An engine
runner that has a stricter post-tokenization input limit owns the final split,
because only it can measure its actual input representation.

Kokoro maps text to phoneme ids after espeak-ng. Its ONNX graph permits at most
510 mapped phoneme ids: the runner adds one id-0 pad token at each end, filling
the graph's 512-position input. When a host chunk exceeds that limit, the
Kokoro runner partitions the ids into consecutive batches of at most 510,
preferring the latest available sentence or phrase punctuation boundary and
otherwise splitting exactly at the limit. It synthesizes every batch in order;
no ids are discarded. Each batch uses the voice-style row for its own bounded
phoneme count. The runner returns the resulting PCM as ordered audio frames
under the original `synthesize` response, as already defined by
[ADR 0002](../adr/0002-runner-protocol.md). This keeps the model-specific limit
out of host splitting and makes runner failure an exceptional condition rather
than normal long-text control flow.

### Text-to-phoneme (espeak-ng)

espeak-ng converts request text to phonemes for the Kokoro engine. It runs as
an isolated subprocess, never linked (`docs/adr/0005-espeak-ng-licensing.md`),
invoked once per synthesis request — `espeak-ng -q --ipa=3 -v <voice> --
"<text>"` — with its stdout captured as the IPA phoneme string; no persistent
daemon or custom IPC framing. Kokoro's ONNX model consumes a custom, sparse
phoneme vocabulary rather than raw IPA, so the runner maps espeak-ng's output
through a static translation table ported from `hexgrad/misaki` (Apache-2.0).
Vendoring is asymmetric by platform: Windows fetches the official
`espeak-ng.msi` release asset via CMake `FetchContent` and extracts it with
7-Zip (no Windows Installer service involved), reassembling its flat file
list into an `espeak-ng-data/` layout `--path` expects; Linux, not yet a
release target, relies on a system-installed `espeak-ng`. Full rationale and
rejected alternatives: `docs/adr/0006-espeak-ng-vendoring-and-phoneme-mapping.md`.

## Local API

Two surfaces:

- An **OpenAI-compatible** speech endpoint returning a complete audio body, for
  client compatibility. It has no cancellation verb; that is inherent to the
  shape and is not worked around.
- A **native streaming** interface — incremental audio, explicit cancellation,
  playback control, and status — over WebSocket. It must not inherit the
  limitations of the compatibility endpoint.

The browser-selection prototype also accepts `POST /v1/selection` with a JSON
`{"text":"..."}` body. A non-empty selection submits one interrupting Host
request; an empty selection is rejected without changing playback. The route
is loopback-only and returns `Access-Control-Allow-Origin` only when the
request's exact origin appears in `server.allowedOrigins`. The Chrome extension
uses the configured Host port (default 7861); it reads `window.getSelection()`
in the active page and never accesses the clipboard.

The service binds `127.0.0.1:7861` by default. The port is configurable; on
collision the host fails to start with a clear error rather than moving, because
a browser extension cannot discover a port that moves.

No credential is required. `Access-Control-Allow-Origin` is sent only for an
explicit configured origin allowlist — the extension's `chrome-extension://<id>`
— never `*`. See `docs/adr/0003-no-authentication.md`.

## Configuration

`config.json` is canonical (`docs/adr/0004-json-canonical-config.md`). The
authoritative `config.schema.json` ships with the application. Installed mode
stores user configuration in the platform's per-user application data directory
and maintains a matching `schemas/` directory there, so relative schema
references also work in editors. Portable mode stores the configuration and
schemas beside the application. Writes are atomic, versioned, and retain the last
valid copy.

Illustrative shape, not yet a frozen schema:

```json
{
  "$schema": "./schemas/config.schema.json",
  "schemaVersion": 1,
  "server": {
    "host": "127.0.0.1",
    "port": 7861,
    "authentication": "none",
    "allowedOrigins": []
  },
  "audio": {
    "outputDevice": "system-default",
    "playbackSkipSeconds": 5
  },
  "selection": {
    "capturePolicy": "automatic"
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
      "loadPolicy": "onDemand",
      "fallbackProfile": "fast"
    },
    "armenian": {
      "model": "mms-hy",
      "device": "cpu",
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

A profile can set `fallbackProfile` to one other profile. If its model cannot
be resolved or its runner is unavailable, the host uses that fallback and logs
the reason. The initial English policy is `quality` first and CPU-only `fast`
as the fallback; `fast` names Kokoro. A failed synthesis request does not retry
through the fallback, because it may already have emitted audio.

### Live reload

The host watches `config.json` and applies changes on save: validate first, apply
only if valid, otherwise keep running on the last valid configuration and log the
failing JSON path.

Two implementation hazards, both to be designed in rather than discovered:

- The settings window writes the file the host is watching. The host must ignore
  file events caused by its own writes, and debounce, or an open settings window
  will be overwritten mid-edit or loop.
- `server.host`, `server.port`, and the portable-versus-installed data location
  cannot be applied live. The host reports that a restart is required instead of
  silently ignoring the change.

Devices, profiles, timeouts, registry directories, and log level all apply live.

### Repository artifacts

`config.example.json`, `config.schema.json`, and `model.schema.json` are tracked
in-repo. CI validates the example configuration and at least one example model
manifest against those schemas so documentation and accepted inputs cannot drift
independently.

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

A voice is identified by a stable ID scoped to its model package and exposed
through common metadata: display name, supported languages, and known
capabilities. A manifest declares packaged voice assets; an engine adapter may
also report built-in voices using the same host-facing representation.
Request-provided speaker prompts or cloning samples are synthesis inputs, not
installed voices, and are never added to a model package implicitly.

### Download catalogue

The curated catalogue of downloadable models, with pinned checksums, is compiled
into the application build and fetched over HTTPS. This introduces no new trust
root and no key management: the catalogue is exactly as trustworthy as the
binary the user already chose to run. The cost is that the catalogue is stale
until the next release, which is acceptable for a small curated set. A
runtime-fetched, signed catalogue is the upgrade path if the set ever needs to
change faster than releases.

Catalogue entries may carry any licence provided the terms are displayed before
download and non-commercial ones are badged; the project links to weights rather
than redistributing them. Manual installation by placing a folder in `models/`
always works regardless of the catalogue.

The outbound HTTPS fetch is WinHTTP, not Boost.Beast (the loopback API server's
client, above) -- Windows-first per product direction, so other platforms throw
a clear not-implemented error until they get their own fetch client. A
downloaded entry has no manifest on the remote host, so the host generates
`model.json` itself from the catalogue entry's own metadata before installing
through the same path a manual import takes.

## Engines and models

- **Kokoro-82M** — bundled with the application at full precision (ONNX, roughly
  330 MB plus voices), so the product speaks on first launch with no download.
  Apache-2.0 permits bundling. It is also the first engine implemented, because
  it is small, CPU-only, loads in about a second, and exercises the runner
  protocol without simultaneously debugging a GPU path.
- **Qwen3-TTS** — the quality default from the bake-off onward, for English and
  Russian, downloaded in-app. Whether it runs via llama.cpp GGUF or upstream
  PyTorch, and at what quantization, is decided by measurement in roadmap
  slice 2, not in advance; GGUF is quantized by definition, which is precisely
  what the comparison has to weigh.
- **LuxTTS** — optional lightweight voice-cloning comparison.
- **MMS Armenian** — offline Armenian fallback, non-commercial licence shown
  clearly before download.

Note that ONNX is a format and runtime, not a quality tier: the same weights in
ONNX and PyTorch produce the same audio. Quantization, not format, is where
quality is traded for size and speed.

Remote and cloud providers are out of scope for the first release. Gemma 3n/4
are not TTS engines — they accept audio and generate text, which is relevant to
future transcription or text cleanup, not synthesis.

References:

- https://www.boost.org/library/latest/beast/
- https://github.com/ggml-org/llama.cpp/blob/master/tools/tts/README.md
- https://doc.qt.io/qt-6/licensing.html
- https://nuitka.net/user-documentation/user-manual.html

## Desktop integration

The application runs as a **per-user background process started at login**, not
as a registered Windows Service: Session 0 isolation prevents a real service
from reaching the user's audio device or showing a tray icon. Autostart is on by
default with an obvious switch, because a background service that is not running
when a client calls it is the most likely support complaint.

Three UI surfaces, divided by task:

- **Tray menu** — current model/profile, pause/resume/stop, output device,
  global hotkey toggle, **Now Playing…**, Settings…, open logs, quit. Roughly
  eight items; beyond that a menu stops being usable.
- **Now Playing window** — a single modeless controller for the active
  utterance, opened from the tray and usable independently of Settings. It
  shows idle/reading/paused state, source and capture method, and the current
  position; its primary buttons are **Read selection**, **Read clipboard**,
  Pause/Resume, Stop, and signed seek buttons labelled with the configured
  interval. A disabled seek button means its target is not in generated audio.
  It is deliberately not a second settings form. There is no separate “Copy
  clipboard” action: **Read selection** runs the configured capture policy and
  surfaces the policy result, while **Read clipboard** explicitly reads the
  existing clipboard.
- **Settings window** — everything, as a form over `config.json`: model manager
  (install, download, remove, load/unload, idle timeout), server port and bind
  address, autostart, hotkey bindings, log level. Restart-requiring settings are
  marked. It opens independently of the tray (`tts-host --settings`), so a
  platform with no usable tray still has a full path to configuration.

The global hotkey and selection capture belong to the host: it is already
running with a tray, so a separate companion process would mean two background
processes for no gain. `hotkeys.readSelection` configures the global command;
an empty value disables it. Its focused capture field records a pressed chord of
modifiers plus a letter, digit, or F1–F24 key, then Settings probes it with
`RegisterHotKey` before saving. A failed probe means Windows or another
application currently owns the chord; Windows provides no global hotkey
inventory.

Desktop selection capture has three explicit policies, persisted in
`selection.capturePolicy`: `automatic` (the default), `uiAutomationOnly`, and
`clipboardOnly`. In `automatic`, the host snapshots the foreground target when
the command arrives, asks that target's focused UI Automation element for
`TextPattern` selection text, then sends Copy and waits for a changed clipboard
only when UI Automation offers no non-empty selection. The fallback dispatches
through a per-platform capture adapter, using the saved target identity and
refusing injection if focus has changed. The first prototype performs direct
capture only and requests manual copy on failure. Synthetic fallback is enabled
later only for tested focused-control adapters. Executable identity is a hint,
never proof of Copy semantics: editors and browsers can contain terminals, and
users can remap shortcuts. No default `Ctrl+C` is sent to unidentified controls.
Windows Terminal's default `Ctrl+Shift+C` is a compatibility candidate to test,
not a universal guarantee. Unknown targets and ambiguous controls fail closed.
Any later adapter must account for held hotkey modifiers, focus changes, and
key-release cleanup; it must not retry another chord after a timeout.

Global hotkeys snapshot the external window and focused element before Host UI
activation. Now Playing remembers the last external target and labels it next
to Read selection; clicking that button must not capture Host controls. If the
saved target is gone or its selection cannot be obtained directly, ask the user
to reselect and use the global shortcut. Do not steal focus to inject Copy.
Accessibility requests run off the UI thread with a bounded deadline and
discard late results; repeated timeouts must not create unbounded workers.
Never inspect password/protected controls. Capture diagnostics record method,
target identity, and failure reason, not selected text.

`uiAutomationOnly` never writes the clipboard. `clipboardOnly` reads the
existing clipboard without sending input; the user must copy the intended text
first, and stale clipboard text may be read. In `automatic`, the host labels the
method used and the reason for failure in Now Playing and the activity log; it
never treats an unchanged clipboard as a selection. UIPI, protected fields,
secure desktops, focus changes, and applications that expose neither TextPattern
nor a verified Copy action can all fail; **Read clipboard** remains the explicit
escape hatch. The host does not request UIAccess simply to cross elevation
boundaries: that requires a signed installed application and expands the
security boundary.

The Windows-specific `uiAutomationOnly` name remains scoped to the Windows
configuration for now; other platforms need native direct-capture labels and
an explicit schema compatibility decision before sharing saved policy values.
`clipboardOnly` means reading the existing clipboard without sending input;
**Read clipboard** uses that same clipboard path independently of the capture
policy. Do not enable synthetic Copy fallback until clipboard concurrency is
addressed: a changed
clipboard sequence alone cannot prove the target supplied the text. Preserve
restorable clipboard formats and never restore over a subsequent user's copy;
if ownership or preservation cannot be established, decline the fallback.

The browser client is equally first-class but has a more precise source: its
content script reads the page DOM selection and sends text to the local Host
after the extension's origin is allowlisted. It does not use the clipboard or
UI Automation for normal web-page text. Browser chrome, PDFs, and pages where
the extension is absent continue through desktop selection policy. Tray requests
run off the Win32 message loop: an interrupting request stops current playback
promptly and abandons synthesis after its current sentence; a queued request
waits behind it. A Windows Explorer context-menu entry is a registered shell
extension with a different install story and is out of scope.

## Distribution

The release artifact is a self-contained directory, zipped. A single
self-extracting executable is not the canonical format: large ML libraries make
it slower to start and harder to inspect, update, and repair.

```text
tts-host/
  tts-host.exe
  runtimes/
  runners/
  schemas/
  models/
    kokoro-en-v1/
  config.json
  portable.marker
```

Producing the zip is implemented today via CMake's `package` target (CPack,
`ZIP` generator; see `CMakeLists.txt`) — run it after a normal build. Its
current output is flatter than the tree above: the runner and its private
runtime library sit directly beside `tts-host`, matching the flat
`tts-host-<engine>-runner` naming convention `src/main.cpp` resolves against,
not yet the separate `runtimes/`/`runners/` split shown here. Reconciling the
two is pending a decision on per-engine runner isolation (raised but not yet
confirmed) — do not silently pick one shape when next touching this area.

The first release ships the zip only. An installer — shortcuts, start-at-login,
uninstall entry — comes with the cross-platform release work. Unzip-and-run
needs no code signing, whereas an unsigned installer triggers SmartScreen, which
is worse for a non-developer than a folder they double-click.

Updates are manual replacement. The host checks for a newer release and links to
it; it does not swap its own files while holding loaded models and a live
service.

If a Python-only model is ever needed, its compatibility runner can be built
with Nuitka standalone mode so the user still never installs Python.

### Data locations

If `portable.marker` exists beside the executable, configuration, models, logs,
and other mutable data live beside the executable. Otherwise per-user OS
directories are used: `%LOCALAPPDATA%\TTS Host`, `~/.config/tts-host` plus
`~/.local/share/tts-host`, `~/Library/Application Support/TTS Host`. A
`--data-dir` flag overrides both and is what tests use. A marker file beats an
environment variable or registry key because it survives being copied to a USB
stick, which is the point of portable mode.

Logs are written to a rotating file in the data directory and to stderr, at info
level by default, with `--verbose` for debug. A background process with no
console needs a file, or its actionable errors reach nobody.

## Platform strategy

- **Windows:** the implemented target. Native executable, native audio, global
  hotkeys, tray, CPU and NVIDIA release variants.
- **Linux:** designed, not built in the first release. Tray support varies by
  desktop environment — KDE implements StatusNotifierItem natively, GNOME needs
  an extension — so the tray is best-effort there and the settings window is
  always reachable without it. X11 and Wayland selection, active-window, and
  injected-input permissions differ materially; a Linux capture adapter must
  use the session's native accessibility/selection mechanism and report an
  unavailable capability rather than emulate the Windows fallback.
- **macOS:** designed, not built. Signed and notarized bundle, Apple Silicon and
  Metal first. Its capture adapter uses the macOS accessibility APIs and asks
  for the required Accessibility permission; it does not reuse Windows process
  classification or key injection rules.
- **WSL:** optional headless deployment using the same service contract, served
  by the `--headless` mode that already exists. Not part of the Windows
  dependency chain.

Platform-specific builds may contain different runner binaries while exposing
the same model/profile and API concepts.
