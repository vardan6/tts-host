#include "tts_host/model_catalogue.hpp"
#include "tts_host/model_registry.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>

#include <nlohmann/json.hpp>

// ADR 0010 two-tier discovery: one id twice within a tier is an error, and one
// id in both tiers resolves to the copy matching the compiled catalogue, else
// the writable copy.

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

void write_package(const std::filesystem::path &directory, const std::filesystem::path &schema_path,
                   const std::string &folder, const std::string &id,
                   const std::optional<std::string> &revision) {
  json manifest{
      {"$schema", schema_path.generic_string()},
      {"schemaVersion", 1},
      {"id", id},
      {"displayName", "Package " + id},
      {"engine", "kokoro-onnx"},
      {"languages", json::array({"en"})},
      {"files", {{"model", "model.onnx"}}},
      {"license", {{"name", "Apache-2.0"}, {"url", "https://example.com/" + id}}},
  };
  if (revision) {
    manifest["revision"] = *revision;
  }
  write_file(directory / folder / "model.json", manifest.dump(2));
  write_file(directory / folder / "model.onnx", "stub weights");
}

tts_host::ConfigDocument make_config(const std::filesystem::path &work, const std::filesystem::path &config_schema,
                                     const json &directories, const std::filesystem::path &bundled) {
  tts_host::ConfigDocument document;
  document.value = json{
      {"schemaVersion", 1},
      {"modelRegistry",
       {{"directories", directories},
        {"watchForChanges", true},
        {"idleUnloadSeconds", 600},
        {"maximumLoadedGpuModels", 1}}},
  };
  document.paths.config_path = work / "config.json";
  document.paths.schema_path = config_schema;
  document.paths.bundled_models_dir = bundled;
  return document;
}

tts_host::ModelPackageCandidate only_discovered(const tts_host::ModelRegistryScan &scan,
                                                const std::string &label) {
  require(scan.discovered_packages.size() == 1,
          label + ": expected one discovered package, got " + std::to_string(scan.discovered_packages.size()));
  return scan.discovered_packages.front();
}

}  // namespace

int main(int argc, char **argv) {
  try {
    require(argc == 3, "usage: tts-host-model-registry-tier-tests <schemas-directory> <work-directory>");
    const std::filesystem::path schemas(argv[1]);
    const std::filesystem::path work(argv[2]);
    const auto model_schema = schemas / "model.schema.json";
    const auto config_schema = schemas / "config.schema.json";
    std::filesystem::remove_all(work);

    const auto current = tts_host::catalogue_revision_for("kokoro-en-v1");
    require(current.has_value(), "the compiled catalogue has no kokoro-en-v1 revision");
    const std::string stale(64, 'a');

    {
      // Missing bundled models/ is ordinary and reports nothing; a bundled-only
      // package is discovered and marked bundled.
      const auto user = work / "bundled-only" / "user";
      const auto bundled = work / "bundled-only" / "bundled";
      std::filesystem::create_directories(user);
      const auto config = make_config(work / "bundled-only", config_schema, json::array({user.generic_string()}), bundled);
      require(tts_host::scan_model_registry(config).unsupported_entries.empty(),
              "a missing bundled models/ was reported as unsupported");

      write_package(bundled, model_schema, "kokoro-en-v1", "kokoro-en-v1", current);
      const auto scan = tts_host::scan_model_registry(config);
      const auto package = only_discovered(scan, "bundled only");
      require(package.bundled && package.revision == current, "the bundled package lost its tier or revision");
      require(tts_host::package_status(package) == tts_host::PackageStatus::current,
              "a bundled package matching the catalogue is not current");
    }

    {
      // One id twice within the writable tier: first by path kept, second unsupported.
      const auto first = work / "duplicate" / "a";
      const auto second = work / "duplicate" / "b";
      write_package(first, model_schema, "copy", "kokoro-en-v1", std::nullopt);
      write_package(second, model_schema, "copy", "kokoro-en-v1", std::nullopt);
      const auto config = make_config(work / "duplicate", config_schema,
                                      json::array({first.generic_string(), second.generic_string()}), {});
      const auto scan = tts_host::scan_model_registry(config);
      require(only_discovered(scan, "duplicate").package_path == first / "copy",
              "the first copy of a duplicate id was not the one kept");
      require(scan.unsupported_entries.size() == 1 &&
                  scan.unsupported_entries.front().path == second / "copy" &&
                  scan.unsupported_entries.front().reason.find("duplicate model id 'kokoro-en-v1'") !=
                      std::string::npos,
              "a duplicate id within one tier was not reported as unsupported");
    }

    const auto both_tiers = [&](const std::string &name, const std::optional<std::string> &user_revision,
                                const std::optional<std::string> &bundled_revision) {
      const auto user = work / name / "user";
      const auto bundled = work / name / "bundled";
      write_package(user, model_schema, "kokoro-en-v1", "kokoro-en-v1", user_revision);
      write_package(bundled, model_schema, "kokoro-en-v1", "kokoro-en-v1", bundled_revision);
      const auto scan = tts_host::scan_model_registry(
          make_config(work / name, config_schema, json::array({user.generic_string()}), bundled));
      require(scan.unsupported_entries.empty(), name + ": one id across tiers was reported unsupported");
      require(scan.superseded_packages.size() == 1, name + ": expected one superseded copy");
      return scan;
    };

    {
      // A newer bundled copy (application upgrade) beats a stale per-user update.
      const auto scan = both_tiers("bundled-newer", stale, current);
      require(only_discovered(scan, "bundled newer").bundled, "a stale per-user copy shadowed the current bundled one");
      require(!scan.superseded_packages.front().bundled, "the stale per-user copy was not superseded");
    }
    {
      // A per-user update matching the catalogue beats the older bundled copy.
      const auto scan = both_tiers("user-newer", current, stale);
      require(!only_discovered(scan, "user newer").bundled, "the current per-user update lost to a stale bundled copy");
      require(scan.superseded_packages.front().bundled, "the stale bundled copy was not superseded");
    }
    {
      // Neither matches (e.g. a local import over a bundled package): per-user wins.
      const auto scan = both_tiers("neither", std::nullopt, stale);
      require(!only_discovered(scan, "neither").bundled, "per-user did not win when neither copy matched");
    }

    {
      // A configured directory that is the application's models/ is one tier,
      // so its packages are neither duplicated nor superseded.
      const auto models = work / "portable" / "models";
      write_package(models, model_schema, "kokoro-en-v1", "kokoro-en-v1", current);
      const auto scan = tts_host::scan_model_registry(
          make_config(work / "portable", config_schema, json::array({models.generic_string()}), models));
      require(!only_discovered(scan, "portable").bundled && scan.superseded_packages.empty() &&
                  scan.unsupported_entries.empty(),
              "the application's models/ configured as a directory was scanned twice");
    }

    {
      // An in-place update changes only the revision; directory watching must notice.
      const auto user = work / "revision-change" / "user";
      write_package(user, model_schema, "kokoro-en-v1", "kokoro-en-v1", stale);
      const auto config = make_config(work / "revision-change", config_schema, json::array({user.generic_string()}), {});
      const auto before = tts_host::scan_model_registry(config);
      write_package(user, model_schema, "kokoro-en-v1", "kokoro-en-v1", current);
      require(tts_host::registry_scan_changed(before, tts_host::scan_model_registry(config)),
              "a revision change was not reported as a registry change");
    }
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return EXIT_FAILURE;
  }
  std::cout << "model registry tier tests passed\n";
  return EXIT_SUCCESS;
}
