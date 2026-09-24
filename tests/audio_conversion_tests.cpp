#include "tts_host/audio_conversion.hpp"

#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void require(bool condition, const std::string &message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

std::vector<std::uint8_t> pcm16(std::initializer_list<std::int16_t> samples) {
  std::vector<std::uint8_t> bytes;
  for (const auto sample : samples) {
    const auto raw = static_cast<std::uint16_t>(sample);
    bytes.push_back(static_cast<std::uint8_t>(raw));
    bytes.push_back(static_cast<std::uint8_t>(raw >> 8));
  }
  return bytes;
}

void converts_mono_s16_to_stereo_float_at_endpoint_rate() {
  const auto converted = tts_host::convert_pcm_s16le(
      pcm16({0, 32767}), 24'000, 1,
      {.sample_rate_hz = 48'000,
       .channels = 2,
       .bits_per_sample = 32,
       .encoding = tts_host::AudioSampleEncoding::ieee_float});
  require(converted.size() == 4 * 2 * sizeof(float),
          "24 kHz two-frame mono input did not resample to four stereo float frames");

  float first_left = 1.0f;
  float first_right = 1.0f;
  float midpoint_left = 0.0f;
  std::memcpy(&first_left, converted.data(), sizeof(float));
  std::memcpy(&first_right, converted.data() + sizeof(float), sizeof(float));
  std::memcpy(&midpoint_left, converted.data() + 2 * sizeof(float), sizeof(float));
  require(first_left == 0.0f && first_right == 0.0f,
          "mono source's first sample was not duplicated into endpoint stereo channels");
  require(midpoint_left > 0.49f && midpoint_left < 0.51f,
          "resampling did not linearly interpolate the midpoint");
}

void converts_stereo_s16_to_mono_24_bit_pcm() {
  const auto converted = tts_host::convert_pcm_s16le(
      pcm16({32767, -32767}), 24'000, 2,
      {.sample_rate_hz = 24'000,
       .channels = 1,
       .bits_per_sample = 24,
       .encoding = tts_host::AudioSampleEncoding::signed_integer});
  require(converted.size() == 3, "stereo input did not downmix to one 24-bit sample");
  require(converted[0] == 0 && converted[1] == 0 && converted[2] == 0,
          "opposing stereo samples did not mix to silence");
}

void maps_seek_positions_between_sample_rate_timelines() {
  require(tts_host::convert_frame_position(12'000, 24'000, 48'000) == 24'000,
          "source seek position did not scale to the endpoint sample rate");
  require(tts_host::convert_frame_position(24'000, 48'000, 24'000) == 12'000,
          "endpoint seek position did not map back to source frames");
}

void rejects_endpoint_format_that_cannot_be_encoded() {
  try {
    static_cast<void>(tts_host::convert_pcm_s16le(
        pcm16({1}), 24'000, 1,
        {.sample_rate_hz = 24'000,
         .channels = 1,
         .bits_per_sample = 64,
         .encoding = tts_host::AudioSampleEncoding::ieee_float}));
  } catch (const std::runtime_error &error) {
    require(std::string(error.what()).find("not supported") != std::string::npos,
            "unsupported endpoint format did not explain why conversion failed");
    return;
  }
  throw std::runtime_error("unsupported endpoint format was accepted");
}

}  // namespace

int main() {
  try {
    converts_mono_s16_to_stereo_float_at_endpoint_rate();
    converts_stereo_s16_to_mono_24_bit_pcm();
    maps_seek_positions_between_sample_rate_timelines();
    rejects_endpoint_format_that_cannot_be_encoded();
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
