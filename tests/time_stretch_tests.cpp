#include "tts_host/time_stretch.hpp"

#include <cmath>
#include <cstddef>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

constexpr std::uint32_t kSampleRate = 24000;
constexpr double kPi = 3.14159265358979323846;

void require(bool condition, const std::string &message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

std::vector<float> sine(double frequency, std::size_t count) {
  std::vector<float> samples(count);
  for (std::size_t n = 0; n < count; ++n) {
    samples[n] = static_cast<float>(0.5 * std::sin(2.0 * kPi * frequency * static_cast<double>(n) / kSampleRate));
  }
  return samples;
}

// Speech-like test signal: a pitched tone whose loudness varies like syllables.
std::vector<float> voiced(std::size_t count) {
  auto samples = sine(180.0, count);
  for (std::size_t n = 0; n < count; ++n) {
    samples[n] *= static_cast<float>(0.6 + 0.4 * std::sin(2.0 * kPi * 4.0 * static_cast<double>(n) / kSampleRate));
  }
  return samples;
}

std::vector<float> stretch(const std::vector<float> &input, double speed, std::size_t chunk) {
  tts_host::TimeStretcher stretcher(kSampleRate, speed);
  std::vector<float> output;
  for (std::size_t offset = 0; offset < input.size(); offset += chunk) {
    const auto count = std::min(chunk, input.size() - offset);
    const auto part = stretcher.push(input.data() + offset, count);
    output.insert(output.end(), part.begin(), part.end());
  }
  const auto tail = stretcher.finish();
  output.insert(output.end(), tail.begin(), tail.end());
  return output;
}

// Frequency from rising zero crossings over the middle half, away from edges.
double dominant_frequency(const std::vector<float> &samples) {
  const auto begin = samples.size() / 4;
  const auto end = samples.size() * 3 / 4;
  int crossings = 0;
  for (auto n = begin + 1; n < end; ++n) {
    if (samples[n - 1] < 0.0f && samples[n] >= 0.0f) {
      ++crossings;
    }
  }
  return crossings * static_cast<double>(kSampleRate) / static_cast<double>(end - begin);
}

double rms(const std::vector<float> &samples) {
  const auto begin = samples.size() / 4;
  const auto end = samples.size() * 3 / 4;
  double sum = 0.0;
  for (auto n = begin; n < end; ++n) {
    sum += static_cast<double>(samples[n]) * samples[n];
  }
  return std::sqrt(sum / static_cast<double>(end - begin));
}

void normal_speed_passes_samples_through_unchanged() {
  const auto input = voiced(12345);
  require(stretch(input, 1.0, 1000) == input, "speed 1.0 changed the samples");
}

void output_length_is_input_length_divided_by_speed() {
  const auto input = voiced(kSampleRate);
  for (const double speed : {0.5, 0.75, 1.25, 1.5, 2.0}) {
    const auto expected = static_cast<std::size_t>(std::llround(kSampleRate / speed));
    require(stretch(input, speed, 4096).size() == expected,
            "speed " + std::to_string(speed) + " produced the wrong output length");
  }
}

void output_does_not_depend_on_how_input_was_split() {
  const auto input = voiced(kSampleRate);
  for (const double speed : {0.5, 1.5, 2.0}) {
    const auto whole = stretch(input, speed, input.size());
    for (const std::size_t chunk : {1u, 7u, 480u, 4093u}) {
      require(stretch(input, speed, chunk) == whole,
              "speed " + std::to_string(speed) + " output changed with chunk size " + std::to_string(chunk));
    }
  }
}

void stretching_preserves_pitch_and_loudness() {
  const auto input = sine(200.0, kSampleRate);
  for (const double speed : {0.5, 0.75, 1.5, 2.0}) {
    const auto output = stretch(input, speed, 2048);
    const auto frequency = dominant_frequency(output);
    require(std::abs(frequency - 200.0) < 6.0,
            "speed " + std::to_string(speed) + " shifted pitch to " + std::to_string(frequency) + " Hz");
    require(std::abs(rms(output) - rms(input)) < 0.1 * rms(input),
            "speed " + std::to_string(speed) + " changed loudness");
  }
}

void out_of_range_speed_and_misuse_are_rejected() {
  for (const double speed : {0.49, 2.01, std::nan("")}) {
    bool rejected = false;
    try {
      tts_host::TimeStretcher stretcher(kSampleRate, speed);
    } catch (const std::invalid_argument &) {
      rejected = true;
    }
    require(rejected, "out-of-range speed was accepted");
  }
  tts_host::TimeStretcher stretcher(kSampleRate, 1.5);
  stretcher.finish();
  bool rejected = false;
  const float sample = 0.0f;
  try {
    stretcher.push(&sample, 1);
  } catch (const std::logic_error &) {
    rejected = true;
  }
  require(rejected, "input after finish was accepted");
}

}  // namespace

int main() {
  try {
    normal_speed_passes_samples_through_unchanged();
    output_length_is_input_length_divided_by_speed();
    output_does_not_depend_on_how_input_was_split();
    stretching_preserves_pitch_and_loudness();
    out_of_range_speed_and_misuse_are_rejected();
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
  std::cout << "time stretch tests passed\n";
  return 0;
}
