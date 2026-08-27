#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

namespace tts_host {

// Writes a canonical 44-byte-header PCM WAVE file. `pcm_payload` is raw
// little-endian sample bytes, exactly as carried by the runner audio
// protocol (see docs/adr/0002-runner-protocol.md), so no byte reordering is
// needed here.
void write_wav_file(const std::filesystem::path &path, std::uint32_t sample_rate_hz,
                    std::uint16_t channels, std::uint16_t bits_per_sample,
                    const std::vector<std::uint8_t> &pcm_payload);

}  // namespace tts_host
