#include "tts_host/kokoro_batching.hpp"

#include <algorithm>
#include <stdexcept>

namespace tts_host {
namespace {

bool is_phrase_punctuation(std::int64_t token_id) {
  switch (token_id) {
    case 1: case 2: case 3: case 4: case 5: case 6: case 9: case 10:
      return true;
    default:
      return false;
  }
}

}  // namespace

std::vector<std::vector<std::int64_t>> split_kokoro_phoneme_ids(
    const std::vector<std::int64_t> &phoneme_ids) {
  std::vector<std::vector<std::int64_t>> batches;
  std::size_t begin = 0;
  while (begin < phoneme_ids.size()) {
    const std::size_t limit = std::min(begin + kKokoroMaximumPhonemeIds, phoneme_ids.size());
    std::size_t end = limit;
    if (limit < phoneme_ids.size()) {
      for (std::size_t index = limit; index > begin; --index) {
        if (is_phrase_punctuation(phoneme_ids[index - 1])) {
          end = index;
          break;
        }
      }
    }
    batches.emplace_back(phoneme_ids.begin() + static_cast<std::ptrdiff_t>(begin),
                         phoneme_ids.begin() + static_cast<std::ptrdiff_t>(end));
    begin = end;
  }
  return batches;
}

std::vector<RunnerAudioFrame> frame_kokoro_pcm_s16le(const std::vector<std::uint8_t> &pcm,
                                                      std::uint64_t first_sequence_number) {
  if (pcm.size() % 2 != 0) {
    throw RunnerProtocolError("Kokoro emitted PCM with an incomplete S16LE sample");
  }
  std::vector<RunnerAudioFrame> frames;
  for (std::size_t offset = 0; offset < pcm.size();) {
    const std::size_t payload_size = std::min(kMaximumRunnerAudioFramePayloadBytes, pcm.size() - offset);
    frames.push_back({.sequence_number = first_sequence_number + frames.size(),
                      .sample_count = static_cast<std::uint32_t>(payload_size / 2),
                      .flags = 0,
                      .payload = {pcm.begin() + static_cast<std::ptrdiff_t>(offset),
                                  pcm.begin() + static_cast<std::ptrdiff_t>(offset + payload_size)}});
    offset += payload_size;
  }
  return frames;
}

}  // namespace tts_host
