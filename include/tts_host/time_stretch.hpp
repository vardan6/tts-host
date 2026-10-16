#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace tts_host {

// Pitch-preserving tempo change for engines without a native speed input
// (WSOLA: overlap-added input segments, each aligned to the previous one by
// cross-correlation). Streams mono float PCM: output depends only on the input
// samples, never on how they were split across push calls, so incremental
// runner output matches a whole-sentence stretch. `speed` is the 0.5-2.0
// synthesis multiplier; 1.0 passes samples through unchanged. See
// docs/design/architecture.md#playback-control-and-position.
class TimeStretcher {
 public:
  TimeStretcher(std::uint32_t sample_rate, double speed);

  // Accepts the next input samples and returns the output samples that no
  // later input can change.
  std::vector<float> push(const float *samples, std::size_t count);

  // Ends the input and returns the remaining output. The total output length
  // is round(input length / speed).
  std::vector<float> finish();

 private:
  bool frame_ready(bool final) const;
  void process_frame();
  float input_at(std::int64_t position) const;
  std::vector<float> take_output(std::uint64_t end);
  void discard_consumed_input();

  double speed_;
  std::size_t frame_;
  std::size_t hop_;
  std::size_t tolerance_;
  std::vector<float> window_;
  std::vector<float> input_;
  std::uint64_t input_start_ = 0;
  std::uint64_t input_total_ = 0;
  std::vector<float> output_;
  std::uint64_t output_start_ = 0;
  std::uint64_t frame_index_ = 0;
  std::int64_t previous_position_ = 0;
  bool finished_ = false;
};

}  // namespace tts_host
