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
in the first protocol version because default-model selection must measure
those numbers, and building it later means building throwaway tooling first.

A stub runner emitting synthetic audio with no model is a permanent in-repo
fixture and the basis of the end-to-end test.

Runner death and deadline recovery, complete-chunk publication, and cancellation
follow [ADR 0002](../adr/0002-runner-protocol.md#runner-failure). Operation and
shutdown limits are defined by `RunnerDeadlines` in `runner_launcher.hpp`.

## Speech pipeline

```text
text ── normalize ── split ── synthesize (lookahead) ── play or stream
```

- **Normalize.** Markdown and HTML are converted to speakable text in the host,
  so every client benefits: code blocks announced and skipped, inline code read,
  links reduced to their text, tables skipped with a marker, headings given a
  leading pause. Number, date, and abbreviation expansion is left to the models
  in the first release.
- **Split.** The host splits into sentence-scale chunks so playback can begin
  before the whole utterance is synthesized.
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
lookahead until playback resumes. Runner waits observe Stop and replacement;
terminate the pending session without retry, per
[ADR 0002](../adr/0002-runner-protocol.md#runner-failure).

The portable coordinator in `synthesis_pipeline.cpp` keeps one lookahead task
owning runner I/O while the caller services retained playback and seek. Restart
retained audio without waiting for unrelated synthesis; preserve pause state
and join the previous endpoint before launching another. On failure, cancel
pending synthesis and join playback before destroying their shared resources.

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
An engine with a native speed input uses it (Kokoro); one without stretches its
own output in the runner while preserving pitch (Qwen).
The setting travels as the typed runner `synthesize.params.speed` field; do not
create a generic unvalidated engine-options map. Model-specific voice, accent,
pronunciation, and sampling controls wait until a shipped engine needs them and
their typed contracts are defined.

### Utterance audio stream

One utterance owns one endpoint stream: resolve the device, activate, and
`Initialize` once when its first audio is ready, then feed every sentence's PCM
into that running client. Refill is event-driven
(`AUDCLNT_STREAMFLAGS_EVENTCALLBACK`); the stream is drained once, at the end
of the utterance, not after each sentence. Opening a client per sentence and
polling for an empty buffer measured about 80 ms of audible silence per
boundary ([finding 16](../reviews/code-review-2026-10-03-roadmap-slices-branch.md#finding-16)).
Pause stops the client; Stop, seek, device change, and replacement reset or
recreate it under the existing controller rules. When the next sentence is not
ready, the client plays silence rather than stopping, so an underrun does not
reopen the device.

Three `audio` values tune the stream; Settings' Playback tab edits them and its
**Reset to defaults** restores only that tab:

| Key | Range | Default | Applies |
|---|---|---|---|
| `bufferMilliseconds` | 20–200 | 50 | next utterance stream |
| `sentencePauseMilliseconds` | 0–1000 | 0 | next sentence boundary |
| `lookaheadSentences` | 1–4 | 1 | next sentence boundary |

The buffer is a request; the endpoint may round it up. The sentence pause is
inserted silence in source PCM, so it is retained, seekable, and counted in
position. Speed changes discard all unplayed lookahead, however deep.

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

The browser extension accepts `POST /v1/selection` with a JSON
`{"text":"..."}` body. Non-empty text submits one interrupting Host request;
empty text is rejected without changing playback. The route is loopback-only
and returns `Access-Control-Allow-Origin` only when the request's exact origin
appears in `server.allowedOrigins`. The Chrome toolbar action reads
`window.getSelection()`; the page context menu's **Read article** action sends
the longest non-empty visible text from an `<article>` or `<main>` element, with
visible `body` text as a fallback. Both use the configured Host port (default
7861), do not access the clipboard, and reject request bodies over 1 MiB rather
than truncating page text.

Browser-local controls use the native WebSocket route `/v1/playback`. Each text
message is a JSON object with `command`: `status`, `pause`, `resume`, or `stop`.
Each valid command returns the current `state` (preparing/playing/paused/stopping/
idle/error), `positionSeconds`, `generatedSeconds`, and `error`. These are Host
playback controls, including speech started from desktop sources; this route
does not deliver PCM to the browser. Stop cancels active playback and clears
pending reads. Closing a connection has no playback effect. Unknown commands,
malformed JSON, and binary messages return an error without changing playback.
Commands are limited to 4 KiB; idle connections expire after 15 seconds.

WebSocket handshakes enforce the exact origin allowlist before upgrading;
HTTP CORS headers alone cannot protect WebSockets. Requests without an Origin
remain available to local non-browser clients, per ADR 0003. Asynchronous
connections share one API worker so an open control socket does not block read
requests or Host shutdown. The browser playback page polls status every 500 ms,
disables unavailable controls, and reconnects after a dropped connection.
Open it from the page or extension context menu, or from extension settings;
the toolbar continues to read selected text. Closing the page keeps speech
running. See `extension/playback.html` and `extension/playback.js`.

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

For the `qwen3-gguf` evaluation adapter, `files` must declare non-empty local
`model` and `mmproj` GGUF files, `voice` reference audio, and `voiceLicense`
provenance/licence text. Registry validation checks their containment,
existence, and GGUF magic; the runner checks decoder/model compatibility on
load. Its protocol `modelPath` names the package's `model.json` descriptor so
the runner can validate and resolve all assets together; `voicePath`, if
present, must match the declared reference. Packages remain data-only.

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
than redistributing optional packages. The two bundled models must separately
meet redistribution terms. Manual installation by placing a folder in `models/`
always works regardless of the catalogue.

The outbound HTTPS fetch is WinHTTP, not Boost.Beast (the loopback API server's
client, above) -- Windows-first per product direction, so other platforms throw
a clear not-implemented error until they get their own fetch client. A
downloaded entry has no manifest on the remote host, so the host generates
`model.json` itself from the catalogue entry's own metadata before installing
through the same path a manual import takes.

### Model installation and updates

Bundled, locally imported, and catalogue-downloaded models use the same
validated data-only package and registry contracts. Local import and catalogue
addition already have Settings paths; extend those paths for Qwen rather than
introducing a second registry or downloader. Unsupported engine packages remain
visible with an actionable reason and never install executable runner code.

Model updates are user-triggered replacements using the build's compiled,
checksum-pinned catalogue. Catalogue freshness remains tied to application
releases; no new upstream polling or signed remote catalogue is introduced.
Identify package content revisions separately from stable model IDs so Settings
can distinguish installed/current/update-available packages. Choose the concrete
revision representation before implementing replacement.

Download and validate replacement content in staging before committing it.
Serialize replacement with model loading, and release any resident runner's
handles before switching the package. An active utterance must finish or be
explicitly stopped before replacement; never switch weights underneath it.
Preserve the working package on validation, download, or commit failure and
recover an interrupted replacement on the next start. Keep configuration
references stable, rescan after success, and report unavailable selected voices
rather than silently choosing a different voice. Replacement applies equally to
bundled packages; installed-mode updates belong in writable per-user storage,
not the application's protected installation directory. The release-layout
decision must define discovery precedence without ambiguous duplicate model IDs.

## Engines and models

- **Kokoro-82M** — bundled with the application at full precision (ONNX, roughly
  330 MB plus voices), so the product speaks on first launch with no download.
  Apache-2.0 permits bundling. It is also the first engine implemented, because
  it is small, CPU-only, loads in about a second, and exercises the runner
  protocol without simultaneously debugging a GPU path.
- **Qwen3-TTS** — the second bundled local model, alongside Kokoro: 1.7B Base,
  Q4_K_M/Q8_0 GGUF on llama.cpp b11029 CUDA, chosen without a bake-off per
  [ADR 0009](../adr/0009-qwen-release-selection-without-bake-off.md). It is
  selectable but not a profile default until it meets the default-model
  criteria. English and Russian are
  intended uses; native-language acceptance remains separate. Ship weights,
  voices/tokenizer assets, and runtime dependencies together so neither
  installation nor first synthesis needs networking or a user-installed Python.
  Additional Qwen variants may be offered through the catalogue once supported.
- **LuxTTS** — optional lightweight voice-cloning comparison.
- **MMS Armenian** — offline Armenian fallback, non-commercial licence shown
  clearly before download.

The evaluation selection below became the release package
([ADR 0009](../adr/0009-qwen-release-selection-without-bake-off.md)); the
bake-off it was meant to precede was not run.

Evaluation selection (approved by the user 2026-10-16):
start with **Qwen3-TTS-12Hz-1.7B-Base**, **Q4_K_M backbone** and **Q8_0
mmproj**, using a pinned **llama.cpp CUDA** build and a packaged, licensed
speaker-reference clip. Base uses a reference voice rather than CustomVoice's
named built-in speakers. This matches the closest-to-real-time local pilot:
1.22 RTF versus PyTorch BF16 CustomVoice at 3.28 (0.6B) and 3.66 (1.7B), as
recorded in `progress.md` (September 18). Those WSL pilots differ in model
family and execution path, so they motivate integration order rather than
establish runtime superiority or satisfy native Windows acceptance. All missed
the RTF limit; integration alone is not expected to fix throughput.

Current upstream evidence:
- [llama.cpp TTS usage](https://github.com/ggml-org/llama.cpp/blob/master/tools/tts/README.md)
  documents Qwen Base synthesis and speaker-reference input.
- [Published GGUF files](https://huggingface.co/ggml-org/Qwen3-TTS-12Hz-1.7B-Base-GGUF/tree/main)
  list the Q4_K_M backbone (1.04 GB) and Q8_0 mmproj (446 MB); file sizes are
  not peak VRAM measurements.
- [Upstream demo source](https://github.com/ggml-org/llama.cpp/blob/master/tools/tts/tts.cpp)
  describes itself as experimental and retrieves output after generation.
  The adapter uses the incremental library API instead of wrapping that CLI;
  a whole-WAV subprocess wrapper would not demonstrate first-chunk latency.
- [Qwen upstream](https://github.com/QwenLM/Qwen3-TTS) supplies PyTorch
  CustomVoice and Base APIs. PyTorch is not shipped (ADR 0009); any later
  default-model measurement must identify model family, voice, precision,
  runtime, and cold/warm conditions explicitly.

The trusted Qwen evaluation runner uses llama.cpp **b11029**, commit
`5c53396b89b05666c9d57445b7616a35f6198a16`. Its optional CMake target uses
pinned headers and an application-owned runtime directory configured with
`TTS_HOST_QWEN_RUNTIME_DIR`; a model cannot select libraries or an executable.
The Windows preparation tool pins the CUDA 12.4 x64 runtime archives by
SHA-256. This is a developer build layout; the release runner/runtime layout is
still to be decided (see [Distribution](#distribution)).
See [evaluation setup](../../README.md#qwen-evaluation).

The adapter keeps weights resident between requests, resets generation state
per sentence, and emits incremental PCM through the common runner protocol.
Stop uses the Host's bounded process cancellation and discards incomplete
sentences; engine failures use its existing one-restart recovery. A prompt
exceeding the context or a generation limit without end-of-speech is a request
rejection (ADR 0002): the reason is reported and the model stays resident
without a restart. Evaluation packages expose one declared
reference voice and declare exactly one language, `en` or `ru`, which every
request uses. Qwen has no speed input, so the runner applies the requested
speed with a streaming, pitch-preserving WSOLA time stretch of the generated
PCM (`time_stretch.cpp`); output is independent of how PCM arrives, so
incremental emission is kept. `stats` reports
RSS, time to first emitted PCM, and sample count, with VRAM measurement marked
unavailable. These limits must be addressed or accounted for before release
acceptance; first emitted runner PCM does not bypass the Host's complete-sentence
playback buffer or prove its audible latency target.

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
from reaching the user's audio device or showing a tray icon. Autostart is
offered by default with an obvious switch, because a background service that is
not running when a client calls it is the most likely support complaint.

Windows Host launch uses the GUI subsystem: Explorer and login launches create
no console. Headless/help invocations preserve inherited pipes/files and attach
to the parent's console when available; fatal desktop launch errors appear in a
native dialog and return a failing exit code.

Tray, browser, and hotkey reads share the Host's resident model manager with tray
Settings. Closing Settings leaves that session resident; independent `--settings`
owns its own temporary session. Successful sequential reads reuse the loaded
runner. A different model replaces the single resident slot. A speech lease
holds the slot through synthesis, lookahead, and playback; Settings load/unload
reports that the model is in use rather than blocking on the utterance. Stop or
runner failure follows ADR 0002's cancellation/recovery contract.

`loadPolicy: eager` warms a model before the first tray read; `onDemand` waits for
speech. With the current single slot, choose the first eager language-default
profile in configuration-key order, otherwise the first eager profile. Other
profiles load on demand. A failed warm-up reports the error without preventing
the Host from starting. Changes to profiles schedule warm-up between utterances;
an unchanged selection reuses its runner. Explicit Settings Load remains available
for either policy. Idle unloading runs while the tray is open, even with Settings
closed, and measures time since load or the end of the last utterance. Active or
paused speech never counts as idle. Optional profile `idleUnloadSeconds` overrides
`modelRegistry.idleUnloadSeconds` for that profile; zero disables unloading.

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
- **Settings window** — everything, as a form over `config.json` (plus the
  OS-owned login-startup switch), grouped into tabs: **General** (login startup,
  default profile, hotkeys, selection policy, log level), **Playback** (output
  device, speed, seek interval, and the stream tuning values below), **Models**
  (install, download, remove, load/unload, idle timeout, licences), and
  **Server** (port and bind address). The activity log stays visible below the
  tabs. It shows the Host's process-wide activity record: every failure or
  important message shown elsewhere (tray message boxes, Now Playing errors,
  runner restarts, eager-load failures, selection-capture results) is
  timestamped there too, including what happened while Settings was closed.
  The record is bounded, in memory only, and never holds selected or spoken
  text. Download progress is transient: each file's progress replaces its own
  newest entry and is shown at most every 250 ms, so a download cannot evict
  earlier messages; start, completion, and failure stay separate entries. Restart-requiring settings are marked. It opens independently of the tray (`tts-host --settings`), so a
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

Only a global-shortcut capture sets the saved target; the Host does not track
foreground changes. The saved target is the window, its process, and the
focused control's UI Automation RuntimeId, kept even when the selection read
failed. **Read selection** re-finds that RuntimeId under the saved window and
reads its TextPattern selection with no foreground check. The window, process,
and protected-field checks still apply. Now Playing reports failures in its
status line and in Activity rather than a dialog. Each queued request carries a
source label, which Now Playing shows once that request begins playing.

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

### Login startup

The operating system's own per-user startup registration is the only record of
this choice, per [ADR 0008](../adr/0008-os-owned-login-startup.md). The Settings
**General** tab shows one switch whose state is read from that registration each
time Settings opens or regains focus, so a change made in the system page shows
on return; changing it writes the registration immediately. Ordinary
launches, `--settings`, and headless runs never create, repoint, or remove it.
`startup.launchOnLogin` (default `true`) is only the pre-ticked default for the
first-run question and the installer. The registered command starts the normal
tray host with the executable and config path of the instance that wrote it.
Registration never requires elevation; a failure is reported in Settings and
does not prevent the Host from starting.

States shown by the switch: **on**, **off**, **turned off in system settings**,
and **points to another copy** (the entry exists but names a different
executable or config). Turning the switch on from the last state repoints the
entry to this copy, because the user asked for it explicitly. In the
system-disabled state the switch is not toggled programmatically; Settings
explains the state and offers **Open system startup settings**.

- **Windows:** `HKCU\Software\Microsoft\Windows\CurrentVersion\Run`, which
  Task Manager and Settings → Apps → Startup list automatically. Their on/off
  switch writes `HKCU\...\Explorer\StartupApproved\Run`; the Host reads that
  value (an odd first byte means disabled) but never writes it. Turning the
  switch off deletes the Run value and leaves `StartupApproved` alone. The page
  link opens `ms-settings:startupapps`.
- **Linux:** an XDG Autostart `.desktop` file in `$XDG_CONFIG_HOME/autostart`
  (default `~/.config/autostart`), listed by KDE Autostart and GNOME startup
  applications. `Hidden=true` or `X-GNOME-Autostart-enabled=false` means
  disabled by the user; a missing file means off. Under Flatpak, request
  autostart through the XDG Background portal instead of writing the file.
- **macOS:** `SMAppService.mainApp` (macOS 13+), listed in System Settings →
  General → Login Items. `requiresApproval` is the system-disabled state;
  `openSystemSettingsLoginItems()` is the page link.

First run: the first tray launch (not `--settings`, headless, or CLI runs) in a
data directory that has no startup-choice marker asks once, with a checkbox
pre-ticked from `startup.launchOnLogin`, then writes the marker whatever the
answer; closing the question without OK answers no. Only a yes registers. The
marker (`kStartupChoiceMarkerName` in `include/tts_host/startup_choice.hpp`)
is Host state in the data directory, not configuration. `compile-win.ps1`
development launches pre-create the marker so the developer machine is never
asked or registered (see `PROJECT.md`). The installer asks the same question and
writes the marker; uninstall removes the registration it owns.

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

The portable zip remains a release format. The Windows installer must also
ship both Kokoro and the ADR 0009 Qwen package plus their complete
runtimes; installation and first synthesis are offline. Packaging must fail
if either required package or dependency is missing rather than silently ship a
single-model distribution. The exact runner/runtime layout, writable model
storage, and redistribution notices are decided before installer implementation.
Its startup choice
follows [Login startup](#login-startup): pre-ticked from
`startup.launchOnLogin`, written to the OS registration, removed on uninstall. Unzip-and-run
needs no code signing, whereas an unsigned installer triggers SmartScreen, which
is worse for a non-developer than a folder they double-click.

Application updates are manual replacement. The host checks for a newer release and links to
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
  The first Linux selection target is KDE Plasma on Wayland, with Kubuntu as
  the primary desktop acceptance environment. Rocky Linux 8/9 with KDE are
  compatibility targets; detect their actual desktop/session and do not infer
  it from the distribution version. X11 capture is a later slice. Wayland
  capture must use application accessibility rather than assuming arbitrary
  access to other clients' focus, selections, or injected input. Missing
  accessibility support must produce actionable manual-copy guidance; browser
  DOM selection remains available independently. KDE-first does not authorize
  replacing the native-UI decision in ADR 0007 with a Qt UI dependency.

  Basis for session priority: the user prefers KDE/Kubuntu and uses Rocky 8/9;
  [Kubuntu 25.10 defaults to Plasma Wayland](https://www.kubuntu.com/news/kubuntu-25-10-questing-quokka-released/),
  and [KDE is prioritizing Wayland](https://blogs.kde.org/2025/11/26/going-all-in-on-a-wayland-future/).
- **macOS:** designed, not built. Signed and notarized bundle, Apple Silicon and
  Metal first. Its capture adapter uses the macOS accessibility APIs and asks
  for the required Accessibility permission; it does not reuse Windows process
  classification or key injection rules.
- **WSL:** optional headless deployment using the same service contract, served
  by the `--headless` mode that already exists. Not part of the Windows
  dependency chain.

Platform-specific builds may contain different runner binaries while exposing
the same model/profile and API concepts.
