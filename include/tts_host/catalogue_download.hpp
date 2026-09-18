#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>

#include "tts_host/config_loader.hpp"
#include "tts_host/model_catalogue.hpp"
#include "tts_host/model_import.hpp"

namespace tts_host {

struct DownloadProgress {
  std::string relative_path;
  std::uint64_t bytes_downloaded = 0;
  std::uint64_t bytes_total = 0;
};

using DownloadProgressCallback = std::function<void(const DownloadProgress &)>;

// Fetches `url` into `destination`, resuming from `resume_from_bytes` (the
// number of bytes already on disk) when it is greater than zero, and reports
// progress as bytes arrive. Throws std::runtime_error on any transport
// failure, leaving whatever prefix was already written so a subsequent call
// can resume. `total_bytes` is the catalogue's pinned size, used only to size
// progress reports and to detect a response that ends early.
using HttpFetchFunction = std::function<void(
    const std::string &url, std::uint64_t resume_from_bytes, const std::filesystem::path &destination,
    std::uint64_t total_bytes, const DownloadProgressCallback &progress)>;

// The production fetcher backing HttpFetchFunction: WinHTTP on Windows.
// Throws a clear not-implemented error on other platforms, the same
// convention as tray_icon.hpp/settings_window.hpp/playback_sink.hpp -- see
// docs/design/architecture.md#download-catalogue.
void fetch_url_to_file(const std::string &url, std::uint64_t resume_from_bytes,
                       const std::filesystem::path &destination, std::uint64_t total_bytes,
                       const DownloadProgressCallback &progress);

// Lowercase hex SHA-256 of a file's bytes, used to verify a fetched file
// against the catalogue's pinned checksum before it is trusted.
std::string sha256_hex_of_file(const std::filesystem::path &path);

// Downloads every file of `entry` into `destination_root` (one of
// modelRegistry.directories, chosen by the caller), verifies each against its
// pinned SHA-256, writes a generated model.json -- the catalogue carries the
// metadata, but the remote host serves only weight files -- and installs the
// verified package. A file already fully present on disk from a prior,
// interrupted attempt is resumed rather than refetched; a file that fails
// checksum verification is deleted so the next attempt refetches it from
// scratch. Throws std::runtime_error if the entry is already installed at
// `destination_root` or if any file fails to fetch or verify.
ImportedModelPackage download_catalogue_entry(const CatalogueEntry &entry,
                                              const std::filesystem::path &destination_root,
                                              const ConfigDocument &config, const HttpFetchFunction &fetch,
                                              const DownloadProgressCallback &progress);

}  // namespace tts_host
