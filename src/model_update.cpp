#include "tts_host/model_update.hpp"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <system_error>

#include <nlohmann/json.hpp>

#include "tts_host/path_encoding.hpp"

namespace tts_host {

namespace {

// Written before the old folder moves aside and removed once the staged one
// is in place, so a start that finds it knows a commit was cut short. Holds
// the name the old folder was given under .previous/.
std::filesystem::path update_journal_path(const std::filesystem::path &directory, const std::string &id) {
  return directory / ("." + id + ".updating");
}

// Where revert parks the version it replaces until the kept one is in place.
std::filesystem::path revert_parking_path(const std::filesystem::path &directory, const std::string &id) {
  return directory / ".previous" / ("." + id + ".reverting");
}

std::string previous_folder_name(const std::string &id, const std::optional<std::string> &revision) {
  return id + "@" + revision.value_or("local");
}

void run_guarded(const PackageSwitchGuard &guard, const std::string &id, const std::function<void()> &switch_package) {
  if (guard) {
    guard(id, switch_package);
  } else {
    switch_package();
  }
}

const ModelPackageCandidate &installed_package(const std::string &id, const ModelRegistryScan &scan) {
  const auto found = std::find_if(scan.discovered_packages.begin(), scan.discovered_packages.end(),
                                  [&](const ModelPackageCandidate &package) { return package.id == id; });
  if (found == scan.discovered_packages.end()) {
    throw std::runtime_error("'" + id + "' is not installed");
  }
  return *found;
}

void remove_previous_versions_in(const std::filesystem::path &directory, const std::string &id,
                                 const ModelRegistryScan &scan) {
  for (const auto &previous : scan.previous_packages) {
    if (previous.id == id && previous.package_path.parent_path().parent_path() == directory) {
      std::filesystem::remove_all(previous.package_path);
    }
  }
}

std::optional<std::string> manifest_revision(const std::filesystem::path &package_path) {
  std::ifstream input(package_path / "model.json");
  const auto manifest = nlohmann::json::parse(input);
  if (!manifest.contains("revision")) {
    return std::nullopt;
  }
  return manifest.at("revision").get<std::string>();
}

std::string read_text(const std::filesystem::path &path) {
  std::ifstream input(path, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

bool validates(const std::filesystem::path &package_path) {
  try {
    validate_model_package(package_path);
    return true;
  } catch (const std::exception &) {
    return false;
  }
}

}  // namespace

std::filesystem::path writable_model_directory(const ConfigDocument &config) {
  const auto &directories = config.value.at("modelRegistry").at("directories");
  if (directories.empty()) {
    throw std::runtime_error("no writable model directory is configured (modelRegistry.directories is empty)");
  }
  return resolve_registry_directory(config.paths.config_path, directories.front().get<std::string>());
}

ImportedModelPackage update_catalogue_package(const CatalogueEntry &entry, const ModelRegistryScan &scan,
                                              const ConfigDocument &config, const HttpFetchFunction &fetch,
                                              const DownloadProgressCallback &progress,
                                              const PackageSwitchGuard &guard) {
  const auto &current = installed_package(entry.id, scan);
  if (!current.revision) {
    throw std::runtime_error("'" + entry.id + "' is a local package; only catalogue installs are updated");
  }
  const auto revision = catalogue_entry_revision(entry);
  if (*current.revision == revision) {
    throw std::runtime_error("'" + entry.id + "' is already current");
  }

  // Installed mode never writes the application's models/.
  const auto directory = current.bundled ? writable_model_directory(config) : current.package_path.parent_path();
  const auto destination = directory / entry.id;
  if (current.bundled && std::filesystem::exists(destination)) {
    throw std::runtime_error("'" + entry.id + "' already has a per-user copy at " + path_to_utf8(destination));
  }
  std::filesystem::create_directories(directory);
  const auto staging = stage_catalogue_entry(entry, directory, config, fetch, progress);

  run_guarded(guard, entry.id, [&] {
    if (current.bundled) {
      // The bundled copy stays where it is as the previous version.
      std::filesystem::rename(staging, destination);
      return;
    }

    // One previous version per id: the older one goes before this one moves in.
    remove_previous_versions_in(directory, entry.id, scan);
    const auto previous_name = previous_folder_name(entry.id, current.revision);
    const auto previous = directory / ".previous" / previous_name;
    std::filesystem::create_directories(previous.parent_path());

    const auto journal = update_journal_path(directory, entry.id);
    {
      std::ofstream output(journal, std::ios::binary | std::ios::trunc);
      if (!output || !(output << previous_name)) {
        throw std::runtime_error("unable to write " + path_to_utf8(journal));
      }
    }
    std::error_code ec;
    std::filesystem::rename(current.package_path, previous, ec);
    if (ec) {
      // Typically a file another process still holds open on Windows.
      std::filesystem::remove(journal);
      throw std::runtime_error("could not move the installed '" + entry.id + "' aside: " + ec.message());
    }
    std::filesystem::rename(staging, destination, ec);
    if (ec) {
      std::filesystem::rename(previous, current.package_path);
      std::filesystem::remove(journal);
      throw std::runtime_error("could not move the update of '" + entry.id + "' into place: " + ec.message());
    }
    std::filesystem::remove(journal);
  });

  return ImportedModelPackage{entry.id, entry.display_name, destination};
}

void revert_model_package(const std::string &id, const ModelRegistryScan &scan, const PackageSwitchGuard &guard) {
  const auto &current = installed_package(id, scan);
  if (current.bundled) {
    throw std::runtime_error("'" + id + "' is the bundled copy; there is no newer version to revert from");
  }
  const auto directory = current.package_path.parent_path();

  const auto kept = std::find_if(scan.previous_packages.begin(), scan.previous_packages.end(),
                                 [&](const ModelPackageCandidate &package) {
                                   return package.id == id && package.package_path.parent_path().parent_path() == directory;
                                 });
  if (kept == scan.previous_packages.end()) {
    const bool bundled_previous = std::any_of(scan.superseded_packages.begin(), scan.superseded_packages.end(),
                                              [&](const ModelPackageCandidate &package) {
                                                return package.id == id && package.bundled;
                                              });
    if (!bundled_previous) {
      throw std::runtime_error("'" + id + "' has no previous version to revert to");
    }
    // ADR 0010: the bundled copy an update superseded is its previous version.
    run_guarded(guard, id, [&] { std::filesystem::remove_all(current.package_path); });
    return;
  }

  const auto parking = revert_parking_path(directory, id);
  if (std::filesystem::exists(parking)) {
    throw std::runtime_error("an earlier revert of '" + id + "' is unfinished; restart TTS Host to recover it");
  }
  const auto restored = directory / id;
  run_guarded(guard, id, [&] {
    std::filesystem::rename(current.package_path, parking);
    std::error_code ec;
    std::filesystem::rename(kept->package_path, restored, ec);
    if (ec) {
      std::filesystem::rename(parking, current.package_path);
      throw std::runtime_error("could not restore the previous '" + id + "': " + ec.message());
    }
    try {
      validate_model_package(restored);
    } catch (const std::exception &error) {
      std::filesystem::rename(restored, kept->package_path);
      std::filesystem::rename(parking, current.package_path);
      throw std::runtime_error("the previous version of '" + id + "' does not validate: " + error.what());
    }
    std::filesystem::rename(parking, directory / ".previous" / previous_folder_name(id, current.revision));
  });
}

std::size_t delete_previous_model_versions(const std::string &id, const ModelRegistryScan &scan) {
  std::size_t removed = 0;
  for (const auto &previous : scan.previous_packages) {
    if (previous.id == id) {
      std::filesystem::remove_all(previous.package_path);
      ++removed;
    }
  }
  return removed;
}

std::size_t remove_superseded_model_copies(const std::string &id, const ModelRegistryScan &scan) {
  std::size_t removed = 0;
  for (const auto &superseded : scan.superseded_packages) {
    if (superseded.id == id && !superseded.bundled) {
      std::filesystem::remove_all(superseded.package_path);
      ++removed;
    }
  }
  return removed;
}

std::vector<std::string> recover_interrupted_model_updates(const ConfigDocument &config) {
  std::vector<std::string> messages;
  for (const auto &entry : config.value.at("modelRegistry").at("directories")) {
    const auto directory = resolve_registry_directory(config.paths.config_path, entry.get<std::string>());
    std::error_code ec;
    if (!std::filesystem::is_directory(directory, ec)) {
      continue;
    }

    std::vector<std::filesystem::path> journals;
    for (const auto &item : std::filesystem::directory_iterator(directory, ec)) {
      const auto name = item.path().filename().string();
      if (name.size() > 10 && name.front() == '.' && name.compare(name.size() - 9, 9, ".updating") == 0) {
        journals.push_back(item.path());
      }
    }
    for (const auto &journal : journals) {
      const auto name = journal.filename().string();
      const auto id = name.substr(1, name.size() - 10);
      const auto installed = directory / id;
      const auto staging = directory / ("." + id + ".downloading");
      const auto previous = directory / ".previous" / read_text(journal);
      if (std::filesystem::exists(installed)) {
        // The commit finished; only the journal was left.
      } else if (validates(staging)) {
        std::filesystem::rename(staging, installed);
        messages.push_back("Completed the interrupted update of '" + id + "'.");
      } else if (std::filesystem::exists(previous)) {
        std::filesystem::rename(previous, installed);
        messages.push_back("Restored '" + id + "' after an interrupted update.");
      }
      std::filesystem::remove(journal);
    }

    const auto previous_root = directory / ".previous";
    if (!std::filesystem::is_directory(previous_root, ec)) {
      continue;
    }
    std::vector<std::filesystem::path> parked;
    for (const auto &item : std::filesystem::directory_iterator(previous_root, ec)) {
      const auto name = item.path().filename().string();
      if (name.size() > 11 && name.front() == '.' && name.compare(name.size() - 10, 10, ".reverting") == 0) {
        parked.push_back(item.path());
      }
    }
    for (const auto &parking : parked) {
      const auto name = parking.filename().string();
      const auto id = name.substr(1, name.size() - 11);
      const auto installed = directory / id;
      if (std::filesystem::exists(installed)) {
        const auto kept = previous_root / previous_folder_name(id, manifest_revision(parking));
        std::filesystem::remove_all(kept);
        std::filesystem::rename(parking, kept);
        messages.push_back("Completed the interrupted revert of '" + id + "'.");
      } else {
        std::filesystem::rename(parking, installed);
        messages.push_back("Restored '" + id + "' after an interrupted revert.");
      }
    }
  }
  return messages;
}

}  // namespace tts_host
