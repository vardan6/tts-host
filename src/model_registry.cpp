#include "tts_host/model_registry.hpp"

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

std::filesystem::path resolve_registry_directory(const std::filesystem::path &config_path,
                                                 const std::string &entry) {
  const auto candidate = std::filesystem::path(entry);
  if (candidate.is_absolute()) {
    return candidate.lexically_normal();
  }

  return (config_path.parent_path() / candidate).lexically_normal();
}

void add_unsupported(ModelRegistryScan &scan, std::filesystem::path path, std::string reason) {
  scan.unsupported_entries.push_back(
      UnsupportedRegistryEntry{std::move(path), std::move(reason)});
}

json load_json_file(const std::filesystem::path &path, const std::string &label) {
  std::ifstream input(path);
  if (!input) {
    throw std::runtime_error("Unable to open " + label + ": " + path.string());
  }

  try {
    return json::parse(input);
  } catch (const json::parse_error &error) {
    throw std::runtime_error("Invalid " + label + " JSON in " + path.string() + ": " +
                             error.what());
  }
}

std::filesystem::path resolve_schema_path(const json &manifest,
                                          const std::filesystem::path &manifest_path) {
  const auto schema_it = manifest.find("$schema");
  if (schema_it != manifest.end() && schema_it->is_string()) {
    const auto candidate = std::filesystem::path(schema_it->get<std::string>());
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
    const auto declared_path = std::filesystem::path(value.get<std::string>());
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

  return ModelPackageCandidate{
      manifest.at("id").get<std::string>(),
      manifest.at("displayName").get<std::string>(),
      manifest.at("engine").get<std::string>(),
      manifest.at("languages").get<std::vector<std::string>>(),
      package_path,
      manifest_path,
      manifest,
  };
}

}  // namespace

ModelRegistryScan scan_model_registry(const ConfigDocument &config) {
  ModelRegistryScan scan;

  const auto &directories = config.value.at("modelRegistry").at("directories");
  for (const auto &directory_value : directories) {
    const auto registry_directory =
        resolve_registry_directory(config.paths.config_path, directory_value.get<std::string>());

    std::error_code ec;
    if (!std::filesystem::exists(registry_directory, ec)) {
      add_unsupported(scan, registry_directory, "registry directory does not exist");
      continue;
    }
    if (ec) {
      add_unsupported(scan, registry_directory,
                      "failed to inspect registry directory: " + ec.message());
      continue;
    }
    if (!std::filesystem::is_directory(registry_directory, ec)) {
      add_unsupported(scan, registry_directory, "registry path is not a directory");
      continue;
    }
    if (ec) {
      add_unsupported(scan, registry_directory,
                      "failed to inspect registry directory type: " + ec.message());
      continue;
    }

    std::filesystem::directory_iterator iterator(registry_directory, ec);
    if (ec) {
      add_unsupported(scan, registry_directory,
                      "failed to enumerate registry directory: " + ec.message());
      continue;
    }

    for (const auto &entry : iterator) {
      const auto package_path = entry.path().lexically_normal();
      const auto manifest_path = package_path / "model.json";

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
        scan.discovered_packages.push_back(load_manifest_candidate(package_path, manifest_path));
      } catch (const std::exception &error) {
        add_unsupported(scan, package_path, error.what());
      }
    }
  }

  std::sort(scan.discovered_packages.begin(), scan.discovered_packages.end(),
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

}  // namespace tts_host
