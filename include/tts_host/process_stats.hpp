#pragma once

#include <cstdint>

namespace tts_host {

// Peak resident set size of the current process, in bytes. 0 if the
// platform call fails.
std::uint64_t peak_resident_set_size_bytes();

}  // namespace tts_host
