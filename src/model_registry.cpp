#include "tts_host/model_registry.hpp"
#include "tts_host/model_catalogue.hpp"
#include "tts_host/path_encoding.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <tuple>

#include <nlohmann/json-schema.hpp>

namespace tts_host {
namespace {

using json = nlohmann::json;
using nlohmann::json_schema::json_validator;

class ValidationErrorHandler final : public nlohmann::json_schema::basic_error_handler {
 public:
  void error(const json::json_pointer &pointer, const json &instance,
             const std::string &message) override {
    basic_error_handler::error(pointer, instance, message);
    issues.push_back({pointer.to_string(), message});
  }

  std::vector<ConfigError> issues;
};

void add_unsupported(ModelRegistryScan &scan, std::filesystem::path path, std::string reason) {
  scan.unsupported_entries.push_back(
      UnsupportedRegistryEntry{std::move(path), std::move(reason)});
}

json load_json_file(const std::filesystem::path &path, const std::string &label) {
  std::ifstream input(path);
  if (!input) {
    throw std::runtime_error("Unable to open " + label + ": " + path_to_utf8(path));
  }

  try {
    return json::parse(input);
  } catch (const json::parse_error &error) {
    throw std::runtime_error("Invalid " + label + " JSON in " + path_to_utf8(path) + ": " +
                             error.what());
  }
}

std::filesystem::path resolve_schema_path(const json &manifest,
                                          const std::filesystem::path &manifest_path) {
  const auto schema_it = manifest.find("$schema");
  if (schema_it != manifest.end() && schema_it->is_string()) {
    const auto candidate = path_from_utf8(schema_it->get<std::string>());
    if (candidate.is_absolute()) {
      return candidate.lexically_normal();
    }
    return (manifest_path.parent_path() / candidate).lexically_normal();
  }

  return (manifest_path.parent_path() / "schemas" / "model.schema.json").lexically_normal();
}

void enforce_supported_schema_version(const json &manifest) {
  const auto schema_version = manifest.find("schemaVersion");
  if (schema_version != manifest.end() && schema_version->is_number_integer() &&
      schema_version->get<int>() != 1) {
    throw std::runtime_error("unsupported schema version " +
                             std::to_string(schema_version->get<int>()) + "; expected 1");
  }
}

void validate_against_schema(const json &schema, const json &manifest) {
  json_validator validator(nullptr, nlohmann::json_schema::default_string_format_check);
  validator.set_root_schema(schema);

  ValidationErrorHandler error_handler;
  validator.validate(manifest, error_handler);
  if (!error_handler.issues.empty()) {
    const auto &issue = error_handler.issues.front();
    throw std::runtime_error("schema validation failed at " +
                             std::string(issue.json_path.empty() ? "/" : issue.json_path) + ": " +
                             issue.message);
  }
}

bool is_within_package_root(const std::filesystem::path &path,
                            const std::filesystem::path &package_root) {
  const auto relative = path.lexically_relative(package_root);
  if (relative.empty()) {
    return path == package_root;
  }

  const auto first_component = *relative.begin();
  return first_component != "..";
}

void validate_manifest_file_paths(const json &manifest,
                                  const std::filesystem::path &package_path) {
  std::error_code ec;
  const auto package_root = std::filesystem::weakly_canonical(package_path, ec);
  if (ec) {
    throw std::runtime_error("failed to resolve package root: " + ec.message());
  }

  for (const auto &[key, value] : manifest.at("files").items()) {
    const auto declared_path = path_from_utf8(value.get<std::string>());
    if (declared_path.is_absolute()) {
      throw std::runtime_error("manifest file path /files/" + key +
                               " must be relative to the package root");
    }

    const auto resolved_path =
        std::filesystem::weakly_canonical(package_root / declared_path, ec);
    if (ec) {
      throw std::runtime_error("failed to resolve manifest file path /files/" + key + ": " +
                               ec.message());
    }
    if (!is_within_package_root(resolved_path, package_root)) {
      throw std::runtime_error("manifest file path /files/" + key +
                               " escapes the package root");
    }
  }
}

ModelPackageCandidate load_manifest_candidate(const std::filesystem::path &package_path,
                                              const std::filesystem::path &manifest_path) {
  const auto manifest = load_json_file(manifest_path, "model manifest");
  enforce_supported_schema_version(manifest);

  const auto schema_path = resolve_schema_path(manifest, manifest_path);
  const auto schema = load_json_file(schema_path, "model schema");
  validate_against_schema(schema, manifest);
  validate_manifest_file_paths(manifest, package_path);
  if (manifest.at("engine") == "qwen3-gguf") {
    // The runner speaks every request in one package language; a second
    // declared language would be selectable but spoken with the wrong tag.
    const auto &languages = manifest.at("languages");
    if (languages.size() != 1 || (languages.front() != "en" && languages.front() != "ru"))
      throw std::runtime_error("qwen3-gguf package must declare exactly one language: en or ru");
    for (const auto *key : {"model", "mmproj", "voice", "voiceLicense"}) {
      const auto &files = manifest.at("files");
      if (!files.contains(key))
        throw std::runtime_error(std::string("qwen3-gguf package requires /files/") + key);
      const auto asset = package_path / path_from_utf8(files.at(key).get<std::string>());
      if (!std::filesystem::is_regular_file(asset) || std::filesystem::file_size(asset) == 0)
        throw std::runtime_error(std::string("qwen3-gguf package requires a non-empty file at /files/") + key);
      if (std::string_view(key) == "model" || std::string_view(key) == "mmproj") {
        std::ifstream input(asset, std::ios::binary);
        char magic[4]{};
        if (!input.read(magic, sizeof(magic)) || std::string_view(magic, 4) != "GGUF")
          throw std::runtime_error(std::string("qwen3-gguf package requires GGUF at /files/") + key);
      }
    }
  }

  return ModelPackageCandidate{
      manifest.at("id").get<std::string>(),
      manifest.at("displayName").get<std::string>(),
      manifest.at("engine").get<std::string>(),
      manifest.at("languages").get<std::vector<std::string>>(),
      package_path,
      manifest_path,
      manifest,
      manifest.contains("revision") ? std::optional<std::string>(manifest.at("revision").get<std::string>())
                                    : std::nullopt,
  };
}

// Scans one registry directory into `tier`. Problems with the directory or an
// entry in it become unsupported entries on `scan`.
void scan_registry_directory(ModelRegistryScan &scan, const std::filesystem::path &registry_directory,
                             bool bundled, std::vector<ModelPackageCandidate> &tier) {
  std::error_code ec;
  if (!std::filesystem::exists(registry_directory, ec)) {
    // An application without bundled models is ordinary; a configured
    // directory that is missing is something the user has to fix.
    if (!bundled) {
      add_unsupported(scan, registry_directory, "registry directory does not exist");
    }
    return;
  }
  if (ec) {
    add_unsupported(scan, registry_directory,
                    "failed to inspect registry directory: " + ec.message());
    return;
  }
  if (!std::filesystem::is_directory(registry_directory, ec)) {
    add_unsupported(scan, registry_directory, "registry path is not a directory");
    return;
  }
  if (ec) {
    add_unsupported(scan, registry_directory,
                    "failed to inspect registry directory type: " + ec.message());
    return;
  }

  std::filesystem::directory_iterator iterator(registry_directory, ec);
  if (ec) {
    add_unsupported(scan, registry_directory,
                    "failed to enumerate registry directory: " + ec.message());
    return;
  }

  for (const auto &entry : iterator) {
    const auto package_path = entry.path().lexically_normal();
    const auto manifest_path = package_path / "model.json";
    // Download staging, update journals, and .previous/ belong to model
    // installation and updates, not to the registry.
    if (package_path.filename().string().rfind('.', 0) == 0) {
      continue;
    }

    if (!entry.is_directory(ec)) {
      if (ec) {
        add_unsupported(scan, package_path,
                        "failed to inspect entry type: " + ec.message());
      } else {
        add_unsupported(scan, package_path, "registry entry is not a directory");
      }
      ec.clear();
      continue;
    }
    if (ec) {
      add_unsupported(scan, package_path, "failed to inspect entry type: " + ec.message());
      ec.clear();
      continue;
    }

    if (!std::filesystem::exists(manifest_path, ec)) {
      if (ec) {
        add_unsupported(scan, manifest_path,
                        "failed to inspect manifest path: " + ec.message());
      } else {
        add_unsupported(scan, package_path, "missing model.json");
      }
      ec.clear();
      continue;
    }
    if (ec) {
      add_unsupported(scan, manifest_path,
                      "failed to inspect manifest path: " + ec.message());
      ec.clear();
      continue;
    }
    if (!std::filesystem::is_regular_file(manifest_path, ec)) {
      if (ec) {
        add_unsupported(scan, manifest_path,
                        "failed to inspect manifest type: " + ec.message());
      } else {
        add_unsupported(scan, manifest_path, "model.json is not a file");
      }
      ec.clear();
      continue;
    }
    if (ec) {
      add_unsupported(scan, manifest_path, "failed to inspect manifest type: " + ec.message());
      ec.clear();
      continue;
    }

    try {
      auto candidate = load_manifest_candidate(package_path, manifest_path);
        candidate.bundled = bundled;
        tier.push_back(std::move(candidate));
    } catch (const std::exception &error) {
      add_unsupported(scan, package_path, error.what());
    }
  }
}

// Keeps the first copy of each id (by manifest path) and reports the rest as
// unsupported: ADR 0010 makes one id twice within a tier an error.
std::vector<ModelPackageCandidate> drop_duplicate_ids(ModelRegistryScan &scan,
                                                      std::vector<ModelPackageCandidate> tier) {
  std::sort(tier.begin(), tier.end(), [](const ModelPackageCandidate &left, const ModelPackageCandidate &right) {
    return std::tie(left.id, left.manifest_path) < std::tie(right.id, right.manifest_path);
  });
  std::vector<ModelPackageCandidate> unique;
  for (auto &candidate : tier) {
    if (!unique.empty() && unique.back().id == candidate.id) {
      add_unsupported(scan, candidate.package_path,
                      "duplicate model id '" + candidate.id + "'; already provided by " +
                          path_to_utf8(unique.back().package_path));
      continue;
    }
    unique.push_back(std::move(candidate));
  }
  return unique;
}

// Lists `<directory>/.previous/<id>@<revision>/` folders. Only the manifest's
// identity is read: these packages are never loaded, and a relative $schema
// no longer resolves one folder deeper. Revert validates in place.
void scan_previous_versions(ModelRegistryScan &scan, const std::filesystem::path &registry_directory) {
  const auto previous_root = registry_directory / ".previous";
  std::error_code ec;
  if (!std::filesystem::is_directory(previous_root, ec)) {
    return;
  }
  for (const auto &entry : std::filesystem::directory_iterator(previous_root, ec)) {
    const auto package_path = entry.path().lexically_normal();
    if (!entry.is_directory(ec) || package_path.filename().string().rfind('.', 0) == 0) {
      continue;
    }
    try {
      const auto manifest_path = package_path / "model.json";
      const auto manifest = load_json_file(manifest_path, "model manifest");
      ModelPackageCandidate candidate;
      candidate.id = manifest.at("id").get<std::string>();
      candidate.display_name = manifest.at("displayName").get<std::string>();
      candidate.engine = manifest.at("engine").get<std::string>();
      candidate.package_path = package_path;
      candidate.manifest_path = manifest_path;
      if (manifest.contains("revision")) {
        candidate.revision = manifest.at("revision").get<std::string>();
      }
      candidate.manifest = manifest;
      scan.previous_packages.push_back(std::move(candidate));
    } catch (const std::exception &error) {
      add_unsupported(scan, package_path, error.what());
    }
  }
}

}  // namespace

std::filesystem::path resolve_registry_directory(const std::filesystem::path &config_path,
                                                 const std::string &entry) {
  const auto candidate = path_from_utf8(entry);
  if (candidate.is_absolute()) {
    return candidate.lexically_normal();
  }

  return (config_path.parent_path() / candidate).lexically_normal();
}

ModelPackageCandidate validate_model_package(const std::filesystem::path &package_path) {
  const auto normalized = package_path.lexically_normal();

  std::error_code ec;
  if (!std::filesystem::is_directory(normalized, ec) || ec) {
    throw std::runtime_error("model package path is not a directory: " + path_to_utf8(normalized));
  }

  const auto manifest_path = normalized / "model.json";
  if (!std::filesystem::exists(manifest_path, ec) || ec) {
    throw std::runtime_error("missing model.json");
  }
  if (!std::filesystem::is_regular_file(manifest_path, ec) || ec) {
    throw std::runtime_error("model.json is not a file");
  }

  return load_manifest_candidate(normalized, manifest_path);
}

ModelRegistryScan scan_model_registry(const ConfigDocument &config) {
  ModelRegistryScan scan;

  std::vector<ModelPackageCandidate> writable;
  std::vector<std::filesystem::path> writable_directories;
  const auto &directories = config.value.at("modelRegistry").at("directories");
  for (const auto &directory_value : directories) {
    const auto registry_directory =
        resolve_registry_directory(config.paths.config_path, directory_value.get<std::string>());
    writable_directories.push_back(registry_directory);
    scan_registry_directory(scan, registry_directory, false, writable);
    scan_previous_versions(scan, registry_directory);
  }
  writable = drop_duplicate_ids(scan, std::move(writable));

  // In portable mode, or when a configured directory is the application's
  // models/, there is only one tier.
  std::vector<ModelPackageCandidate> bundled;
  const auto &bundled_directory = config.paths.bundled_models_dir;
  if (!bundled_directory.empty() &&
      std::find(writable_directories.begin(), writable_directories.end(),
                bundled_directory.lexically_normal()) == writable_directories.end()) {
    scan_registry_directory(scan, bundled_directory.lexically_normal(), true, bundled);
    bundled = drop_duplicate_ids(scan, std::move(bundled));
  }

  for (auto &candidate : writable) {
    const auto shadowed = std::find_if(bundled.begin(), bundled.end(),
                                       [&](const ModelPackageCandidate &other) { return other.id == candidate.id; });
    if (shadowed == bundled.end()) {
      scan.discovered_packages.push_back(std::move(candidate));
      continue;
    }
    const auto expected = catalogue_revision_for(candidate.id);
    const bool bundled_wins = expected && shadowed->revision == expected && candidate.revision != expected;
    scan.discovered_packages.push_back(std::move(bundled_wins ? *shadowed : candidate));
    scan.superseded_packages.push_back(std::move(bundled_wins ? candidate : *shadowed));
    bundled.erase(shadowed);
  }
  for (auto &candidate : bundled) {
    scan.discovered_packages.push_back(std::move(candidate));
  }

  std::sort(scan.discovered_packages.begin(), scan.discovered_packages.end(),
            [](const ModelPackageCandidate &left, const ModelPackageCandidate &right) {
              return std::tie(left.id, left.manifest_path) <
                     std::tie(right.id, right.manifest_path);
            });
  std::sort(scan.superseded_packages.begin(), scan.superseded_packages.end(),
            [](const ModelPackageCandidate &left, const ModelPackageCandidate &right) {
              return std::tie(left.id, left.manifest_path) <
                     std::tie(right.id, right.manifest_path);
            });
  std::sort(scan.previous_packages.begin(), scan.previous_packages.end(),
            [](const ModelPackageCandidate &left, const ModelPackageCandidate &right) {
              return std::tie(left.id, left.manifest_path) <
                     std::tie(right.id, right.manifest_path);
            });
  std::sort(scan.unsupported_entries.begin(), scan.unsupported_entries.end(),
            [](const UnsupportedRegistryEntry &left, const UnsupportedRegistryEntry &right) {
              return std::tie(left.path, left.reason) < std::tie(right.path, right.reason);
            });

  return scan;
}

bool registry_scan_changed(const ModelRegistryScan &previous, const ModelRegistryScan &current) {
  if (previous.discovered_packages.size() != current.discovered_packages.size() ||
      previous.unsupported_entries.size() != current.unsupported_entries.size()) {
    return true;
  }

  for (std::size_t index = 0; index < previous.discovered_packages.size(); ++index) {
    const auto &left = previous.discovered_packages[index];
    const auto &right = current.discovered_packages[index];
    if (left.id != right.id || left.manifest_path != right.manifest_path ||
        left.revision != right.revision) {
      return true;
    }
  }

  if (previous.superseded_packages.size() != current.superseded_packages.size()) {
    return true;
  }
  for (std::size_t index = 0; index < previous.superseded_packages.size(); ++index) {
    if (previous.superseded_packages[index].manifest_path != current.superseded_packages[index].manifest_path) {
      return true;
    }
  }

  if (previous.previous_packages.size() != current.previous_packages.size()) {
    return true;
  }
  for (std::size_t index = 0; index < previous.previous_packages.size(); ++index) {
    if (previous.previous_packages[index].manifest_path != current.previous_packages[index].manifest_path) {
      return true;
    }
  }

  for (std::size_t index = 0; index < previous.unsupported_entries.size(); ++index) {
    const auto &left = previous.unsupported_entries[index];
    const auto &right = current.unsupported_entries[index];
    if (left.path != right.path || left.reason != right.reason) {
      return true;
    }
  }

  return false;
}

}  // namespace tts_host
