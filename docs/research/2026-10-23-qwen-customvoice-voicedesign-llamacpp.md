# Qwen3-TTS CustomVoice and VoiceDesign via llama.cpp

Research note, 2026-10-23. Not citable until captured with `/planning-capture`.
Question from `roadmap.md`: do GGUF conversions of the 1.7B CustomVoice (named
speakers, no reference clip) and VoiceDesign (voice described in text) models
exist or can be produced, and does the pinned (b11029) or a newer llama.cpp run
them? PyTorch stays rejected
([ADR 0009](../adr/0009-qwen-release-selection-without-bake-off.md)).

## Findings

**Upstream llama.cpp synthesizes Base only.** The Qwen pipeline in
`tools/mtmd/mtmd-helper-gen.cpp` (master, checked at b11840, 2026-10-23) builds
the prompt as role tokens, think/language codec rows, an *optional* speaker
x-vector from `--tts-speaker-file`, then the text. It has no input for a named
speaker and no instruction turn. `tools/tts/README.md` documents only Base,
`--tts-lang`, and `--tts-speaker-file`. Our runner calls the same
`mtmd_helper_gen_audio_*` functions (`src/qwen_api.hpp`), so it inherits these
limits whatever the model file contains.

**Upstream can now convert CustomVoice, but not speak with its voices.**
`convert_hf_to_gguf.py` crashed on CustomVoice (`KeyError:
'speaker_encoder_config'`,
[issue #29088](https://github.com/ggml-org/llama.cpp/issues/29088)); fixed by
[PR #29179](https://github.com/ggml-org/llama.cpp/pull/29179), merged
2026-10-07, which is after our pinned b11029 (2026-09-18). The converter turns
every `*_id` field of `talker_config` into a codec control token, so a
CustomVoice file should carry its speaker IDs as vocabulary tokens. The
pipeline never inserts them, so with no reference clip it would speak with no
speaker conditioning, not as a named speaker. The same applies to
VoiceDesign: the file may convert, but the instruction is never sent.

**Community conversions exist for both models; none target stock llama.cpp
synthesis with speaker or instruction support:**

| Repository | Model | Runtime | Speakers / instruction |
| --- | --- | --- | --- |
| [BricksDisplay](https://huggingface.co/BricksDisplay/Qwen3-TTS-12Hz-1.7B-CustomVoice-GGUF) (2026-10-20) | CustomVoice, bf16…q2_k + separate `codec-*.gguf` | [codec.cpp](https://github.com/mybigday/codec.cpp) `tts-cli` (bundles llama.cpp for the backbone; CPU, CUDA, Vulkan, Metal) | `--voice ryan --instruct "…"` |
| [sakasegawa](https://huggingface.co/sakasegawa/Qwen3-TTS-12Hz-1.7B-CustomVoice-GGUF) (2026-09-29) | CustomVoice, Q8_0…Q4_K, single file | [speech.cpp](https://github.com/nyosegawa/speech.cpp) only; its own layout, explicitly not readable by llama.cpp; Metal/Vulkan/CPU | `--voice`, `--instructions`, streaming chunks |
| [empyrealworks](https://huggingface.co/empyrealworks/Qwen3-TTS-12Hz-1.7B-VoiceDesign-GGUF) (2026-09-29) | VoiceDesign, Q4_K backbone + mmproj (llama.cpp naming) | not documented (no README) | unverified |
| khimaros, lainlives, justmaier, dbrains (April–May 2026) | CustomVoice / VoiceDesign | predate upstream Qwen support; runtime unverified | unverified |

The licence of the upstream models is Apache-2.0; each conversion must be
checked individually before cataloguing.

## Options

1. **Wait for upstream.** Upstream would need a named-speaker input (one codec
   row in the slot the x-vector uses) and an instruction turn. The prompt
   change is small, but nobody has proposed it upstream, so there is no date.
   Our runner would gain it through a pin bump and a new typed field in the
   runner protocol.
2. **Carry a patch on our llama.cpp pin.** Same change, built by us instead of
   tracking a pinned upstream release. This departs from the pinned b11029
   build in the [engine design](../design/architecture.md#engines-and-models)
   and ADR 0009, so it would need an ADR.
3. **Adopt codec.cpp as a separate runner.** It already does CustomVoice
   speakers and instructions with CUDA, using llama.cpp for the backbone. This
   means a new engine, packaging, and catalogue format, so it would need an
   ADR. Its streaming behaviour, Windows CUDA build, and RTF are unmeasured.
4. **speech.cpp.** Not suitable as-is: no CUDA backend is documented, and
   its Windows support is unverified.

None of this is measured here: no conversion was run, nothing was synthesized,
and no RTF or VRAM figures exist for CustomVoice or VoiceDesign on GGUF. The
September 18 PyTorch BF16 CustomVoice pilot (3.66 RTF for 1.7B, `progress.md`)
does not transfer to a GGUF runtime.

## Open questions for planning

- Is a named speaker worth an engine or provenance change (options 2/3), or
  only worth a pin bump once upstream supports it (option 1)?
- Is VoiceDesign in scope, or only CustomVoice's fixed speakers? VoiceDesign
  needs an instruction text per profile, which the per-model options plan
  (`roadmap.md`) would have to type.
- If option 3 is chosen: a measurement slice on Windows CUDA for RTF, first
  chunk, and VRAM before any catalogue entry.
