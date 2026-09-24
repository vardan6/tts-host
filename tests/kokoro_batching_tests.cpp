#include "tts_host/kokoro_batching.hpp"

#include <iostream>
#include <numeric>
#include <stdexcept>
#include <vector>

namespace {

std::vector<std::int64_t> ids(std::size_t count) {
  std::vector<std::int64_t> result(count);
  std::iota(result.begin(), result.end(), 20);
  return result;
}

void expect_lossless_and_bounded(const std::vector<std::int64_t> &input, std::size_t expected_batches) {
  const auto batches = tts_host::split_kokoro_phoneme_ids(input);
  if (batches.size() != expected_batches) throw std::runtime_error("unexpected batch count");
  std::vector<std::int64_t> joined;
  for (const auto &batch : batches) {
    if (batch.empty() || batch.size() > tts_host::kKokoroMaximumPhonemeIds) {
      throw std::runtime_error("batch is empty or exceeds Kokoro's input limit");
    }
    joined.insert(joined.end(), batch.begin(), batch.end());
  }
  if (joined != input) throw std::runtime_error("batches did not preserve every phoneme id in order");
}

void handles_model_boundary() {
  expect_lossless_and_bounded(ids(509), 1);
  expect_lossless_and_bounded(ids(510), 1);
  expect_lossless_and_bounded(ids(511), 2);
}

void handles_long_unpunctuated_selection() {
  // Models see phoneme ids, not the clipboard's source text. This represents
  // a long selection that espeak-ng mapped without a usable punctuation break.
  expect_lossless_and_bounded(ids(tts_host::kKokoroMaximumPhonemeIds * 3 + 17), 4);
}

void prefers_latest_phrase_boundary() {
  auto input = ids(511);
  input[499] = 4;
  const auto batches = tts_host::split_kokoro_phoneme_ids(input);
  if (batches.size() != 2 || batches.front().size() != 500) {
    throw std::runtime_error("batching did not prefer the latest phrase boundary");
  }
  expect_lossless_and_bounded(input, 2);
}

void frames_large_pcm_without_losing_samples() {
  std::vector<std::uint8_t> pcm(tts_host::kMaximumRunnerAudioFramePayloadBytes + 2, 0x7f);
  const auto frames = tts_host::frame_kokoro_pcm_s16le(pcm, 7);
  if (frames.size() != 2 || frames[0].sequence_number != 7 || frames[1].sequence_number != 8 ||
      frames[0].payload.size() != tts_host::kMaximumRunnerAudioFramePayloadBytes ||
      frames[1].payload.size() != 2 || frames[0].flags != 0 || frames[1].flags != 0 ||
      frames[0].sample_count + frames[1].sample_count != pcm.size() / 2) {
    throw std::runtime_error("PCM framing did not preserve protocol-valid ordered frames");
  }
}

}  // namespace

int main() {
  try {
    handles_model_boundary();
    handles_long_unpunctuated_selection();
    prefers_latest_phrase_boundary();
    frames_large_pcm_without_losing_samples();
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
  return 0;
}
