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

}  // namespace tts_host
