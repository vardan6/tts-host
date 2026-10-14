#pragma once

#include "tts_host/runner_protocol.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace tts_host {

// Kokoro's BERT input has 512 positions; the runner reserves one id-0 pad
// token on each side of mapped phoneme ids.
inline constexpr std::size_t kKokoroMaximumPhonemeIds = 510;

// Splits mapped phoneme ids without dropping or reordering them. A batch ends
// at the latest phrase punctuation at or before the limit when possible.
std::vector<std::vector<std::int64_t>> split_kokoro_phoneme_ids(
    const std::vector<std::int64_t> &phoneme_ids);

// Emits valid mono S16LE protocol frames for PCM. No returned frame has the
// end-of-stream flag: the caller sets it on the final frame of the request.
std::vector<RunnerAudioFrame> frame_kokoro_pcm_s16le(const std::vector<std::uint8_t> &pcm,
                                                      std::uint64_t first_sequence_number);

}  // namespace tts_host
