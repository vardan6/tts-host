#include "tts_host/model_import.hpp"

#include "tts_host/model_registry.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

#include <nlohmann/json.hpp>

namespace {

using json = nlohmann::json;

void require(bool condition, const std::string &message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void write_file(const std::filesystem::path &path, const std::string &contents) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream output(path, std::ios::binary);
  require(static_cast<bool>(output), "unable to write " + path.string());
  output << contents;
}

// A source package as a user would have it before importing: it sits outside
// any registry directory, and its "$schema" is an absolute path so it
// validates where it currently lives (import repoints the copy).
json source_manifest(const std::filesystem::path &schema_path, const std::string &id) {
  return json{
      {"$schema", schema_path.generic_string()},
      {"schemaVersion", 1},
      {"id", id},
      {"displayName", "Imported " + id},
      {"engine", "stub"},
      {"languages", json::array({"en"})},
      {"files", {{"model", "model.bin"}}},
      {"license", {{"name", "Apache-2.0"}, {"url", "https://example.com/" + id}}},
  };
}

std::filesystem::path make_source_package(const std::filesystem::path &root,
                                          const std::filesystem::path &schema_path,
                                          const std::string &id, const json &manifest_overrides,
                                          bool write_model_file) {
  const auto package = root / id;
  std::filesystem::remove_all(package);
  auto manifest = source_manifest(schema_path, id);
  manifest.merge_patch(manifest_overrides);
  write_file(package / "model.json", manifest.dump(2));
  if (write_model_file) {
    write_file(package / "model.bin", "stub weights");
  }
  return package;
}

tts_host::ConfigDocument make_config(const std::filesystem::path &config_path,
                                     const std::filesystem::path &config_schema_path,
                                     const std::filesystem::path &registry_directory) {
  tts_host::ConfigDocument document;
  document.value = json{
      {"schemaVersion", 1},
      {"modelRegistry",
       {{"directories", json::array({registry_directory.generic_string()})},
        {"watchForChanges", false},
        {"idleUnloadSeconds", 600},
        {"maximumLoadedGpuModels", 1}}},
  };
  document.paths.config_path = config_path;
  document.paths.schema_path = config_schema_path;
  return document;
}

void expect_import_error(const std::filesystem::path &source, const tts_host::ConfigDocument &config,
                         const std::string &expected_message) {
  try {
    tts_host::import_model_package(source, config);
  } catch (const std::exception &error) {
    require(std::string(error.what()).find(expected_message) != std::string::npos,
            "importing " + source.string() + " reported '" + error.what() + "', expected '" +
                expected_message + "'");
    return;
  }
  throw std::runtime_error("importing " + source.string() + " unexpectedly succeeded");
}

std::size_t registry_entry_count(const std::filesystem::path &registry_directory) {
  std::size_t count = 0;
  std::error_code ec;
  for (const auto &entry : std::filesystem::directory_iterator(registry_directory, ec)) {
    static_cast<void>(entry);
    ++count;
  }
  return count;
}

}  // namespace

int main(int argc, char **argv) {
  try {
    require(argc == 3, "usage: tts-host-model-import-tests <schemas-directory> <work-directory>");
    const std::filesystem::path schemas_directory(argv[1]);
    const std::filesystem::path work_directory(argv[2]);
    const auto model_schema = schemas_directory / "model.schema.json";
    const auto config_schema = schemas_directory / "config.schema.json";

    std::filesystem::remove_all(work_directory);
    const auto sources = work_directory / "sources";
    const auto registry = work_directory / "registry";
    const auto config = make_config(work_directory / "config.json", config_schema, registry);

    // The registry directory does not exist yet: importing into a configured
    // but not-yet-created model directory is the first-import case.
    const auto source = make_source_package(sources, model_schema, "imported-package", json::object(), true);
    const auto imported = tts_host::import_model_package(source, config);

    require(imported.id == "imported-package" && imported.display_name == "Imported imported-package",
            "import reported the wrong package metadata");
    require(imported.package_path == registry / "imported-package",
            "import copied the package to " + imported.package_path.string());
    require(std::filesystem::is_regular_file(imported.package_path / "model.bin"),
            "import did not copy the package's weights");
    require(std::filesystem::exists(source / "model.bin"), "import removed the source package");

    // Discovery is the point of importing: the copy has to validate where it
    // now lives, which means its "$schema" resolves from the new location.
    const auto scan = tts_host::scan_model_registry(config);
    require(scan.discovered_packages.size() == 1 &&
                scan.discovered_packages.front().id == "imported-package",
            "the imported package was not discovered by a registry scan");
    require(scan.unsupported_entries.empty(),
            "the imported package left an unsupported registry entry: " +
                (scan.unsupported_entries.empty() ? std::string() : scan.unsupported_entries.front().reason));

    // Re-importing the same id must not silently replace or duplicate what is
    // installed.
    expect_import_error(source, config, "already installed");
    expect_import_error(imported.package_path, config, "already in the model directory");

    // Invalid and incomplete packages report the registry's reason, and leave
    // nothing behind in the model directory.
    const auto invalid = make_source_package(sources, model_schema, "invalid-package",
                                             json{{"displayName", nullptr}}, true);
    expect_import_error(invalid, config, "schema validation failed");

    const auto incomplete =
        make_source_package(sources, model_schema, "incomplete-package", json::object(), false);
    expect_import_error(incomplete, config, "/files/model (model.bin) is missing from the package");

    const auto escaping = make_source_package(sources, model_schema, "escaping-package",
                                              json{{"files", {{"model", "../model.bin"}}}}, true);
    expect_import_error(escaping, config, "escapes the package root");

    expect_import_error(sources / "does-not-exist", config, "is not a directory");

    require(registry_entry_count(registry) == 1,
            "a failed import left something behind in the model directory");
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
  return 0;
}
