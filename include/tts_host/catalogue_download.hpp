#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>

#include "tts_host/config_loader.hpp"
#include "tts_host/model_catalogue.hpp"
#include "tts_host/model_import.hpp"
#include "tts_host/sha256.hpp"

namespace tts_host {

struct DownloadProgress {
  std::string relative_path;
  std::uint64_t bytes_downloaded = 0;
  std::uint64_t bytes_total = 0;
};

using DownloadProgressCallback = std::function<void(const DownloadProgress &)>;

// Decides which progress updates are worth showing. The fetcher reports after
// every network read (up to 64 KiB), thousands of times per model; showing each
// would repaint the UI per read. Reports the first update of each file, the
// update completing it, and otherwise at most one per `interval`.
class DownloadProgressThrottle {
 public:
  explicit DownloadProgressThrottle(std::chrono::steady_clock::duration interval) : interval_(interval) {}

  bool should_report(const DownloadProgress &progress, std::chrono::steady_clock::time_point now);

 private:
  std::chrono::steady_clock::duration interval_;
  std::string last_path_;
  bool reported_ = false;
  std::chrono::steady_clock::time_point last_report_{};
};

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

// Where stage_catalogue_entry assembles `entry` inside `destination_root`. A
// dot-prefixed sibling of the installed folder, so the registry skips it and
// renaming it into place is one directory rename on the same volume.
std::filesystem::path catalogue_staging_path(const CatalogueEntry &entry,
                                             const std::filesystem::path &destination_root);

// Fetches one downloadable file of `entry` into `staging`, resuming a partial
// file, and verifies it against its pinned size and SHA-256. A mismatching file
// is deleted so the next attempt refetches it. Repair stages only damaged files
// this way.
void stage_catalogue_file(const CatalogueEntry &entry, const CatalogueFile &file,
                          const std::filesystem::path &staging, const HttpFetchFunction &fetch,
                          const DownloadProgressCallback &progress);

// Writes the entry's host-written files and generated model.json into `staging`.
void write_catalogue_generated_files(const CatalogueEntry &entry, const std::filesystem::path &staging,
                                     const ConfigDocument &config);

// Fetches, verifies, and writes `entry` into its staging folder and validates
// the result as a package, without installing it. Resumes a partial staging
// folder left by an earlier attempt. Returns the staging path. Model update
// stages this way so a failure never touches the working package.
std::filesystem::path stage_catalogue_entry(const CatalogueEntry &entry,
                                            const std::filesystem::path &destination_root,
                                            const ConfigDocument &config, const HttpFetchFunction &fetch,
                                            const DownloadProgressCallback &progress);

// Downloads every file of `entry` into `destination_root` (one of
// modelRegistry.directories, chosen by the caller), verifies each against its
// pinned SHA-256, writes the entry's generated files and a generated
// model.json -- the catalogue carries the metadata, but the remote host serves
// only weight files -- and installs the
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
