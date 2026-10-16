# ADR 0009 — Bundle the evaluated Qwen package without an English bake-off

- Status: accepted
- Date: 2026-10-16

## Decision

The bundled Qwen3-TTS package is the configuration already integrated and
verified through the Host:

- **Qwen3-TTS-12Hz-1.7B-Base** with a packaged, licensed speaker-reference clip
- **Q4_K_M** backbone and **Q8_0** mmproj GGUF files
- the `qwen3-gguf` runner on pinned **llama.cpp b11029** (commit
  `5c53396b89b05666c9d57445b7616a35f6198a16`) with CUDA

The English model bake-off is not run. Catalogue, update, packaging, and
installer work proceed with this package. The `qwen3-gguf` runner is the release
runner, and its limits (speed 1.0 only, VRAM unmeasured) become release work
rather than evaluation caveats.

Bundling Qwen does not make it a profile default. Kokoro stays the default
until Qwen is measured against the
[default-model criteria](../requirements/product.md#acceptance-criteria-for-default-model-selection)
or the user explicitly changes those criteria. A real-time factor above 1.0
remains a hard failure for a default.

## Why

The user chose to prioritize shipping both bundled models over further model
comparison (2026-10-16). This package is the only candidate already working end
to end with the Host on Windows and Linux: resident synthesis, cancellation,
recovery, and request rejection. It was also the closest to real time in the
September pilots. Measuring other candidates would first require new runner
support for each one.

## Consequences

- Measured throughput misses the quality target: the 1.22 RTF pilot, and about
  1.5 RTF warm in the October 16 Linux integration run (a short sentence, not
  the corpus). Qwen is selectable, not default.
- Peak VRAM remains unmeasured, so the ≤ 6 GB quality memory criterion cannot be
  claimed.
- A different size, quantization, or runtime is a later catalogue addition or a
  superseding ADR, not a reopened gate on distribution work.

## Rejected

- **Run the bake-off first** (Kokoro, Qwen 0.6B/1.7B at several
  quantizations, a lightweight alternative, llama.cpp versus PyTorch): it
  delays distribution and needs per-candidate runner work. Superseded by this
  decision.
- **Ship Qwen 0.6B instead:** not integrated or verified through the Host.
- **Upstream PyTorch runtime:** needs a bundled Python/PyTorch stack and was
  slower in the pilots (3.28 and 3.66 RTF).
