# ADR 0005 — espeak-ng runs as an isolated process, not linked code

- Status: accepted
- Date: 2026-08-28

## Decision

espeak-ng (GPL-3.0-or-later, code and phoneme data alike) is never linked —
statically or dynamically — into `tts-host` or into any Apache-2.0 runner
binary. It runs as its own separate executable/process, invoked over a pipe or
subprocess boundary the same way the Kokoro and stub runners are already
isolated from the host (ADR 0002). Only that one process carries GPL-3.0
obligations; the host and every other component stay Apache-2.0-clean.

## Why

espeak-ng is the practical, proven open-source text-to-phoneme engine for the
languages this product targets, but it is GPL-3.0, not Apache-2.0-compatible
for linking. Separate processes communicating over IPC (stdin/stdout, a pipe)
are the standard way to consume GPL functionality without the combined work
becoming a single "work based on the Program" under GPL's linking-based
copyleft trigger. The project already pays this architectural cost for
unrelated reasons — runners are separate processes for crash containment and
protocol versioning (ADR 0002) — so isolating espeak-ng this way is free reuse
of an existing boundary, not new architecture.

## Rejected

- **Link espeak-ng into `tts-host-kokoro-onnx-runner`.** Would make that
  runner's distributed binary subject to GPL-3.0 source-availability
  obligations. Avoidable at no cost given the existing process boundary.
- **Find a non-GPL phonemizer instead.** Would avoid the question entirely,
  but no evaluated alternative meets espeak-ng's language coverage and
  maturity; revisit only if phonemization quality or licensing become a
  problem in practice.

## Revisit when

A non-GPL phonemizer of comparable quality becomes available, or the
subprocess boundary proves too costly (latency, packaging) to keep paying.
