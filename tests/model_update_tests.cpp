#include "tts_host/model_update.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>

#include <nlohmann/json.hpp>

#include "tts_host/sha256.hpp"

// Manual catalogue model update (docs/design/architecture.md#model-installation-and-updates,
// ADR 0010): staged replacement, one kept previous version, Revert/Delete,
// failure leaving the working package, and recovery of an interrupted commit.

namespace {

using json = nlohmann::json;

void require(bool condition, const std::string &message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

tts_host::ConfigDocument make_config(const std::filesystem::path &work, const std::filesystem::path &schemas,
                                     const std::filesystem::path &user, const std::filesystem::path &bundled) {
  tts_host::ConfigDocument document;
  document.value = json{
      {"schemaVersion", 1},
      {"modelRegistry",
       {{"directories", json::array({user.generic_string()})},
        {"watchForChanges", false},
        {"idleUnloadSeconds", 600},
        {"maximumLoadedGpuModels", 1}}},
  };
  document.paths.config_path = work / "config.json";
  document.paths.schema_path = schemas / "config.schema.json";
  document.paths.bundled_models_dir = bundled;
  return document;
}

// One version of the demo package: its model file holds `weights`.
tts_host::CatalogueEntry version(const std::string &weights) {
  return tts_host::CatalogueEntry{
      "demo-update",
      "Demo Update",
      "kokoro-onnx",
      {"en"},
      tts_host::CatalogueLicense{"Apache-2.0", "https://example.invalid/licence", false},
      {tts_host::CatalogueFile{"model.onnx", "model", "https://example.invalid/" + weights,
                               tts_host::sha256_hex(weights), weights.size()}},
      {},
  };
}

struct FakeNetwork {
  int fetches = 0;
  bool corrupt = false;
  tts_host::HttpFetchFunction fetcher() {
    return [this](const std::string &url, std::uint64_t, const std::filesystem::path &destination,
                  std::uint64_t, const tts_host::DownloadProgressCallback &) {
      ++fetches;
      std::ofstream output(destination, std::ios::binary | std::ios::trunc);
      const auto body = url.substr(url.rfind('/') + 1);
      output << (corrupt ? std::string(body.size(), 'x') : body);
    };
  }
};

std::string installed_revision(const tts_host::ConfigDocument &config) {
  const auto scan = tts_host::scan_model_registry(config);
  require(scan.discovered_packages.size() == 1, "expected exactly one installed package");
  require(scan.unsupported_entries.empty(),
          "staging or .previous/ leaked into the registry: " +
              (scan.unsupported_entries.empty() ? std::string() : scan.unsupported_entries.front().path.string()));
  return scan.discovered_packages.front().revision.value_or("local");
}

std::size_t previous_count(const tts_host::ConfigDocument &config) {
  return tts_host::scan_model_registry(config).previous_packages.size();
}

}  // namespace

int main(int argc, char **argv) {
  try {
    require(argc == 3, "usage: tts-host-model-update-tests <schemas-directory> <work-directory>");
    const std::filesystem::path schemas(argv[1]);
    const std::filesystem::path work(argv[2]);
    std::filesystem::remove_all(work);

    const auto v1 = version("weights-one");
    const auto v2 = version("weights-two");
    const auto v3 = version("weights-three");
    const auto r1 = tts_host::catalogue_entry_revision(v1);
    const auto r2 = tts_host::catalogue_entry_revision(v2);
    const auto r3 = tts_host::catalogue_entry_revision(v3);

    {
      const auto user = work / "in-tier" / "user";
      std::filesystem::create_directories(user);
      const auto config = make_config(work / "in-tier", schemas, user, {});
      FakeNetwork network;
      tts_host::download_catalogue_entry(v1, user, config, network.fetcher(), nullptr);

      // Speech holding the runner refuses the switch; the working package and
      // the staged download both survive, and the retry does not refetch.
      std::string guarded_id;
      const tts_host::PackageSwitchGuard busy = [&](const std::string &id, const std::function<void()> &) {
        guarded_id = id;
        throw std::runtime_error("Model is in use");
      };
      bool refused = false;
      try {
        tts_host::update_catalogue_package(v2, tts_host::scan_model_registry(config), config, network.fetcher(),
                                           nullptr, busy);
      } catch (const std::runtime_error &) {
        refused = true;
      }
      require(refused && guarded_id == "demo-update", "the update did not go through the package-switch guard");
      require(installed_revision(config) == r1, "a refused update changed the installed package");

      const int fetches_before_retry = network.fetches;
      const tts_host::PackageSwitchGuard idle = [](const std::string &, const std::function<void()> &run) { run(); };
      tts_host::update_catalogue_package(v2, tts_host::scan_model_registry(config), config, network.fetcher(),
                                         nullptr, idle);
      require(network.fetches == fetches_before_retry, "the retry refetched an already staged file");
      require(installed_revision(config) == r2, "the update did not install the catalogue revision");
      require(std::filesystem::is_directory(user / ".previous" / ("demo-update@" + r1)),
              "the replaced version was not kept as the previous version");

      // One previous version per id.
      tts_host::update_catalogue_package(v3, tts_host::scan_model_registry(config), config, network.fetcher(),
                                         nullptr, nullptr);
      require(installed_revision(config) == r3 && previous_count(config) == 1 &&
                  std::filesystem::is_directory(user / ".previous" / ("demo-update@" + r2)),
              "more than one previous version was kept");

      // A download that fails verification leaves the working package.
      network.corrupt = true;
      bool rejected = false;
      try {
        tts_host::update_catalogue_package(v1, tts_host::scan_model_registry(config), config, network.fetcher(),
                                           nullptr, nullptr);
      } catch (const std::runtime_error &) {
        rejected = true;
      }
      require(rejected && installed_revision(config) == r3 && previous_count(config) == 1,
              "a failed download disturbed the installed or previous version");
      network.corrupt = false;

      bool already_current = false;
      try {
        tts_host::update_catalogue_package(v3, tts_host::scan_model_registry(config), config, network.fetcher(),
                                           nullptr, nullptr);
      } catch (const std::runtime_error &) {
        already_current = true;
      }
      require(already_current, "a current package was updated again");

      // Revert swaps, so the version reverted from becomes the previous one,
      // and the restored package validates where it was moved back to.
      tts_host::revert_model_package("demo-update", tts_host::scan_model_registry(config), nullptr);
      require(installed_revision(config) == r2 &&
                  std::filesystem::is_directory(user / ".previous" / ("demo-update@" + r3)),
              "revert did not swap the installed and previous versions");

      require(tts_host::delete_previous_model_versions("demo-update", tts_host::scan_model_registry(config)) == 1 &&
                  previous_count(config) == 0,
              "delete previous did not remove the kept version");
      bool nothing_to_revert = false;
      try {
        tts_host::revert_model_package("demo-update", tts_host::scan_model_registry(config), nullptr);
      } catch (const std::runtime_error &) {
        nothing_to_revert = true;
      }
      require(nothing_to_revert && installed_revision(config) == r2, "revert without a previous version did something");
    }

    {
      // An update interrupted after the old folder moved aside is completed
      // from staging on the next start, or rolled back if staging is gone.
      for (const bool staged : {true, false}) {
        const auto name = std::string(staged ? "interrupted-staged" : "interrupted-unstaged");
        const auto user = work / name / "user";
        std::filesystem::create_directories(user);
        const auto config = make_config(work / name, schemas, user, {});
        FakeNetwork network;
        tts_host::download_catalogue_entry(v1, user, config, network.fetcher(), nullptr);
        if (staged) {
          tts_host::stage_catalogue_entry(v2, user, config, network.fetcher(), nullptr);
        }
        std::filesystem::create_directories(user / ".previous");
        std::filesystem::rename(user / "demo-update", user / ".previous" / ("demo-update@" + r1));
        std::ofstream(user / ".demo-update.updating") << "demo-update@" + r1;

        const auto messages = tts_host::recover_interrupted_model_updates(config);
        require(messages.size() == 1, name + ": recovery reported " + std::to_string(messages.size()) + " messages");
        require(installed_revision(config) == (staged ? r2 : r1), name + ": recovery installed the wrong version");
        require(!std::filesystem::exists(user / ".demo-update.updating"), name + ": the journal was left behind");
        require(tts_host::recover_interrupted_model_updates(config).empty(), name + ": recovery is not idempotent");
      }

      // A revert interrupted with the installed version parked puts it back.
      const auto user = work / "interrupted-revert" / "user";
      std::filesystem::create_directories(user);
      const auto config = make_config(work / "interrupted-revert", schemas, user, {});
      FakeNetwork network;
      tts_host::download_catalogue_entry(v1, user, config, network.fetcher(), nullptr);
      std::filesystem::create_directories(user / ".previous");
      std::filesystem::rename(user / "demo-update", user / ".previous" / ".demo-update.reverting");
      require(tts_host::recover_interrupted_model_updates(config).size() == 1 && installed_revision(config) == r1,
              "an interrupted revert was not restored");
    }

    {
      // A revert interrupted after the kept version moved into place finishes
      // only if that version validates; otherwise the working copy goes back
      // and the kept version stays previous. After the last rename there is
      // nothing to recover.
      for (const std::string stage : {"restored-valid", "restored-invalid", "finished"}) {
        const auto user = work / ("revert-" + stage) / "user";
        std::filesystem::create_directories(user);
        const auto config = make_config(work / ("revert-" + stage), schemas, user, {});
        FakeNetwork network;
        tts_host::download_catalogue_entry(v1, user, config, network.fetcher(), nullptr);
        tts_host::update_catalogue_package(v2, tts_host::scan_model_registry(config), config, network.fetcher(),
                                           nullptr, nullptr);
        const auto kept = user / ".previous" / ("demo-update@" + r1);
        if (stage == "restored-invalid") {
          // E.g. kept from before a schema change.
          std::ifstream input(kept / "model.json");
          auto manifest = json::parse(input);
          input.close();
          manifest["schemaVersion"] = 2;
          std::ofstream(kept / "model.json", std::ios::trunc) << manifest.dump(2);
        }
        const auto parking = user / ".previous" / ".demo-update.reverting";
        std::filesystem::rename(user / "demo-update", parking);
        std::filesystem::rename(kept, user / "demo-update");
        if (stage == "finished") {
          std::filesystem::rename(parking, user / ".previous" / ("demo-update@" + r2));
        }

        const auto messages = tts_host::recover_interrupted_model_updates(config);
        require(messages.size() == (stage == "finished" ? 0u : 1u),
                stage + ": recovery reported " + std::to_string(messages.size()) + " messages");
        require(!std::filesystem::exists(parking), stage + ": the parked version was left behind");
        if (stage == "restored-invalid") {
          require(messages.front().find("does not validate") != std::string::npos,
                  stage + ": recovery did not report the invalid previous version");
          require(installed_revision(config) == r2 && std::filesystem::is_directory(kept) && previous_count(config) == 1,
                  stage + ": the working copy was not put back with the invalid version kept");
        } else {
          require(installed_revision(config) == r1 &&
                      std::filesystem::is_directory(user / ".previous" / ("demo-update@" + r2)) &&
                      previous_count(config) == 1,
                  stage + ": the revert was not completed");
        }
        require(tts_host::recover_interrupted_model_updates(config).empty(), stage + ": recovery is not idempotent");
      }
    }

    {
      // A bundled package is never written: the update lands in the per-user
      // directory and the bundled copy is the previous version, so Revert
      // removes the per-user copy.
      const auto user = work / "bundled" / "user";
      const auto bundled = work / "bundled" / "app-models";
      std::filesystem::create_directories(user);
      std::filesystem::create_directories(bundled);
      const auto config = make_config(work / "bundled", schemas, user, bundled);
      FakeNetwork network;
      tts_host::download_catalogue_entry(v1, bundled, config, network.fetcher(), nullptr);
      require(tts_host::scan_model_registry(config).discovered_packages.front().bundled, "fixture is not bundled");

      tts_host::update_catalogue_package(v2, tts_host::scan_model_registry(config), config, network.fetcher(),
                                         nullptr, nullptr);
      auto scan = tts_host::scan_model_registry(config);
      require(installed_revision(config) == r2 && !scan.discovered_packages.front().bundled &&
                  scan.superseded_packages.size() == 1 && scan.superseded_packages.front().bundled,
              "the update of a bundled package did not go to the per-user directory");
      require(std::filesystem::is_directory(bundled / "demo-update") && !std::filesystem::exists(bundled / ".previous"),
              "the bundled models/ was written");
      require(tts_host::remove_superseded_model_copies("demo-update", scan) == 0,
              "a bundled superseded copy was removed");

      tts_host::revert_model_package("demo-update", scan, nullptr);
      scan = tts_host::scan_model_registry(config);
      require(installed_revision(config) == r1 && scan.discovered_packages.front().bundled &&
                  !std::filesystem::exists(user / "demo-update"),
              "reverting to the bundled copy did not remove the per-user one");
    }

    {
      // Superseded per-user copies can be removed; local packages are never updated.
      const auto user = work / "superseded" / "user";
      std::filesystem::create_directories(user);
      const auto config = make_config(work / "superseded", schemas, user, {});
      FakeNetwork network;
      tts_host::download_catalogue_entry(v1, user, config, network.fetcher(), nullptr);
      auto scan = tts_host::scan_model_registry(config);
      scan.superseded_packages.push_back(scan.discovered_packages.front());
      require(tts_host::remove_superseded_model_copies("demo-update", scan) == 1 &&
                  !std::filesystem::exists(user / "demo-update"),
              "a superseded per-user copy was not removed");

      tts_host::download_catalogue_entry(v1, user, config, network.fetcher(), nullptr);
      auto manifest = json::parse(std::ifstream(user / "demo-update" / "model.json"));
      manifest.erase("revision");
      std::ofstream(user / "demo-update" / "model.json") << manifest.dump(2);
      bool local_rejected = false;
      try {
        tts_host::update_catalogue_package(v2, tts_host::scan_model_registry(config), config, network.fetcher(),
                                           nullptr, nullptr);
      } catch (const std::runtime_error &) {
        local_rejected = true;
      }
      require(local_rejected && installed_revision(config) == "local", "a local package was updated");
    }

    {
      // Remove… deletes the package and, unless kept, its previous version;
      // profiles naming it are listed, and a refused switch leaves it whole.
      for (const bool keep_previous : {false, true}) {
        const auto name = std::string(keep_previous ? "remove-keep" : "remove");
        const auto user = work / name / "user";
        std::filesystem::create_directories(user);
        auto config = make_config(work / name, schemas, user, {});
        config.value["profiles"] = json{{"fast", {{"model", "kokoro-en-v1"}}}, {"quality", {{"model", "demo-update"}}}};
        FakeNetwork network;
        tts_host::download_catalogue_entry(v1, user, config, network.fetcher(), nullptr);
        tts_host::update_catalogue_package(v2, tts_host::scan_model_registry(config), config, network.fetcher(),
                                           nullptr, nullptr);

        const auto plan = tts_host::plan_model_removal("demo-update", tts_host::scan_model_registry(config), config);
        require(plan.package_path == (user / "demo-update").lexically_normal() &&
                    plan.package_bytes >= std::string("weights-two").size(),
                name + ": the plan did not measure the installed package");
        require(plan.previous_path && plan.previous_bytes >= std::string("weights-one").size(),
                name + ": the plan did not find the kept previous version");
        require(plan.profiles == std::vector<std::string>{"quality"}, name + ": the plan listed the wrong profiles");
        require(plan.last_package && !plan.bundled_becomes_active, name + ": the only package was not flagged last");

        const tts_host::PackageSwitchGuard busy = [](const std::string &, const std::function<void()> &) {
          throw std::runtime_error("Model is in use");
        };
        bool refused = false;
        try {
          tts_host::remove_model_package(plan, keep_previous, busy);
        } catch (const std::runtime_error &) {
          refused = true;
        }
        require(refused && installed_revision(config) == r2 && previous_count(config) == 1,
                name + ": a refused removal changed the installed models");

        tts_host::remove_model_package(plan, keep_previous, nullptr);
        const auto scan = tts_host::scan_model_registry(config);
        require(scan.discovered_packages.empty() && scan.unsupported_entries.empty(),
                name + ": the package was not removed cleanly");
        require(scan.previous_packages.size() == (keep_previous ? 1u : 0u),
                name + ": the previous version was not handled as asked");
        require(!std::filesystem::exists(user / ".demo-update.removing") &&
                    !std::filesystem::exists(user / ".previous" / (".demo-update@" + r1 + ".removing")),
                name + ": a removal folder was left behind");
      }
    }

    {
      // Removing a per-user copy that shadowed a bundled one makes the bundled
      // copy active again; the bundled copy itself is read-only.
      const auto user = work / "remove-bundled" / "user";
      const auto bundled = work / "remove-bundled" / "app-models";
      std::filesystem::create_directories(user);
      std::filesystem::create_directories(bundled);
      const auto config = make_config(work / "remove-bundled", schemas, user, bundled);
      FakeNetwork network;
      tts_host::download_catalogue_entry(v1, bundled, config, network.fetcher(), nullptr);
      tts_host::update_catalogue_package(v2, tts_host::scan_model_registry(config), config, network.fetcher(),
                                         nullptr, nullptr);

      const auto plan = tts_host::plan_model_removal("demo-update", tts_host::scan_model_registry(config), config);
      require(plan.bundled_becomes_active && !plan.last_package && !plan.previous_path,
              "the plan did not report the bundled copy taking over");
      tts_host::remove_model_package(plan, false, nullptr);
      const auto scan = tts_host::scan_model_registry(config);
      require(installed_revision(config) == r1 && scan.discovered_packages.front().bundled,
              "the bundled copy did not become active again");

      bool bundled_refused = false;
      try {
        tts_host::plan_model_removal("demo-update", scan, config);
      } catch (const std::runtime_error &) {
        bundled_refused = true;
      }
      require(bundled_refused && std::filesystem::is_directory(bundled / "demo-update"),
              "a bundled package was offered for removal");
    }

    {
      // A removal cut short after its rename is finished on the next start.
      const auto user = work / "remove-interrupted" / "user";
      std::filesystem::create_directories(user / ".previous" / ".demo-update@old.removing");
      std::filesystem::create_directories(user / ".demo-update.removing");
      std::ofstream(user / ".demo-update.removing" / "model.onnx") << "weights";
      const auto config = make_config(work / "remove-interrupted", schemas, user, {});
      require(tts_host::recover_interrupted_model_updates(config).empty() &&
                  !std::filesystem::exists(user / ".demo-update.removing") &&
                  !std::filesystem::exists(user / ".previous" / ".demo-update@old.removing"),
              "an interrupted removal was not finished");
    }
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return EXIT_FAILURE;
  }
  std::cout << "model update tests passed\n";
  return EXIT_SUCCESS;
}
