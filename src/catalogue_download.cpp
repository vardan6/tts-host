#include "tts_host/catalogue_download.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <stdexcept>
#include <vector>

#include <nlohmann/json.hpp>

#include "tts_host/model_registry.hpp"

namespace tts_host {

namespace {

using json = nlohmann::json;

// A minimal, self-contained SHA-256 (FIPS 180-4), processed incrementally so
// verifying a large weight file never holds the whole thing in memory.
class Sha256 {
 public:
  void update(const unsigned char *data, std::size_t length) {
    total_bytes_ += length;
    std::size_t offset = 0;
    if (!buffer_.empty()) {
      const std::size_t needed = 64 - buffer_.size();
      const std::size_t take = std::min(needed, length);
      buffer_.insert(buffer_.end(), data, data + take);
      offset = take;
      if (buffer_.size() < 64) {
        return;
      }
      process_block(buffer_.data());
      buffer_.clear();
    }
    while (offset + 64 <= length) {
      process_block(data + offset);
      offset += 64;
    }
    buffer_.assign(data + offset, data + length);
  }

  std::array<unsigned char, 32> finalize() {
    std::vector<unsigned char> tail;
    tail.push_back(0x80);
    const std::size_t used = buffer_.size() + 1;
    const std::size_t remainder = used % 64;
    const std::size_t zero_count = remainder <= 56 ? (56 - remainder) : (120 - remainder);
    tail.insert(tail.end(), zero_count, 0);
    const std::uint64_t total_bits = total_bytes_ * 8;
    for (int i = 7; i >= 0; --i) {
      tail.push_back(static_cast<unsigned char>((total_bits >> (i * 8)) & 0xff));
    }

    std::vector<unsigned char> final_bytes(buffer_);
    final_bytes.insert(final_bytes.end(), tail.begin(), tail.end());
    for (std::size_t offset = 0; offset < final_bytes.size(); offset += 64) {
      process_block(final_bytes.data() + offset);
    }

    std::array<unsigned char, 32> digest{};
    for (int i = 0; i < 8; ++i) {
      digest[i * 4 + 0] = static_cast<unsigned char>((state_[i] >> 24) & 0xff);
      digest[i * 4 + 1] = static_cast<unsigned char>((state_[i] >> 16) & 0xff);
      digest[i * 4 + 2] = static_cast<unsigned char>((state_[i] >> 8) & 0xff);
      digest[i * 4 + 3] = static_cast<unsigned char>(state_[i] & 0xff);
    }
    return digest;
  }

 private:
  static std::uint32_t rotr(std::uint32_t value, int bits) {
    return (value >> bits) | (value << (32 - bits));
  }

  void process_block(const unsigned char *block) {
    static constexpr std::uint32_t k[64] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
        0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
        0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
        0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
        0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
        0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
    };

    std::uint32_t w[64];
    for (int i = 0; i < 16; ++i) {
      w[i] = (static_cast<std::uint32_t>(block[i * 4]) << 24) |
             (static_cast<std::uint32_t>(block[i * 4 + 1]) << 16) |
             (static_cast<std::uint32_t>(block[i * 4 + 2]) << 8) |
             static_cast<std::uint32_t>(block[i * 4 + 3]);
    }
    for (int i = 16; i < 64; ++i) {
      const std::uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
      const std::uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
      w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    std::uint32_t a = state_[0], b = state_[1], c = state_[2], d = state_[3];
    std::uint32_t e = state_[4], f = state_[5], g = state_[6], h = state_[7];
    for (int i = 0; i < 64; ++i) {
      const std::uint32_t s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
      const std::uint32_t ch = (e & f) ^ (~e & g);
      const std::uint32_t temp1 = h + s1 + ch + k[i] + w[i];
      const std::uint32_t s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
      const std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
      const std::uint32_t temp2 = s0 + maj;
      h = g;
      g = f;
      f = e;
      e = d + temp1;
      d = c;
      c = b;
      b = a;
      a = temp1 + temp2;
    }
    state_[0] += a;
    state_[1] += b;
    state_[2] += c;
    state_[3] += d;
    state_[4] += e;
    state_[5] += f;
    state_[6] += g;
    state_[7] += h;
  }

  std::uint32_t state_[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                            0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
  std::vector<unsigned char> buffer_;
  std::uint64_t total_bytes_ = 0;
};

std::string to_lowercase_hex(const std::array<unsigned char, 32> &digest) {
  static constexpr char kHexDigits[] = "0123456789abcdef";
  std::string hex(64, '0');
  for (std::size_t i = 0; i < digest.size(); ++i) {
    hex[i * 2] = kHexDigits[(digest[i] >> 4) & 0x0f];
    hex[i * 2 + 1] = kHexDigits[digest[i] & 0x0f];
  }
  return hex;
}

// Mirrors model_import.cpp's repoint_manifest_schema: a package's "$schema"
// only resolves relative to where it sits, and a downloaded package has no
// manifest to copy in the first place, so this writes one pointing at this
// installation's own model schema directly.
std::string schema_reference(const std::filesystem::path &staging,
                             const ConfigDocument &config) {
  const auto schema_path = config.paths.schema_path.parent_path() / "model.schema.json";
  std::error_code ec;
  if (!std::filesystem::is_regular_file(schema_path, ec) || ec) {
    return schema_path.generic_string();
  }
  const auto relative = schema_path.lexically_relative(staging);
  return relative.empty() ? schema_path.generic_string() : relative.generic_string();
}

void write_generated_manifest(const CatalogueEntry &entry, const std::filesystem::path &staging,
                              const ConfigDocument &config) {
  json files = json::object();
  for (const auto &file : entry.files) {
    files[file.manifest_key] = file.relative_path;
  }

  const json manifest = {
      {"$schema", schema_reference(staging, config)},
      {"schemaVersion", 1},
      {"id", entry.id},
      {"displayName", entry.display_name},
      {"engine", entry.engine},
      {"languages", entry.languages},
      {"files", files},
      {"license", {{"name", entry.license.name}, {"url", entry.license.url}}},
  };

  std::ofstream output(staging / "model.json");
  if (!output) {
    throw std::runtime_error("unable to write " + (staging / "model.json").string());
  }
  output << manifest.dump(2) << '\n';
}

}  // namespace

std::string sha256_hex_of_file(const std::filesystem::path &path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    throw std::runtime_error("unable to read " + path.string() + " to verify its checksum");
  }

  Sha256 hasher;
  std::vector<char> buffer(64 * 1024);
  while (input) {
    input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    const auto read = input.gcount();
    if (read > 0) {
      hasher.update(reinterpret_cast<const unsigned char *>(buffer.data()),
                   static_cast<std::size_t>(read));
    }
  }
  return to_lowercase_hex(hasher.finalize());
}

ImportedModelPackage download_catalogue_entry(const CatalogueEntry &entry,
                                              const std::filesystem::path &destination_root,
                                              const ConfigDocument &config, const HttpFetchFunction &fetch,
                                              const DownloadProgressCallback &progress) {
  const auto destination = destination_root / entry.id;
  std::error_code exists_ec;
  if (std::filesystem::exists(destination, exists_ec)) {
    throw std::runtime_error("'" + entry.id + "' is already installed at " + destination.string());
  }

  const auto staging = destination_root / ("." + entry.id + ".downloading");
  std::filesystem::create_directories(staging);

  for (const auto &file : entry.files) {
    const auto file_path = staging / file.relative_path;
    std::filesystem::create_directories(file_path.parent_path());

    std::error_code size_ec;
    std::uint64_t bytes_on_disk =
        std::filesystem::exists(file_path, size_ec) ? std::filesystem::file_size(file_path, size_ec) : 0;
    if (size_ec || bytes_on_disk > file.size_bytes) {
      bytes_on_disk = 0;
    }

    if (bytes_on_disk < file.size_bytes) {
      fetch(file.url, bytes_on_disk, file_path, file.size_bytes, progress);
    }

    std::error_code final_size_ec;
    const auto final_size = std::filesystem::file_size(file_path, final_size_ec);
    if (final_size_ec || final_size != file.size_bytes) {
      throw std::runtime_error("download of " + file.relative_path + " for '" + entry.id +
                               "' is incomplete; rerun to resume");
    }

    const auto actual_sha256 = sha256_hex_of_file(file_path);
    if (actual_sha256 != file.sha256) {
      std::error_code remove_ec;
      std::filesystem::remove(file_path, remove_ec);
      throw std::runtime_error("checksum mismatch for " + file.relative_path + " of '" + entry.id +
                               "': expected " + file.sha256 + ", got " + actual_sha256);
    }
  }

  write_generated_manifest(entry, staging, config);

  try {
    validate_model_package(staging);
  } catch (const std::exception &error) {
    std::error_code cleanup_ec;
    std::filesystem::remove_all(staging, cleanup_ec);
    throw std::runtime_error("the downloaded package '" + entry.id +
                             "' did not validate: " + error.what());
  }

  std::error_code rename_ec;
  std::filesystem::rename(staging, destination, rename_ec);
  if (rename_ec) {
    throw std::runtime_error("failed to move the downloaded package into " + destination.string() +
                             ": " + rename_ec.message());
  }

  return ImportedModelPackage{entry.id, entry.display_name, destination};
}

}  // namespace tts_host

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>

namespace tts_host {

namespace {

std::wstring widen_utf8(const std::string &value) {
  if (value.empty()) {
    return std::wstring();
  }
  const int size = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, nullptr, 0);
  std::wstring result(static_cast<std::size_t>(size), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, result.data(), size);
  result.resize(static_cast<std::size_t>(size) - 1);
  return result;
}

struct WinHttpHandle {
  HINTERNET handle = nullptr;
  ~WinHttpHandle() {
    if (handle) {
      WinHttpCloseHandle(handle);
    }
  }
};

}  // namespace

void fetch_url_to_file(const std::string &url, std::uint64_t resume_from_bytes,
                       const std::filesystem::path &destination, std::uint64_t total_bytes,
                       const DownloadProgressCallback &progress) {
  const std::wstring wide_url = widen_utf8(url);

  wchar_t host_name[256] = {};
  wchar_t url_path[2048] = {};
  wchar_t extra_info[2048] = {};
  URL_COMPONENTS components{};
  components.dwStructSize = sizeof(components);
  components.lpszHostName = host_name;
  components.dwHostNameLength = static_cast<DWORD>(std::size(host_name));
  components.lpszUrlPath = url_path;
  components.dwUrlPathLength = static_cast<DWORD>(std::size(url_path));
  components.lpszExtraInfo = extra_info;
  components.dwExtraInfoLength = static_cast<DWORD>(std::size(extra_info));

  if (!WinHttpCrackUrl(wide_url.c_str(), static_cast<DWORD>(wide_url.size()), 0, &components)) {
    throw std::runtime_error("failed to parse download URL: " + url);
  }
  if (components.nScheme != INTERNET_SCHEME_HTTPS) {
    throw std::runtime_error("refusing a non-HTTPS download URL: " + url);
  }

  WinHttpHandle session{WinHttpOpen(L"tts-host/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0)};
  if (!session.handle) {
    throw std::runtime_error("WinHttpOpen failed for " + url);
  }

  WinHttpHandle connection{WinHttpConnect(session.handle, components.lpszHostName, components.nPort, 0)};
  if (!connection.handle) {
    throw std::runtime_error("WinHttpConnect failed for " + url);
  }

  const std::wstring path_and_extra = std::wstring(components.lpszUrlPath) + components.lpszExtraInfo;
  WinHttpHandle request{WinHttpOpenRequest(connection.handle, L"GET", path_and_extra.c_str(), nullptr,
                                           WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                           WINHTTP_FLAG_SECURE)};
  if (!request.handle) {
    throw std::runtime_error("WinHttpOpenRequest failed for " + url);
  }

  if (resume_from_bytes > 0) {
    const std::wstring range_header = L"Range: bytes=" + std::to_wstring(resume_from_bytes) + L"-";
    WinHttpAddRequestHeaders(request.handle, range_header.c_str(),
                             static_cast<DWORD>(range_header.size()), WINHTTP_ADDREQ_FLAG_ADD);
  }

  if (!WinHttpSendRequest(request.handle, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0,
                          0) ||
      !WinHttpReceiveResponse(request.handle, nullptr)) {
    throw std::runtime_error("HTTPS request failed for " + url);
  }

  DWORD status_code = 0;
  DWORD status_size = sizeof(status_code);
  WinHttpQueryHeaders(request.handle, WINHTTP_QUERY_FLAG_NUMBER | WINHTTP_QUERY_STATUS_CODE,
                     WINHTTP_HEADER_NAME_BY_INDEX, &status_code, &status_size, WINHTTP_NO_HEADER_INDEX);
  if (status_code != 200 && status_code != 206) {
    throw std::runtime_error("HTTPS request for " + url + " returned status " +
                             std::to_string(status_code));
  }

  // A server that ignores the Range header falls back to a full 200 response;
  // appending that onto a partial file would corrupt it, so restart from 0.
  const bool resumed = status_code == 206;
  std::filesystem::create_directories(destination.parent_path());
  std::ofstream output(destination,
                       resumed ? (std::ios::binary | std::ios::app) : (std::ios::binary | std::ios::trunc));
  if (!output) {
    throw std::runtime_error("failed to open " + destination.string() + " for writing");
  }

  std::vector<char> buffer(64 * 1024);
  std::uint64_t bytes_downloaded = resumed ? resume_from_bytes : 0;
  for (;;) {
    DWORD available = 0;
    if (!WinHttpQueryDataAvailable(request.handle, &available)) {
      throw std::runtime_error("WinHttpQueryDataAvailable failed for " + url);
    }
    if (available == 0) {
      break;
    }
    const DWORD to_read = static_cast<DWORD>(std::min<std::size_t>(available, buffer.size()));
    DWORD read = 0;
    if (!WinHttpReadData(request.handle, buffer.data(), to_read, &read)) {
      throw std::runtime_error("WinHttpReadData failed for " + url);
    }
    if (read == 0) {
      break;
    }
    output.write(buffer.data(), static_cast<std::streamsize>(read));
    bytes_downloaded += read;
    if (progress) {
      progress(DownloadProgress{destination.filename().string(), bytes_downloaded, total_bytes});
    }
  }
  output.flush();
  if (!output) {
    throw std::runtime_error("failed writing " + destination.string());
  }
  if (bytes_downloaded != total_bytes) {
    throw std::runtime_error("download of " + url + " ended after " + std::to_string(bytes_downloaded) +
                             " of " + std::to_string(total_bytes) + " bytes; rerun to resume");
  }
}

}  // namespace tts_host

#else  // !_WIN32

namespace tts_host {

void fetch_url_to_file(const std::string &, std::uint64_t, const std::filesystem::path &, std::uint64_t,
                       const DownloadProgressCallback &) {
  throw std::runtime_error("catalogue download is not implemented on this platform yet");
}

}  // namespace tts_host

#endif
