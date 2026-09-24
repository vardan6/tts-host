#pragma once

#include <cstdint>
#include <vector>

namespace tts_host {

// The subset of endpoint PCM encodings that the Windows shared-mode playback
// backend can negotiate. Runner audio is always interleaved signed 16-bit LE
// PCM (ADR 0002); this describes the endpoint's requested representation.
enum class AudioSampleEncoding { signed_integer, ieee_float };

struct AudioFormat {
  std::uint32_t sample_rate_hz;
  std::uint16_t channels;
  std::uint16_t bits_per_sample;
  AudioSampleEncoding encoding;
};

// Maps a frame position between source and endpoint timelines. Integer
// arithmetic keeps a seek aligned to a stable frame boundary.
std::uint64_t convert_frame_position(std::uint64_t source_frame,
                                     std::uint32_t source_sample_rate_hz,
                                     std::uint32_t destination_sample_rate_hz);

// Converts interleaved pcm_s16le to an endpoint format. It resamples with
// linear interpolation, duplicates mono when expanding channels, and mixes
// multiple channels to their arithmetic mean when reducing them. The function
// is platform-independent so conversion behavior can be tested without WASAPI.
std::vector<std::uint8_t> convert_pcm_s16le(const std::vector<std::uint8_t> &source,
                                            std::uint32_t source_sample_rate_hz,
                                            std::uint16_t source_channels,
                                            const AudioFormat &destination);

}  // namespace tts_host
