#include "tts_host/model_update.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
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
  std::vector<std::string> urls;
  tts_host::HttpFetchFunction fetcher() {
    return [this](const std::string &url, std::uint64_t, const std::filesystem::path &destination,
                  std::uint64_t, const tts_host::DownloadProgressCallback &) {
      ++fetches;
      urls.push_back(url);
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

// A Qwen-engine package, whose discovery checks its assets' content as well.
tts_host::CatalogueEntry qwen_version() {
  const auto file = [](const std::string &path, const std::string &key, const std::string &body) {
    return tts_host::CatalogueFile{path, key, "https://example.invalid/" + body, tts_host::sha256_hex(body),
                                   body.size()};
  };
  return tts_host::CatalogueEntry{
      "demo-qwen",
      "Demo Qwen",
      "qwen3-gguf",
      {"en"},
      tts_host::CatalogueLicense{"Apache-2.0", "https://example.invalid/licence", false},
      {file("model.gguf", "model", "GGUFweights"), file("mmproj.gguf", "mmproj", "GGUFmmproj"),
       file("reference.wav", "voice", "RIFFvoice")},
      {tts_host::CatalogueGeneratedFile{"reference-license.txt", "voiceLicense", "public domain notice"}},
  };
}

std::size_t previous_count(const tts_host::ConfigDocument &config) {
  return tts_host::scan_model_registry(config).previous_packages.size();
}

// A package with a weight file, a file in a subfolder, and a host-written
// notice, for Verify and repair.
tts_host::CatalogueEntry multi_file_version() {
  auto entry = version("weights-multi");
  entry.files.push_back(tts_host::CatalogueFile{"voices/af.bin", "voices", "https://example.invalid/voices-multi",
                                                tts_host::sha256_hex("voices-multi"), std::string("voices-multi").size()});
  entry.generated_files.push_back(tts_host::CatalogueGeneratedFile{"NOTICE.txt", "notice", "public domain notice"});
  return entry;
}

void write_file(const std::filesystem::path &path, const std::string &contents) {
  std::ofstream(path, std::ios::binary | std::ios::trunc) << contents;
}

std::string read_file(const std::filesystem::path &path) {
  std::ifstream input(path, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
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

    const auto multi = multi_file_version();
    const auto r_multi = tts_host::catalogue_entry_revision(multi);

    {
      // Verify and repair: damaged and missing files are reported with their
      // size, only they are re-downloaded, and the kept previous version stays.
      const auto user = work / "repair" / "user";
      std::filesystem::create_directories(user);
      const auto config = make_config(work / "repair", schemas, user, {});
      FakeNetwork network;
      tts_host::download_catalogue_entry(v1, user, config, network.fetcher(), nullptr);
      tts_host::update_catalogue_package(multi, tts_host::scan_model_registry(config), config, network.fetcher(),
                                         nullptr, nullptr);
      require(installed_revision(config) == r_multi && previous_count(config) == 1, "repair fixture is wrong");

      std::uint64_t hashed = 0;
      const tts_host::DownloadProgressCallback count_hashing = [&](const tts_host::DownloadProgress &progress) {
        hashed = progress.bytes_downloaded;
      };
      auto report = tts_host::verify_catalogue_package(multi, tts_host::scan_model_registry(config), count_hashing);
      require(report.damaged.empty() && report.files_checked == 3 && hashed > 0 && !report.bundled,
              "an intact package did not verify clean");

      bool cancelled = false;
      try {
        tts_host::verify_catalogue_package(multi, tts_host::scan_model_registry(config),
                                           [](const tts_host::DownloadProgress &) { throw std::runtime_error("cancel"); });
      } catch (const std::runtime_error &) {
        cancelled = true;
      }
      require(cancelled, "verification ignored a cancelling progress callback");

      const auto package = user / "demo-update";
      write_file(package / "model.onnx", "xeights-multi");
      std::filesystem::remove(package / "voices" / "af.bin");
      report = tts_host::verify_catalogue_package(multi, tts_host::scan_model_registry(config), nullptr);
      require(report.damaged.size() == 2 && report.damaged[0].relative_path == "model.onnx" &&
                  !report.damaged[0].missing && report.damaged[1].relative_path == "voices/af.bin" &&
                  report.damaged[1].missing &&
                  report.repair_download_bytes == std::string("weights-multi").size() + std::string("voices-multi").size(),
              "damaged and missing files were not reported");

      // A failed download or a refused switch leaves the damaged package as it was.
      network.corrupt = true;
      bool failed = false;
      try {
        tts_host::repair_catalogue_package(multi, report, config, network.fetcher(), nullptr, nullptr);
      } catch (const std::runtime_error &) {
        failed = true;
      }
      require(failed && read_file(package / "model.onnx") == "xeights-multi" &&
                  !std::filesystem::exists(user / ".demo-update.repairing"),
              "a failed repair download changed the package");
      network.corrupt = false;
      const tts_host::PackageSwitchGuard busy = [](const std::string &, const std::function<void()> &) {
        throw std::runtime_error("Model is in use");
      };
      bool refused = false;
      try {
        tts_host::repair_catalogue_package(multi, report, config, network.fetcher(), nullptr, busy);
      } catch (const std::runtime_error &) {
        refused = true;
      }
      require(refused && read_file(package / "model.onnx") == "xeights-multi" &&
                  std::filesystem::exists(package / "NOTICE.txt"),
              "a refused repair changed the package");

      network.urls.clear();
      tts_host::repair_catalogue_package(multi, report, config, network.fetcher(), nullptr, nullptr);
      require(network.urls.empty(), "the retry refetched already staged files");
      require(tts_host::verify_catalogue_package(multi, tts_host::scan_model_registry(config), nullptr).damaged.empty(),
              "the repaired package does not verify");
      require(installed_revision(config) == r_multi && previous_count(config) == 1 &&
                  std::filesystem::is_directory(user / ".previous" / ("demo-update@" + r1)),
              "repair changed the revision or the kept previous version");
      require(!std::filesystem::exists(user / ".demo-update.downloading") &&
                  !std::filesystem::exists(user / ".demo-update.damaged") &&
                  !std::filesystem::exists(user / ".demo-update.repairing"),
              "repair left staging, the damaged copy, or its journal behind");

      // Only damaged files are fetched; a damaged host-written file is rewritten.
      write_file(package / "voices" / "af.bin", "voices-xxxxx");
      write_file(package / "NOTICE.txt", "tampered");
      report = tts_host::verify_catalogue_package(multi, tts_host::scan_model_registry(config), nullptr);
      require(report.damaged.size() == 2 && report.repair_download_bytes == std::string("voices-multi").size(),
              "a damaged host-written file was counted as a download");
      network.urls.clear();
      tts_host::repair_catalogue_package(multi, report, config, network.fetcher(), nullptr, nullptr);
      require(network.urls == std::vector<std::string>{"https://example.invalid/voices-multi"},
              "repair fetched files that were intact");
      require(tts_host::verify_catalogue_package(multi, tts_host::scan_model_registry(config), nullptr).damaged.empty(),
              "the host-written file was not rewritten");

      bool outdated_rejected = false;
      try {
        tts_host::verify_catalogue_package(v2, tts_host::scan_model_registry(config), nullptr);
      } catch (const std::runtime_error &) {
        outdated_rejected = true;
      }
      require(outdated_rejected, "a package at another revision was verified against this one");
    }

    {
      // A damaged bundled package is reported only; the app's models/ is never written.
      const auto user = work / "repair-bundled" / "user";
      const auto bundled = work / "repair-bundled" / "app-models";
      std::filesystem::create_directories(user);
      std::filesystem::create_directories(bundled);
      const auto config = make_config(work / "repair-bundled", schemas, user, bundled);
      FakeNetwork network;
      tts_host::download_catalogue_entry(multi, bundled, config, network.fetcher(), nullptr);
      write_file(bundled / "demo-update" / "model.onnx", "xeights-multi");
      const auto report = tts_host::verify_catalogue_package(multi, tts_host::scan_model_registry(config), nullptr);
      require(report.bundled && report.damaged.size() == 1, "a damaged bundled package was not reported");
      bool refused = false;
      try {
        tts_host::repair_catalogue_package(multi, report, config, network.fetcher(), nullptr, nullptr);
      } catch (const std::runtime_error &) {
        refused = true;
      }
      require(refused && read_file(bundled / "demo-update" / "model.onnx") == "xeights-multi" &&
                  !std::filesystem::exists(bundled / ".demo-update.downloading") &&
                  !std::filesystem::exists(user / "demo-update"),
              "a bundled package was repaired");
    }

    {
      // A Qwen package with a missing asset or a damaged GGUF header does not
      // load, yet Verify reports it and repair restores it; bundled reports only.
      const auto qwen = qwen_version();
      const std::vector<std::pair<std::string, std::string>> damages{
          {"reference.wav", ""}, {"mmproj.gguf", ""}, {"model.gguf", "XXXFweights"}};
      for (const bool bundled_tier : {false, true}) {
        for (const auto &[damaged_file, contents] : damages) {
          const auto root = work / ("repair-qwen-" + std::string(bundled_tier ? "bundled-" : "") + damaged_file);
          const auto user = root / "user";
          const auto bundled = root / "app-models";
          std::filesystem::create_directories(user);
          std::filesystem::create_directories(bundled);
          const auto config = make_config(root, schemas, user, bundled);
          FakeNetwork network;
          const auto directory = bundled_tier ? bundled : user;
          tts_host::download_catalogue_entry(qwen, directory, config, network.fetcher(), nullptr);
          const auto file = directory / "demo-qwen" / damaged_file;
          if (contents.empty()) std::filesystem::remove(file); else write_file(file, contents);

          const auto scan = tts_host::scan_model_registry(config);
          require(scan.discovered_packages.empty() && scan.damaged_packages.size() == 1 &&
                      scan.unsupported_entries.size() == 1,
                  damaged_file + ": a damaged Qwen package was loadable or not kept for maintenance");
          const auto report = tts_host::verify_catalogue_package(qwen, scan, nullptr);
          require(report.damaged.size() == 1 && report.damaged[0].relative_path == damaged_file &&
                      report.damaged[0].missing == contents.empty() && report.bundled == bundled_tier,
                  damaged_file + ": the damaged Qwen file was not reported");
          bool repaired = true;
          try {
            tts_host::repair_catalogue_package(qwen, report, config, network.fetcher(), nullptr, nullptr);
          } catch (const std::runtime_error &) {
            repaired = false;
          }
          require(repaired != bundled_tier, damaged_file + ": repair ignored the package's tier");
          const auto after = tts_host::scan_model_registry(config);
          require(after.discovered_packages.size() == (bundled_tier ? 0u : 1u) &&
                      after.damaged_packages.size() == (bundled_tier ? 1u : 0u),
                  damaged_file + ": repair did not make the Qwen package loadable again");
        }
      }
    }

    {
      // A repair cut short is rolled back while good files were moving, and
      // completed or rolled back after the damaged copy moved aside.
      for (const std::string stage : {"moving", "swapped", "swapped-invalid"}) {
        const auto user = work / ("repair-" + stage) / "user";
        std::filesystem::create_directories(user);
        const auto config = make_config(work / ("repair-" + stage), schemas, user, {});
        FakeNetwork network;
        tts_host::download_catalogue_entry(multi, user, config, network.fetcher(), nullptr);
        const auto package = user / "demo-update";
        write_file(package / "model.onnx", "xeights-multi");
        const auto staging = tts_host::stage_catalogue_entry(multi, user, config, network.fetcher(), nullptr);
        write_file(user / ".demo-update.repairing", "repair");

        if (stage == "moving") {
          std::filesystem::remove(staging / "voices" / "af.bin");
          std::filesystem::rename(package / "voices" / "af.bin", staging / "voices" / "af.bin");
        } else {
          std::filesystem::rename(package, user / ".demo-update.damaged");
          if (stage == "swapped-invalid") std::filesystem::remove(staging / "model.json");
        }

        require(tts_host::recover_interrupted_model_updates(config).size() == 1,
                stage + ": the interrupted repair was not reported");
        const auto report = tts_host::verify_catalogue_package(multi, tts_host::scan_model_registry(config), nullptr);
        require(report.damaged.size() == (stage == "swapped" ? 0u : 1u),
                stage + ": recovery left the wrong package installed");
        require(installed_revision(config) == r_multi && !std::filesystem::exists(user / ".demo-update.repairing") &&
                    !std::filesystem::exists(user / ".demo-update.damaged"),
                stage + ": recovery left its journal or the damaged copy");
        require(tts_host::recover_interrupted_model_updates(config).empty(), stage + ": recovery is not idempotent");
      }
    }

    {
      // A package in a folder not named after its id: repair works in place,
      // and an interrupted one is recovered from the folder its journal names.
      // A journal naming no recoverable folder is kept and reported.
      for (const std::string stage : {"complete", "moving", "unrecoverable"}) {
        const auto user = work / ("repair-renamed-" + stage) / "user";
        std::filesystem::create_directories(user);
        const auto config = make_config(work / ("repair-renamed-" + stage), schemas, user, {});
        FakeNetwork network;
        tts_host::download_catalogue_entry(multi, user, config, network.fetcher(), nullptr);
        const auto package = user / "renamed-demo";
        std::filesystem::rename(user / "demo-update", package);
        write_file(package / "model.onnx", "xeights-multi");

        if (stage == "complete") {
          const auto report = tts_host::verify_catalogue_package(multi, tts_host::scan_model_registry(config), nullptr);
          tts_host::repair_catalogue_package(multi, report, config, network.fetcher(), nullptr, nullptr);
          require(!std::filesystem::exists(user / "demo-update") &&
                      tts_host::verify_catalogue_package(multi, tts_host::scan_model_registry(config), nullptr)
                          .damaged.empty(),
                  "a renamed package was not repaired in place");
          continue;
        }

        // Cut short while good files moved: staging has no manifest yet.
        const auto staging = tts_host::stage_catalogue_entry(multi, user, config, network.fetcher(), nullptr);
        std::filesystem::remove(staging / "model.json");
        std::filesystem::remove(staging / "voices" / "af.bin");
        std::filesystem::rename(package / "voices" / "af.bin", staging / "voices" / "af.bin");
        const auto journal = user / ".demo-update.repairing";
        write_file(journal, stage == "moving" ? "repair\nrenamed-demo" : "repair");

        const auto messages = tts_host::recover_interrupted_model_updates(config);
        require(messages.size() == 1, stage + ": the interrupted renamed repair was not reported");
        if (stage == "moving") {
          require(read_file(package / "voices" / "af.bin") == "voices-multi" && !std::filesystem::exists(journal),
                  "the good file was not put back into the renamed folder");
        } else {
          require(std::filesystem::exists(journal) && std::filesystem::exists(staging / "voices" / "af.bin") &&
                      messages.front().find("left") != std::string::npos,
                  "an unrecoverable repair journal was deleted");
        }
      }

      // A journal may only name an ordinary entry of its directory.
      const auto user = work / "repair-escape" / "user";
      std::filesystem::create_directories(user);
      const auto config = make_config(work / "repair-escape", schemas, user, {});
      for (const std::string name : {"../outside", ".previous", ""}) {
        write_file(user / ".demo-update.repairing", "repair\n" + name);
        const auto messages = tts_host::recover_interrupted_model_updates(config);
        require(messages.size() == 1 && messages.front().find("invalid package folder") != std::string::npos &&
                    std::filesystem::exists(user / ".demo-update.repairing"),
                "a repair journal naming '" + name + "' was trusted");
      }
    }
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return EXIT_FAILURE;
  }
  std::cout << "model update tests passed\n";
  return EXIT_SUCCESS;
}
