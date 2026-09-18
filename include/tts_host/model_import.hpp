#pragma once

#include <filesystem>
#include <string>

#include "tts_host/config_loader.hpp"

namespace tts_host {

struct ImportedModelPackage {
  std::string id;
  std::string display_name;
  // Where the package now lives inside the configured model directory.
  std::filesystem::path package_path;
};

// Copies a model package directory into the first configured
// modelRegistry.directories entry so the registry discovers it, which is the
// "add a model through the UI" half of
// docs/requirements/product.md#models (the other half -- dropping a folder
// into a model directory by hand -- already works through scan_model_registry).
//
// Nothing is copied unless the source validates: an invalid, incomplete, or
// duplicate package throws std::runtime_error carrying the same actionable
// reason `--list-models` would report for it. Copying a package never executes
// anything the package supplies.
ImportedModelPackage import_model_package(const std::filesystem::path &source_package,
                                          const ConfigDocument &config);

}  // namespace tts_host
