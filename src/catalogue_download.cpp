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

// Mirrors model_import.cpp's rewrite_imported_manifest: a package's "$schema"
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
  for (const auto &file : entry.generated_files) {
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
      {"revision", catalogue_entry_revision(entry)},
  };

  std::ofstream output(staging / "model.json");
  if (!output) {
    throw std::runtime_error("unable to write " + (staging / "model.json").string());
  }
  output << manifest.dump(2) << '\n';
}

}  // namespace

bool DownloadProgressThrottle::should_report(const DownloadProgress &progress,
                                             std::chrono::steady_clock::time_point now) {
  const bool new_file = !reported_ || progress.relative_path != last_path_;
  const bool complete = progress.bytes_downloaded >= progress.bytes_total;
  if (!new_file && !complete && now - last_report_ < interval_) {
    return false;
  }
  reported_ = true;
  last_path_ = progress.relative_path;
  last_report_ = now;
  return true;
}

std::filesystem::path catalogue_staging_path(const CatalogueEntry &entry,
                                             const std::filesystem::path &destination_root) {
  return destination_root / ("." + entry.id + ".downloading");
}

void stage_catalogue_file(const CatalogueEntry &entry, const CatalogueFile &file,
                          const std::filesystem::path &staging, const HttpFetchFunction &fetch,
                          const DownloadProgressCallback &progress) {
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

void write_catalogue_generated_files(const CatalogueEntry &entry, const std::filesystem::path &staging,
                                     const ConfigDocument &config) {
  for (const auto &file : entry.generated_files) {
    const auto file_path = staging / file.relative_path;
    std::filesystem::create_directories(file_path.parent_path());
    std::ofstream output(file_path, std::ios::binary | std::ios::trunc);
    if (!output || !output.write(file.contents.data(), static_cast<std::streamsize>(file.contents.size()))) {
      throw std::runtime_error("unable to write " + file_path.string());
    }
  }

  write_generated_manifest(entry, staging, config);
}

std::filesystem::path stage_catalogue_entry(const CatalogueEntry &entry,
                                            const std::filesystem::path &destination_root,
                                            const ConfigDocument &config, const HttpFetchFunction &fetch,
                                            const DownloadProgressCallback &progress) {
  const auto staging = catalogue_staging_path(entry, destination_root);
  std::filesystem::create_directories(staging);

  for (const auto &file : entry.files) {
    stage_catalogue_file(entry, file, staging, fetch, progress);
  }
  write_catalogue_generated_files(entry, staging, config);

  try {
    validate_model_package(staging);
  } catch (const std::exception &error) {
    std::error_code cleanup_ec;
    std::filesystem::remove_all(staging, cleanup_ec);
    throw std::runtime_error("the downloaded package '" + entry.id +
                             "' did not validate: " + error.what());
  }
  return staging;
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

  const auto staging = stage_catalogue_entry(entry, destination_root, config, fetch, progress);
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
