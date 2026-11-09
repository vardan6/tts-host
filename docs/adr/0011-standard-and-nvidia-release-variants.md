# ADR 0011 — Standard and NVIDIA Windows release variants

- Status: accepted
- Date: 2026-11-03
- Amends: [ADR 0010](0010-model-revisions-storage-and-release-layout.md)
  (its "one Windows package carries the CUDA libraries" consequence)

## Decision

Windows releases ship two variants, built from one tree by one script:

- **Standard** (the recommended download): Kokoro and `runners/kokoro-onnx/`
  only, CPU inference, about 0.4 GB. Published as an installer and a portable
  zip.
- **NVIDIA**: the standard content plus the ADR 0009 Qwen package and
  `runners/qwen3-gguf/` with its CUDA libraries, about 2.1 GB. Published as an
  installer only, because its zip (2.40 GB) exceeds GitHub's 2 GiB per-file
  limit.

Both installers share one Inno Setup `AppId`, so either variant upgrades the
other in place, and per-user data survives the switch. The installers download
nothing. In the standard variant, Settings shows a catalogue model whose runner
is absent (Qwen) as needing the NVIDIA variant and offers no download.

Each release ships the standard variant; the NVIDIA variant ships when it is
ready, and alpha releases may omit it (`v0.1.0-alpha.1` is standard-only).
Building, packaging, and verifying the standard variant needs no Qwen asset
and no NVIDIA GPU. Every variant a release ships keeps its complete-file and
speech checks.

## Why

The two-model package is 2.1–2.4 GB, of which Qwen and its CUDA runtime are
about 2.5 GB of the 2.8 GB installed tree. Qwen needs an NVIDIA GPU and is not a
default (ADR 0009); the user judged it not yet better than Kokoro (2026-11-03).
Most testers would download weight they cannot use, and the full zip cannot be
published on GitHub at all. This is the size condition under which ADR 0010
allowed a split.

## Rejected alternatives

- **Optional Qwen component downloaded by the installer:** needs a download
  and unpack path for the runtime in Inno Setup, plus internet at install time,
  for a model the user has not yet chosen.
- **Mirroring models and runtimes into this repository's GitHub releases:**
  re-uploads about 2.5 GB per release and takes on redistribution obligations.
  The catalogue already pins upstream files (Hugging Face, llama.cpp GitHub
  releases) by revision and SHA-256, which gives the same integrity guarantee.
- **Kokoro on CUDA ONNX Runtime:** adds about 1 GB of CUDA and cuDNN libraries
  for a model that already streams on CPU (2.7 s to first chunk, no VRAM).
  DirectML is the cheaper GPU path if one is ever needed.

## Consequences

- `TTS_HOST_RELEASE` packaging checks the required files of the variant being
  built, not always both models.
- Settings and the catalogue need a "runner absent" state distinct from
  unsupported hardware.
- Intended follow-up (user, 2026-11-03): one lightweight installer, where
  adding a model downloads its runner and runtime on demand, and the installer
  says so. That would replace the NVIDIA variant and needs a runtime-download
  design and its own ADR.
