#include "tts_host/model_catalogue.hpp"

#include <algorithm>
#include <cstdio>
#include <map>
#include <set>
#include <utility>
#include <stdexcept>
#include <string>
#include <vector>

#include "tts_host/sha256.hpp"

namespace tts_host {

namespace {

constexpr const char *kLjSpeechReferenceNotice =
    "Qwen reference voice: LJ Speech Dataset clip LJ001-0001, read by Linda Johnson\n"
    "for LibriVox. The LJ Speech Dataset is in the public domain\n"
    "(https://keithito.com/LJ-Speech-Dataset/). Fetched from the pinned mirror\n"
    "https://huggingface.co/datasets/flexthink/ljspeech at revision\n"
    "7367bcc33648be329bbef057cc97d0b83cadee11, SHA-256\n"
    "4d2bd83ff0f7fe33491b03193abdf623f1b4cc2d8103972b33638d3607d86dc5.\n";

constexpr const char *kQwenRevisionUrl =
    "https://huggingface.co/ggml-org/Qwen3-TTS-12Hz-1.7B-Base-GGUF/resolve/"
    "ca27d74bc954b73dadab5b71ca265d87fc861a7c/";

// Every Qwen package is a 1.7B Base build on the qwen3-gguf runner. Base clones
// a reference voice; the public-domain clip is fetched from a revision-pinned
// mirror, and the host writes its provenance notice.
CatalogueEntry qwen_base_entry(std::string id, std::string display_name, CatalogueFile model,
                               CatalogueFile mmproj) {
  return CatalogueEntry{
      std::move(id),
      std::move(display_name),
      "qwen3-gguf",
      {"en"},
      CatalogueLicense{"Apache-2.0", "https://huggingface.co/Qwen/Qwen3-TTS-12Hz-1.7B-Base", false},
      {
          std::move(model),
          std::move(mmproj),
          CatalogueFile{
              "reference.wav",
              "voice",
              "https://huggingface.co/datasets/flexthink/ljspeech/resolve/"
              "7367bcc33648be329bbef057cc97d0b83cadee11/wavs/LJ001-0001.wav",
              "4d2bd83ff0f7fe33491b03193abdf623f1b4cc2d8103972b33638d3607d86dc5",
              425830,
          },
      },
      {
          CatalogueGeneratedFile{"reference-license.txt", "voiceLicense", kLjSpeechReferenceNotice},
      },
  };
}

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
          {},
      },
      // The bundled Qwen package (docs/adr/0009-qwen-release-selection-without-bake-off.md),
      // also offered here so it can be installed into another model directory.
      qwen_base_entry("qwen3-tts-1.7b", "Qwen3-TTS 1.7B Base (English)",
                      CatalogueFile{
                          "model.gguf",
                          "model",
                          std::string{kQwenRevisionUrl} + "Qwen3-TTS-12Hz-1.7B-Base-Q4_K_M.gguf",
                          "8d18c94acb2addd042f97da63c98be144eafa76d0d9495177eab65130cf85129",
                          1035965280,
                      },
                      CatalogueFile{
                          "mmproj.gguf",
                          "mmproj",
                          std::string{kQwenRevisionUrl} + "mmproj-Qwen3-TTS-12Hz-1.7B-Base-Q8_0.gguf",
                          "6fd65188839bcd6ecc91b277ad471e22a0edfada4699a0fe82f1165c18cfcce2",
                          446422912,
                      }),
      // Higher-precision builds of the same model: catalogue-only, never bundled.
      qwen_base_entry("qwen3-tts-1.7b-q8", "Qwen3-TTS 1.7B Base Q8_0 (English)",
                      CatalogueFile{
                          "model.gguf",
                          "model",
                          std::string{kQwenRevisionUrl} + "Qwen3-TTS-12Hz-1.7B-Base-Q8_0.gguf",
                          "ac7931aeb2e7aad1a6ed6602d353a5679c9d096b18ce8204ac730a8408d572e1",
                          1847874400,
                      },
                      CatalogueFile{
                          "mmproj.gguf",
                          "mmproj",
                          std::string{kQwenRevisionUrl} + "mmproj-Qwen3-TTS-12Hz-1.7B-Base-Q8_0.gguf",
                          "6fd65188839bcd6ecc91b277ad471e22a0edfada4699a0fe82f1165c18cfcce2",
                          446422912,
                      }),
      qwen_base_entry("qwen3-tts-1.7b-bf16", "Qwen3-TTS 1.7B Base bf16 (English)",
                      CatalogueFile{
                          "model.gguf",
                          "model",
                          std::string{kQwenRevisionUrl} + "Qwen3-TTS-12Hz-1.7B-Base-bf16.gguf",
                          "0322c634ad5d3282524bc45bff030ab3f6f2a32ba14d5e7dace7eed75ecede46",
                          3472593760,
                      },
                      CatalogueFile{
                          "mmproj.gguf",
                          "mmproj",
                          std::string{kQwenRevisionUrl} + "mmproj-Qwen3-TTS-12Hz-1.7B-Base-bf16.gguf",
                          "b9503e95e44705739cf82c15ce909b040b96a31ad79d02cf4dc5e1484399265d",
                          669081472,
                      }),
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
    for (const auto &file : entry.generated_files) {
      if (escapes_package_root(file.relative_path)) {
        reject(entry.id, "generated file path " +
                             (file.relative_path.empty() ? "<empty>" : file.relative_path) +
                             " escapes the package root");
      }
      if (!seen_paths.insert(file.relative_path).second) {
        reject(entry.id, "file path " + file.relative_path + " appears more than once");
      }
      if (file.manifest_key.empty()) {
        reject(entry.id, "generated file " + file.relative_path + " declares no manifest key");
      }
      if (!seen_manifest_keys.insert(file.manifest_key).second) {
        reject(entry.id, "manifest key " + file.manifest_key + " appears more than once");
      }
      if (file.contents.empty()) {
        reject(entry.id, "generated file " + file.relative_path + " is empty");
      }
    }
    if (!seen_manifest_keys.count("model")) {
      reject(entry.id, "no file declares the required manifest key \"model\"");
    }
  }
}

bool catalogue_entry_installed(const CatalogueEntry &entry, const ModelRegistryScan &scan) {
  return catalogue_entry_package(entry, scan) != nullptr;
}

const ModelPackageCandidate *catalogue_entry_package(const CatalogueEntry &entry,
                                                     const ModelRegistryScan &scan) {
  const auto found = std::find_if(scan.discovered_packages.begin(), scan.discovered_packages.end(),
                                  [&entry](const ModelPackageCandidate &package) {
                                    return package.id == entry.id;
                                  });
  return found == scan.discovered_packages.end() ? nullptr : &*found;
}

std::string catalogue_entry_revision(const CatalogueEntry &entry) {
  std::vector<std::pair<std::string, std::string>> rows;
  for (const auto &file : entry.files) {
    rows.emplace_back(file.relative_path, file.sha256);
  }
  for (const auto &file : entry.generated_files) {
    rows.emplace_back(file.relative_path, sha256_hex(file.contents));
  }
  std::sort(rows.begin(), rows.end());

  std::string listing;
  for (const auto &[path, sha256] : rows) {
    listing += sha256 + "  " + path + "\n";
  }
  return sha256_hex(listing);
}

std::optional<std::string> catalogue_revision_for(const std::string &id) {
  static const std::map<std::string, std::string> revisions = [] {
    std::map<std::string, std::string> result;
    for (const auto &entry : model_catalogue()) {
      result.emplace(entry.id, catalogue_entry_revision(entry));
    }
    return result;
  }();
  const auto found = revisions.find(id);
  if (found == revisions.end()) {
    return std::nullopt;
  }
  return found->second;
}

PackageStatus package_status(const ModelPackageCandidate &package) {
  const auto expected = catalogue_revision_for(package.id);
  if (!package.revision || !expected) {
    return PackageStatus::local;
  }
  return *package.revision == *expected ? PackageStatus::current : PackageStatus::update_available;
}

std::string describe_package_status(PackageStatus status) {
  switch (status) {
    case PackageStatus::current:
      return "current";
    case PackageStatus::update_available:
      return "update available";
    case PackageStatus::local:
      break;
  }
  return "local";
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
