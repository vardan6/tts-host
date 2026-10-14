#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace tts_host {

// JSON and CLI strings are UTF-8; filesystem paths use the platform encoding.
inline std::filesystem::path path_from_utf8(std::string_view value) {
  return std::filesystem::path(std::u8string(
      reinterpret_cast<const char8_t *>(value.data()), value.size()));
}

inline std::string path_to_utf8(const std::filesystem::path &value) {
  const auto encoded = value.u8string();
  return std::string(reinterpret_cast<const char *>(encoded.data()), encoded.size());
}

}  // namespace tts_host
