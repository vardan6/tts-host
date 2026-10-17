#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace tts_host {

// Lowercase hex SHA-256 of a file's bytes, used to verify a fetched file
// against the catalogue's pinned checksum before it is trusted.
std::string sha256_hex_of_file(const std::filesystem::path &path);

// Lowercase hex SHA-256 of in-memory bytes, used for host-written catalogue
// files and the ADR 0010 package revision.
std::string sha256_hex(std::string_view bytes);

}  // namespace tts_host
