#include "tts_host/model_import.hpp"

#include <fstream>
#include <stdexcept>
#include <system_error>

#include <nlohmann/json.hpp>

#include "tts_host/model_registry.hpp"

namespace tts_host {
namespace {

using json = nlohmann::json;

// scan_model_registry only checks that declared file paths stay inside the
// package; an installed package that names weights it does not carry is the
// "incomplete package" case requirements ask to report, so import checks it
// before copying anything.
void require_declared_files_exist(const json &manifest, const std::filesystem::path &package_path) {
  for (const auto &[key, value] : manifest.at("files").items()) {
    const auto declared = package_path / std::filesystem::path(value.get<std::string>());
    std::error_code ec;
    if (!std::filesystem::is_regular_file(declared, ec) || ec) {
      throw std::runtime_error("manifest file path /files/" + key + " (" +
                               value.get<std::string>() + ") is missing from the package");
    }
  }
}

std::filesystem::path installed_model_schema_path(const ConfigDocument &config) {
  return config.paths.schema_path.parent_path() / "model.schema.json";
}

// A package's "$schema" is normally a relative path that only resolves at the
// depth the package was authored at, so a copied manifest would stop
// validating in its new location (see resolve_schema_path in
// src/model_registry.cpp). Repoint the copy at this installation's own model
// schema instead, relatively where possible so the model directory stays
// relocatable.
void repoint_manifest_schema(const std::filesystem::path &package_path,
                             const ConfigDocument &config) {
  const auto schema_path = installed_model_schema_path(config);
  std::error_code ec;
  if (!std::filesystem::is_regular_file(schema_path, ec) || ec) {
    return;  // No installed schema to point at; keep whatever the package declared.
  }

  const auto manifest_path = package_path / "model.json";
  json manifest;
  {
    std::ifstream input(manifest_path);
    if (!input) {
      throw std::runtime_error("unable to read the copied manifest " + manifest_path.string());
    }
    manifest = json::parse(input);
  }

  const auto relative = schema_path.lexically_relative(package_path);
  manifest["$schema"] = relative.empty() ? schema_path.generic_string() : relative.generic_string();

  std::ofstream output(manifest_path);
  if (!output) {
    throw std::runtime_error("unable to write the copied manifest " + manifest_path.string());
  }
  output << manifest.dump(2) << '\n';
}

}  // namespace

ImportedModelPackage import_model_package(const std::filesystem::path &source_package,
                                          const ConfigDocument &config) {
  const auto source = std::filesystem::absolute(source_package).lexically_normal();
  const auto candidate = validate_model_package(source);
  require_declared_files_exist(candidate.manifest, source);

  const auto &directories = config.value.at("modelRegistry").at("directories");
  if (directories.empty()) {
    throw std::runtime_error(
        "no model directory is configured to import into (modelRegistry.directories is empty)");
  }
  const auto destination_root =
      resolve_registry_directory(config.paths.config_path, directories.front().get<std::string>());

  if (source.parent_path() == destination_root) {
    throw std::runtime_error("'" + candidate.id + "' is already in the model directory " +
                             destination_root.string());
  }

  for (const auto &installed : scan_model_registry(config).discovered_packages) {
    if (installed.id == candidate.id) {
      throw std::runtime_error("a model with id '" + candidate.id + "' is already installed at " +
                               installed.package_path.string());
    }
  }

  std::error_code ec;
  std::filesystem::create_directories(destination_root, ec);
  if (ec) {
    throw std::runtime_error("failed to create the model directory " + destination_root.string() +
                             ": " + ec.message());
  }

  // The manifest id is the destination folder name (the schema constrains it to
  // a filename-safe pattern), so the same package imported from anywhere lands
  // in the same place.
  const auto destination = destination_root / candidate.id;
  if (std::filesystem::exists(destination, ec)) {
    throw std::runtime_error("the model directory already contains " + destination.string());
  }

  // Copy into a staging directory and move it into place only once the copy
  // validates, so a failed import leaves nothing the registry would pick up.
  const auto staging = destination_root / ("." + candidate.id + ".importing");
  std::filesystem::remove_all(staging, ec);
  ec.clear();

  std::filesystem::copy(source, staging, std::filesystem::copy_options::recursive, ec);
  if (ec) {
    std::error_code cleanup_ec;
    std::filesystem::remove_all(staging, cleanup_ec);
    throw std::runtime_error("failed to copy " + source.string() + " into " +
                             destination_root.string() + ": " + ec.message());
  }

  try {
    repoint_manifest_schema(staging, config);
    validate_model_package(staging);
  } catch (const std::exception &error) {
    std::error_code cleanup_ec;
    std::filesystem::remove_all(staging, cleanup_ec);
    throw std::runtime_error("the imported copy of '" + candidate.id +
                             "' did not validate in the model directory: " + error.what());
  }

  std::filesystem::rename(staging, destination, ec);
  if (ec) {
    std::error_code cleanup_ec;
    std::filesystem::remove_all(staging, cleanup_ec);
    throw std::runtime_error("failed to move the imported package into " + destination.string() +
                             ": " + ec.message());
  }

  return ImportedModelPackage{candidate.id, candidate.display_name, destination};
}

}  // namespace tts_host
