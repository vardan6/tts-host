# ADR 0002 — Out-of-process runners, JSON-RPC control, framed binary audio

- Status: accepted
- Date: 2026-08-27

## Decision

Inference runs in separate runner processes, one per loaded model. The host
speaks to a runner over two channels on its standard streams:

- **Control:** JSON-RPC 2.0 with LSP-style `Content-Length` framing.
- **Audio:** a separate length-prefixed binary stream. Each chunk carries a
  header with sequence number, sample count, and flags.

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

Stdio rather than a loopback socket per runner: no port allocation, no
host-to-runner authentication question, and process death closes the channel for
free.

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
