#include "tts_host/time_stretch.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace tts_host {

namespace {

constexpr double kPi = 3.14159265358979323846;

// 30 ms frames overlapped by half, aligned within +/-10 ms: long enough to
// span a low voice's pitch period, short enough not to smear syllables.
constexpr double kFrameSeconds = 0.030;
constexpr double kToleranceSeconds = 0.010;

// The input position frame k is nominally taken from; its output position is
// k * hop, so input advances `speed` times faster than output.
std::int64_t nominal_position(std::uint64_t frame_index, std::size_t hop, double speed) {
  return std::llround(static_cast<double>(frame_index) * static_cast<double>(hop) * speed);
}

}  // namespace

TimeStretcher::TimeStretcher(std::uint32_t sample_rate, double speed) : speed_(speed) {
  if (sample_rate == 0) {
    throw std::invalid_argument("time stretch needs a positive sample rate");
  }
  if (!std::isfinite(speed) || speed < 0.5 || speed > 2.0) {
    throw std::invalid_argument("time stretch speed must be between 0.5 and 2.0");
  }
  hop_ = std::max<std::size_t>(1, static_cast<std::size_t>(std::lround(sample_rate * kFrameSeconds / 2)));
  frame_ = hop_ * 2;
  tolerance_ = static_cast<std::size_t>(std::lround(sample_rate * kToleranceSeconds));
  // A periodic Hann window at half-frame hop sums to exactly one.
  window_.resize(frame_);
  for (std::size_t n = 0; n < frame_; ++n) {
    window_[n] = static_cast<float>(0.5 - 0.5 * std::cos(2.0 * kPi * static_cast<double>(n) /
                                                         static_cast<double>(frame_)));
  }
}

std::vector<float> TimeStretcher::push(const float *samples, std::size_t count) {
  if (finished_) {
    throw std::logic_error("time stretch received input after finish");
  }
  if (speed_ == 1.0) {
    input_total_ += count;
    return std::vector<float>(samples, samples + count);
  }
  input_.insert(input_.end(), samples, samples + count);
  input_total_ += count;
  while (frame_ready(false)) {
    process_frame();
  }
  discard_consumed_input();
  return take_output(frame_index_ * hop_);
}

std::vector<float> TimeStretcher::finish() {
  if (finished_) {
    throw std::logic_error("time stretch finished twice");
  }
  finished_ = true;
  if (speed_ == 1.0) {
    return {};
  }
  while (frame_ready(true)) {
    process_frame();
  }
  return take_output(static_cast<std::uint64_t>(std::llround(static_cast<double>(input_total_) / speed_)));
}

bool TimeStretcher::frame_ready(bool final) const {
  const auto nominal = nominal_position(frame_index_, hop_, speed_);
  if (final) {
    // Beyond the end, input reads as silence; the final length is trimmed.
    return static_cast<std::uint64_t>(nominal) < input_total_;
  }
  // Every sample the alignment search or the overlap-add may read must be
  // present, so the result cannot depend on where the input was split.
  const auto needed = frame_index_ == 0 ? frame_ : nominal + tolerance_ + frame_;
  return needed <= input_total_;
}

void TimeStretcher::process_frame() {
  std::int64_t position = 0;
  if (frame_index_ > 0) {
    // Pick the segment near the nominal position that best continues the
    // previous segment's waveform, so overlap-add does not cancel the voice.
    const auto natural = previous_position_ + static_cast<std::int64_t>(hop_);
    const auto nominal = nominal_position(frame_index_, hop_, speed_);
    const auto tolerance = static_cast<std::int64_t>(tolerance_);
    double best = -std::numeric_limits<double>::infinity();
    for (auto candidate = std::max<std::int64_t>(0, nominal - tolerance); candidate <= nominal + tolerance;
         ++candidate) {
      double correlation = 0.0;
      for (std::size_t n = 0; n < hop_; ++n) {
        const auto offset = static_cast<std::int64_t>(n);
        correlation += static_cast<double>(input_at(candidate + offset)) * input_at(natural + offset);
      }
      if (correlation > best) {
        best = correlation;
        position = candidate;
      }
    }
  }

  const auto output_position = frame_index_ * hop_;
  const auto needed = static_cast<std::size_t>(output_position + frame_ - output_start_);
  if (output_.size() < needed) {
    output_.resize(needed, 0.0f);
  }
  const auto base = static_cast<std::size_t>(output_position - output_start_);
  for (std::size_t n = 0; n < frame_; ++n) {
    // Nothing overlaps the first half of the first frame, so it is not faded in.
    const float weight = frame_index_ == 0 && n < hop_ ? 1.0f : window_[n];
    output_[base + n] += input_at(position + static_cast<std::int64_t>(n)) * weight;
  }
  previous_position_ = position;
  ++frame_index_;
}

float TimeStretcher::input_at(std::int64_t position) const {
  if (position < 0 || static_cast<std::uint64_t>(position) >= input_total_ ||
      static_cast<std::uint64_t>(position) < input_start_) {
    return 0.0f;
  }
  return input_[static_cast<std::size_t>(static_cast<std::uint64_t>(position) - input_start_)];
}

std::vector<float> TimeStretcher::take_output(std::uint64_t end) {
  if (end <= output_start_) {
    return {};
  }
  const auto count = static_cast<std::size_t>(end - output_start_);
  if (output_.size() < count) {
    output_.resize(count, 0.0f);
  }
  std::vector<float> result(output_.begin(), output_.begin() + static_cast<std::ptrdiff_t>(count));
  output_.erase(output_.begin(), output_.begin() + static_cast<std::ptrdiff_t>(count));
  output_start_ = end;
  return result;
}

void TimeStretcher::discard_consumed_input() {
  const auto next_search = nominal_position(frame_index_, hop_, speed_) - static_cast<std::int64_t>(tolerance_);
  const auto next_natural = previous_position_ + static_cast<std::int64_t>(hop_);
  const auto keep_from = std::max<std::int64_t>(0, std::min(next_search, next_natural));
  if (static_cast<std::uint64_t>(keep_from) <= input_start_) {
    return;
  }
  const auto drop = std::min<std::size_t>(input_.size(),
                                          static_cast<std::size_t>(static_cast<std::uint64_t>(keep_from) - input_start_));
  input_.erase(input_.begin(), input_.begin() + static_cast<std::ptrdiff_t>(drop));
  input_start_ += drop;
}

}  // namespace tts_host
