#include "tts_host/wav_writer.hpp"

#include <fstream>
#include <stdexcept>

namespace tts_host {
namespace {

void append_u32_le(std::vector<std::uint8_t> &bytes, std::uint32_t value) {
  bytes.push_back(static_cast<std::uint8_t>(value));
  bytes.push_back(static_cast<std::uint8_t>(value >> 8));
  bytes.push_back(static_cast<std::uint8_t>(value >> 16));
  bytes.push_back(static_cast<std::uint8_t>(value >> 24));
}

void append_u16_le(std::vector<std::uint8_t> &bytes, std::uint16_t value) {
  bytes.push_back(static_cast<std::uint8_t>(value));
  bytes.push_back(static_cast<std::uint8_t>(value >> 8));
}

void append_tag(std::vector<std::uint8_t> &bytes, const char (&tag)[5]) {
  bytes.insert(bytes.end(), tag, tag + 4);
}

}  // namespace

void write_wav_file(const std::filesystem::path &path, std::uint32_t sample_rate_hz,
                    std::uint16_t channels, std::uint16_t bits_per_sample,
                    const std::vector<std::uint8_t> &pcm_payload) {
  const std::uint16_t block_align = static_cast<std::uint16_t>(channels * (bits_per_sample / 8));
  const std::uint32_t byte_rate = sample_rate_hz * block_align;
  const std::uint32_t data_size = static_cast<std::uint32_t>(pcm_payload.size());

  std::vector<std::uint8_t> header;
  header.reserve(44);
  append_tag(header, "RIFF");
  append_u32_le(header, 36 + data_size);
  append_tag(header, "WAVE");
  append_tag(header, "fmt ");
  append_u32_le(header, 16);
  append_u16_le(header, 1);  // PCM
  append_u16_le(header, channels);
  append_u32_le(header, sample_rate_hz);
  append_u32_le(header, byte_rate);
  append_u16_le(header, block_align);
  append_u16_le(header, bits_per_sample);
  append_tag(header, "data");
  append_u32_le(header, data_size);

  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  if (!output) {
    throw std::runtime_error("Unable to open WAV output file: " + path.string());
  }
  output.write(reinterpret_cast<const char *>(header.data()),
               static_cast<std::streamsize>(header.size()));
  output.write(reinterpret_cast<const char *>(pcm_payload.data()),
               static_cast<std::streamsize>(pcm_payload.size()));
  if (!output) {
    throw std::runtime_error("Unable to write WAV output file: " + path.string());
  }
}

}  // namespace tts_host
