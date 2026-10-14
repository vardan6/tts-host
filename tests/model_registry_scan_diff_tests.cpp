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

void write_package(const std::filesystem::path &registry, const std::filesystem::path &schema_path,
                   const std::string &id) {
  const auto package = registry / id;
  write_file(package / "model.json",
             json{
                 {"$schema", schema_path.generic_string()},
                 {"schemaVersion", 1},
                 {"id", id},
                 {"displayName", "Package " + id},
                 {"engine", "stub"},
                 {"languages", json::array({"en"})},
                 {"files", {{"model", "model.bin"}}},
                 {"license", {{"name", "Apache-2.0"}, {"url", "https://example.com/" + id}}},
             }
                 .dump(2));
  write_file(package / "model.bin", "stub weights");
}

tts_host::ConfigDocument make_config(const std::filesystem::path &config_path,
                                     const std::filesystem::path &config_schema_path,
                                     const std::filesystem::path &registry_directory) {
  tts_host::ConfigDocument document;
  document.value = json{
      {"schemaVersion", 1},
      {"modelRegistry",
       {{"directories", json::array({registry_directory.generic_string()})},
        {"watchForChanges", true},
        {"idleUnloadSeconds", 600},
        {"maximumLoadedGpuModels", 1}}},
  };
  document.paths.config_path = config_path;
  document.paths.schema_path = config_schema_path;
  return document;
}

}  // namespace

int main(int argc, char **argv) {
  try {
    require(argc == 2, "usage: tts-host-model-registry-scan-diff-tests <schemas-directory>");
    const std::filesystem::path schemas_directory(argv[1]);
    const auto model_schema = schemas_directory / "model.schema.json";
    const auto config_schema = schemas_directory / "config.schema.json";

    const auto work_directory =
        std::filesystem::temp_directory_path() / "tts-host-model-registry-scan-diff-tests";
    std::filesystem::remove_all(work_directory);
    const auto registry = work_directory / "registry";
    const auto config = make_config(work_directory / "config.json", config_schema, registry);

    // A registry directory that does not exist yet produces an unsupported
    // entry; scanning it twice without any change must report unchanged.
    const auto empty_scan_a = tts_host::scan_model_registry(config);
    const auto empty_scan_b = tts_host::scan_model_registry(config);
    require(!tts_host::registry_scan_changed(empty_scan_a, empty_scan_b),
            "two scans of the same missing directory reported a change");

    // Adding a package changes the discovered set.
    write_package(registry, model_schema, "package-a");
    const auto one_package_scan = tts_host::scan_model_registry(config);
    require(tts_host::registry_scan_changed(empty_scan_a, one_package_scan),
            "adding a package was not detected as a change");

    // Re-scanning with nothing added or removed is unchanged, even though
    // scan_model_registry re-validates and rebuilds every candidate.
    const auto one_package_scan_again = tts_host::scan_model_registry(config);
    require(!tts_host::registry_scan_changed(one_package_scan, one_package_scan_again),
            "an identical re-scan reported a change");

    // Adding a second package changes it again.
    write_package(registry, model_schema, "package-b");
    const auto two_package_scan = tts_host::scan_model_registry(config);
    require(tts_host::registry_scan_changed(one_package_scan, two_package_scan),
            "adding a second package was not detected as a change");

    // Removing a package changes it back.
    std::filesystem::remove_all(registry / "package-b");
    const auto back_to_one_scan = tts_host::scan_model_registry(config);
    require(tts_host::registry_scan_changed(two_package_scan, back_to_one_scan),
            "removing a package was not detected as a change");
    require(!tts_host::registry_scan_changed(one_package_scan, back_to_one_scan),
            "removing back to the original package set reported a change from the original scan");
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
  return 0;
}
