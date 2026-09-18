#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "tts_host/config_loader.hpp"

namespace tts_host {

struct ModelPackageCandidate {
  std::string id;
  std::string display_name;
  std::string engine;
  std::vector<std::string> languages;
  std::filesystem::path package_path;
  std::filesystem::path manifest_path;
  nlohmann::json manifest;
};

struct UnsupportedRegistryEntry {
  std::filesystem::path path;
  std::string reason;
};

struct ModelRegistryScan {
  std::vector<ModelPackageCandidate> discovered_packages;
  std::vector<UnsupportedRegistryEntry> unsupported_entries;
};

ModelRegistryScan scan_model_registry(const ConfigDocument &config);

// Resolves one modelRegistry.directories entry the way scan_model_registry
// does: absolute entries as written, relative entries against the directory
// holding config.json.
std::filesystem::path resolve_registry_directory(const std::filesystem::path &config_path,
                                                 const std::string &entry);

// Validates a single candidate package directory with exactly the checks
// scan_model_registry applies to a registry entry, throwing std::runtime_error
// carrying the same actionable reason the scan would have reported. Used by
// model import to reject a package before it is copied into a registry
// directory.
ModelPackageCandidate validate_model_package(const std::filesystem::path &package_path);

// True if two scans differ in what they found: discovered package ids/paths or
// unsupported-entry paths/reasons. Used by directory watching to decide whether
// a poll needs to refresh UI (and reset selection) or can be a no-op, since
// scan_model_registry re-validates every manifest on every call and always
// returns a fresh (if identical) result.
bool registry_scan_changed(const ModelRegistryScan &previous, const ModelRegistryScan &current);

}  // namespace tts_host
