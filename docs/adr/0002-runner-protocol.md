# ADR 0002 — Out-of-process runners, JSON-RPC control, framed binary audio

- Status: accepted
- Date: 2026-08-27

## Decision

Inference runs in separate runner processes, one per loaded model. The host
speaks to a runner over a control channel and a dedicated audio pipe:

- **Control:** JSON-RPC 2.0 with LSP-style `Content-Length` framing over the
  runner's stdin (host to runner) and stdout (runner to host).
- **Audio:** a unidirectional, host-created OS pipe. The runner inherits only
  its write end and the host retains the read end. The inherited endpoint is
  passed as `TTS_HOST_AUDIO_FD` on POSIX and `TTS_HOST_AUDIO_HANDLE` on Windows.
  The runner writes length-prefixed binary frames to it.
- **Diagnostics:** stderr is reserved for runner diagnostics and is never part
  of either protocol channel.

Audio frames use a fixed 20-byte, big-endian header: unsigned 32-bit payload
length, unsigned 64-bit sequence number, unsigned 32-bit sample count, and
unsigned 32-bit flags, followed by the payload bytes. The maximum payload is
16 MiB. Big-endian encoding makes the stream independent of host CPU byte
order; the payload format is negotiated separately by the synthesis protocol.

`synthesize` is a JSON-RPC request with a non-empty string `params.text`. Its
response identifies the PCM stream with `sampleRateHz`, `channels`,
`sampleFormat`, and `totalSampleFrames`. Protocol version 1 uses interleaved
signed 16-bit little-endian PCM (`pcm_s16le`); an audio-frame `sample_count` is
the number of sample frames (one frame contains one sample per channel). Frames
for a synthesis request are emitted in sequence-number order, and the final
frame sets flag bit 0 (`endOfStream`). The first implementation accepts one
synthesis request at a time, so the audio stream needs no request identifier.

The first call is `initialize`, which exchanges protocol version and engine
capabilities. Capabilities include `load`, `unload`, `synthesize`, `cancel`, and
`stats` — the last reporting peak RSS, VRAM, time-to-first-chunk, and sample
count.

A **stub runner** that emits synthetic audio without any model is a permanent
in-repo fixture, not scaffolding. It is what the end-to-end test drives in CI.

## Why

Runners are the only place framework-specific dependencies live, so a native
ONNX runner, a llama.cpp runner, and a packaged Python runner can coexist
without ABI or dependency conflict, and a runner crash cannot take down the
desktop host.

JSON-RPC 2.0 is off-the-shelf, versioned, and well understood, which is a better
use of effort than designing a control protocol. It does **not** solve binary
audio — base64 inside JSON messages would cost roughly a third in size and force
the whole payload through a JSON parser — hence the separate binary channel.

Pipes rather than a loopback socket per runner: no port allocation, no
host-to-runner authentication question, and process death closes the channel for
free. Splitting the audio pipe from stdin/stdout preserves ordinary JSON-RPC
request/response semantics and leaves stderr usable for diagnostics.

`stats` is specified in the first protocol version, before it is needed, because
the model bake-off (roadmap slice 2) has to measure VRAM, RSS, and
time-to-first-audio. Without it, that slice builds throwaway tooling or measures
by hand.

The stub runner exists so the protocol has a fast, deterministic conformance
test that needs no GPU and no model weights in CI.

## Rejected

- **In-process engine plugins (DLL/`.so`).** C++ ABI conflicts between engines,
  and any engine crash kills the host.
- **A custom binary framing for both control and audio.** More to specify, less
  tooling, no benefit over JSON-RPC on the control path.
- **Audio inline in JSON-RPC as base64.** Size and parse cost on the latency
  path.

## Runner failure

If a runner dies mid-synthesis, the host restarts it and retries the failed
chunk exactly once, then fails the request with an actionable error. Every
restart is logged; a model that dies repeatedly must be visible.
