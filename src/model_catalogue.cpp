#include "tts_host/model_catalogue.hpp"

#include <algorithm>
#include <cstdio>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace tts_host {

namespace {

// Kokoro-82M is also bundled with the application, so this entry is not how a
// user first gets speech -- it is how the bundled package is restored or
// installed into a second model directory. Its URLs and checksums are the
// canonical onnx-community files that tools/fetch_kokoro_weights.py fetches;
// the checksums are of the exact bytes this repository was tested against.
const std::vector<CatalogueEntry> &curated_entries() {
  static const std::vector<CatalogueEntry> entries = {
      CatalogueEntry{
          "kokoro-en-v1",
          "Kokoro-82M (English)",
          "kokoro-onnx",
          {"en"},
          CatalogueLicense{"Apache-2.0",
                           "https://huggingface.co/onnx-community/Kokoro-82M-v1.0-ONNX", false},
          {
              CatalogueFile{
                  "model.onnx",
                  "model",
                  "https://huggingface.co/onnx-community/Kokoro-82M-v1.0-ONNX/resolve/main/onnx/"
                  "model.onnx",
                  "8fbea51ea711f2af382e88c833d9e288c6dc82ce5e98421ea61c058ce21a34cb",
                  325532232,
              },
              CatalogueFile{
                  "voices/af_heart.bin",
                  "voice",
                  "https://huggingface.co/onnx-community/Kokoro-82M-v1.0-ONNX/resolve/main/voices/"
                  "af_heart.bin",
                  "d583ccff3cdca2f7fae535cb998ac07e9fcb90f09737b9a41fa2734ec44a8f0b",
                  522240,
              },
          },
      },
  };
  return entries;
}

[[noreturn]] void reject(const std::string &entry_id, const std::string &reason) {
  throw std::runtime_error("catalogue entry '" + entry_id + "': " + reason);
}

bool is_lowercase_sha256(const std::string &value) {
  if (value.size() != 64) {
    return false;
  }
  return std::all_of(value.begin(), value.end(), [](unsigned char character) {
    return (character >= '0' && character <= '9') || (character >= 'a' && character <= 'f');
  });
}

// The same containment rule scan_model_registry applies to manifest file paths,
// checked lexically here because the destination directory does not exist yet.
bool escapes_package_root(const std::string &relative_path) {
  if (relative_path.empty() || relative_path.front() == '/' || relative_path.front() == '\\') {
    return true;
  }
  if (relative_path.size() >= 2 && relative_path[1] == ':') {
    return true;
  }

  std::string component;
  for (const char character : relative_path + '/') {
    if (character == '/' || character == '\\') {
      if (component == "..") {
        return true;
      }
      component.clear();
      continue;
    }
    component.push_back(character);
  }
  return false;
}

}  // namespace

const std::vector<CatalogueEntry> &model_catalogue() { return curated_entries(); }

std::uint64_t catalogue_entry_size_bytes(const CatalogueEntry &entry) {
  std::uint64_t total = 0;
  for (const auto &file : entry.files) {
    total += file.size_bytes;
  }
  return total;
}

void validate_catalogue(const std::vector<CatalogueEntry> &entries) {
  std::set<std::string> seen_ids;
  for (const auto &entry : entries) {
    if (entry.id.empty()) {
      reject("<unnamed>", "id is empty");
    }
    if (!seen_ids.insert(entry.id).second) {
      reject(entry.id, "id appears more than once in the catalogue");
    }
    if (entry.display_name.empty()) {
      reject(entry.id, "displayName is empty");
    }
    if (entry.engine.empty()) {
      reject(entry.id, "engine is empty");
    }
    if (entry.languages.empty()) {
      reject(entry.id, "declares no languages");
    }
    for (const auto &language : entry.languages) {
      if (language.empty()) {
        reject(entry.id, "declares an empty language tag");
      }
    }
    // Licence disclosure before download is a product requirement
    // (docs/requirements/product.md#distribution-and-usability), so an entry
    // with nothing to disclose cannot ship.
    if (entry.license.name.empty()) {
      reject(entry.id, "licence name is empty");
    }
    if (entry.files.empty()) {
      reject(entry.id, "declares no files to download");
    }

    std::set<std::string> seen_paths;
    std::set<std::string> seen_manifest_keys;
    for (const auto &file : entry.files) {
      if (escapes_package_root(file.relative_path)) {
        reject(entry.id, "file path " +
                             (file.relative_path.empty() ? "<empty>" : file.relative_path) +
                             " escapes the package root");
      }
      if (!seen_paths.insert(file.relative_path).second) {
        reject(entry.id, "file path " + file.relative_path + " appears more than once");
      }
      // schemas/model.schema.json requires a "files.model" key, and download
      // has to synthesize a model.json since the remote host does not serve
      // one, so every file needs a manifest key to write it under.
      if (file.manifest_key.empty()) {
        reject(entry.id, "file " + file.relative_path + " declares no manifest key");
      }
      if (!seen_manifest_keys.insert(file.manifest_key).second) {
        reject(entry.id, "manifest key " + file.manifest_key + " appears more than once");
      }
      if (file.url.rfind("https://", 0) != 0) {
        reject(entry.id, "file " + file.relative_path + " is not fetched over HTTPS: " +
                             (file.url.empty() ? "<empty>" : file.url));
      }
      if (!is_lowercase_sha256(file.sha256)) {
        reject(entry.id, "file " + file.relative_path +
                             " has no pinned lowercase hex SHA-256 checksum");
      }
      if (file.size_bytes == 0) {
        reject(entry.id, "file " + file.relative_path + " declares a zero download size");
      }
    }
    if (!seen_manifest_keys.count("model")) {
      reject(entry.id, "no file declares the required manifest key \"model\"");
    }
  }
}

bool catalogue_entry_installed(const CatalogueEntry &entry, const ModelRegistryScan &scan) {
  return std::any_of(scan.discovered_packages.begin(), scan.discovered_packages.end(),
                     [&entry](const ModelPackageCandidate &package) {
                       return package.id == entry.id;
                     });
}

std::string describe_catalogue_license(const CatalogueEntry &entry) {
  std::string description = entry.license.name;
  if (entry.license.non_commercial) {
    description += " [NON-COMMERCIAL]";
  }
  if (!entry.license.url.empty()) {
    description += " (" + entry.license.url + ")";
  }
  return description;
}

std::string describe_download_size(std::uint64_t bytes) {
  constexpr std::uint64_t kMegabyte = 1024ULL * 1024ULL;
  if (bytes < kMegabyte) {
    return std::to_string((bytes + 1023ULL) / 1024ULL) + " KB";
  }

  char formatted[32] = {};
  const double megabytes = static_cast<double>(bytes) / static_cast<double>(kMegabyte);
  std::snprintf(formatted, sizeof(formatted), "%.1f MB", megabytes);
  return formatted;
}

}  // namespace tts_host
