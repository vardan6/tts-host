#include "tts_host/audio_conversion.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace tts_host {

std::uint64_t convert_frame_position(std::uint64_t source_frame,
                                     std::uint32_t source_sample_rate_hz,
                                     std::uint32_t destination_sample_rate_hz) {
  if (source_sample_rate_hz == 0 || destination_sample_rate_hz == 0) {
    throw std::invalid_argument("sample rates must be positive");
  }
  if (source_frame > std::numeric_limits<std::uint64_t>::max() /
                         destination_sample_rate_hz) {
    throw std::overflow_error("frame position conversion overflowed");
  }
  return source_frame * destination_sample_rate_hz / source_sample_rate_hz;
}
namespace {

std::int16_t read_s16le(const std::vector<std::uint8_t> &bytes, std::size_t offset) {
  const std::uint16_t raw = static_cast<std::uint16_t>(bytes[offset]) |
                            (static_cast<std::uint16_t>(bytes[offset + 1]) << 8);
  return static_cast<std::int16_t>(raw);
}

void append_signed_integer(std::vector<std::uint8_t> &output, float sample,
                           std::uint16_t bits_per_sample) {
  const float clamped = std::clamp(sample, -1.0f, 1.0f);
  const std::int64_t maximum = (std::int64_t{1} << (bits_per_sample - 1)) - 1;
  const std::int64_t minimum = -(std::int64_t{1} << (bits_per_sample - 1));
  const auto encoded = static_cast<std::int64_t>(std::lround(clamped * maximum));
  const std::int64_t bounded = std::clamp(encoded, minimum, maximum);
  const std::uint32_t raw = static_cast<std::uint32_t>(bounded);
  for (std::uint16_t byte = 0; byte < bits_per_sample / 8; ++byte) {
    output.push_back(static_cast<std::uint8_t>(raw >> (byte * 8)));
  }
}

void append_float32le(std::vector<std::uint8_t> &output, float sample) {
  const float clamped = std::clamp(sample, -1.0f, 1.0f);
  static_assert(sizeof(float) == sizeof(std::uint32_t));
  std::uint32_t raw = 0;
  std::memcpy(&raw, &clamped, sizeof(raw));
  for (std::uint16_t byte = 0; byte < 4; ++byte) {
    output.push_back(static_cast<std::uint8_t>(raw >> (byte * 8)));
  }
}

}  // namespace

std::vector<std::uint8_t> convert_pcm_s16le(const std::vector<std::uint8_t> &source,
                                            std::uint32_t source_sample_rate_hz,
                                            std::uint16_t source_channels,
                                            const AudioFormat &destination) {
  if (source_sample_rate_hz == 0 || source_channels == 0 || destination.sample_rate_hz == 0 ||
      destination.channels == 0 || source.size() % (source_channels * 2) != 0) {
    throw std::invalid_argument("invalid PCM conversion format or payload");
  }
  const bool integer_format = destination.encoding == AudioSampleEncoding::signed_integer;
  if ((integer_format && destination.bits_per_sample != 16 && destination.bits_per_sample != 24 &&
       destination.bits_per_sample != 32) ||
      (!integer_format && destination.bits_per_sample != 32)) {
    throw std::runtime_error("WASAPI endpoint format is not supported for PCM conversion");
  }

  const std::size_t source_frames = source.size() / (source_channels * 2);
  if (source_frames == 0) {
    return {};
  }
  const std::size_t destination_frames =
      (source_frames * static_cast<std::size_t>(destination.sample_rate_hz) +
       source_sample_rate_hz - 1) /
      source_sample_rate_hz;
  const std::size_t destination_bytes_per_frame =
      destination.channels * (destination.bits_per_sample / 8);
  std::vector<std::uint8_t> output;
  output.reserve(destination_frames * destination_bytes_per_frame);

  const auto source_sample = [&](std::size_t frame, std::uint16_t channel) {
    return static_cast<float>(read_s16le(source, (frame * source_channels + channel) * 2)) /
           32768.0f;
  };
  const auto sample_for_channel = [&](std::size_t frame, std::uint16_t output_channel) {
    if (destination.channels == 1 && source_channels > 1) {
      float sum = 0.0f;
      for (std::uint16_t channel = 0; channel < source_channels; ++channel) {
        sum += source_sample(frame, channel);
      }
      return sum / source_channels;
    }
    return source_sample(frame, std::min<std::uint16_t>(output_channel, source_channels - 1));
  };

  for (std::size_t output_frame = 0; output_frame < destination_frames; ++output_frame) {
    const double source_position = static_cast<double>(output_frame) * source_sample_rate_hz /
                                   destination.sample_rate_hz;
    const std::size_t left_frame = std::min<std::size_t>(static_cast<std::size_t>(source_position),
                                                         source_frames - 1);
    const std::size_t right_frame = std::min(left_frame + 1, source_frames - 1);
    const float fraction = static_cast<float>(source_position - left_frame);
    for (std::uint16_t channel = 0; channel < destination.channels; ++channel) {
      const float left = sample_for_channel(left_frame, channel);
      const float sample = left + (sample_for_channel(right_frame, channel) - left) * fraction;
      if (integer_format) {
        append_signed_integer(output, sample, destination.bits_per_sample);
      } else {
        append_float32le(output, sample);
      }
    }
  }
  return output;
}

}  // namespace tts_host
