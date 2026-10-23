#include "tts_host/model_update.hpp"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <optional>
#include <set>
#include <stdexcept>
#include <system_error>

#include <nlohmann/json.hpp>

#include "tts_host/path_encoding.hpp"
#include "tts_host/sha256.hpp"

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

// Verify and repair also reach a package whose files no longer load.
const ModelPackageCandidate &maintained_package(const std::string &id, const ModelRegistryScan &scan) {
  const auto found = std::find_if(scan.damaged_packages.begin(), scan.damaged_packages.end(),
                                  [&](const ModelPackageCandidate &package) { return package.id == id; });
  if (found == scan.damaged_packages.end() ||
      std::any_of(scan.discovered_packages.begin(), scan.discovered_packages.end(),
                  [&](const ModelPackageCandidate &package) { return package.id == id; })) {
    return installed_package(id, scan);
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

// The revision of a package that may not parse, for naming its .previous/ slot.
std::optional<std::string> readable_revision(const std::filesystem::path &package_path) {
  try {
    return manifest_revision(package_path);
  } catch (const std::exception &) {
    return std::nullopt;
  }
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

std::uintmax_t folder_bytes(const std::filesystem::path &folder) {
  std::uintmax_t total = 0;
  std::error_code ec;
  for (const auto &item : std::filesystem::recursive_directory_iterator(folder, ec)) {
    if (item.is_regular_file(ec)) {
      total += item.file_size(ec);
    }
  }
  return total;
}

// Where removal moves a folder before deleting it; dot-prefixed, so neither
// scan sees it.
std::filesystem::path removal_path(const std::filesystem::path &folder) {
  return folder.parent_path() / ("." + folder.filename().string() + ".removing");
}

bool is_removal_leftover(const std::filesystem::path &path) {
  const auto name = path.filename().string();
  return name.size() > 10 && name.front() == '.' && name.compare(name.size() - 9, 9, ".removing") == 0;
}

// Renames `folder` aside, then deletes it. The rename is what fails when a
// file is held open, leaving `folder` whole.
void remove_folder(const std::filesystem::path &folder, const std::string &what) {
  const auto aside = removal_path(folder);
  std::filesystem::remove_all(aside);
  std::error_code ec;
  std::filesystem::rename(folder, aside, ec);
  if (ec) {
    throw std::runtime_error("could not remove " + what + ": " + ec.message() +
                             " (unload the model and close files open in its folder)");
  }
  std::filesystem::remove_all(aside);
}

void remove_leftovers_in(const std::filesystem::path &directory) {
  std::error_code ec;
  std::vector<std::filesystem::path> leftovers;
  for (const auto &item : std::filesystem::directory_iterator(directory, ec)) {
    if (item.is_directory(ec) && is_removal_leftover(item.path())) {
      leftovers.push_back(item.path());
    }
  }
  for (const auto &leftover : leftovers) {
    std::filesystem::remove_all(leftover, ec);
  }
}

// Written before a repair moves the good files into staging and removed once
// the repaired package is in place. Holds "repair" and, on the next line, the
// package's folder name, which need not equal its id.
std::filesystem::path repair_journal_path(const std::filesystem::path &directory, const std::string &id) {
  return directory / ("." + id + ".repairing");
}

// The package folder a repair journal names, or `directory / id` for a
// journal without one. Throws for a name that is not one ordinary entry of
// `directory`.
std::filesystem::path repaired_folder(const std::filesystem::path &journal, const std::string &id) {
  const auto text = read_text(journal);
  const auto newline = text.find('\n');
  if (newline == std::string::npos) return journal.parent_path() / id;
  const auto name = path_from_utf8(text.substr(newline + 1));
  const auto spelled = path_to_utf8(name);
  if (spelled.empty() || spelled.front() == '.' || name.filename() != name || name.has_root_path()) {
    throw std::runtime_error("repair journal " + path_to_utf8(journal) + " names an invalid package folder");
  }
  return journal.parent_path() / name;
}

// Where a repair moves the damaged package before deleting it.
std::filesystem::path repair_discard_path(const std::filesystem::path &directory, const std::string &id) {
  return directory / ("." + id + ".damaged");
}

// Dot-prefixed entries of `directory` whose names end in `suffix`.
std::vector<std::filesystem::path> dot_entries_with_suffix(const std::filesystem::path &directory,
                                                           const std::string &suffix) {
  std::vector<std::filesystem::path> found;
  std::error_code ec;
  for (const auto &item : std::filesystem::directory_iterator(directory, ec)) {
    const auto name = item.path().filename().string();
    if (name.size() > suffix.size() + 1 && name.front() == '.' &&
        name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0) {
      found.push_back(item.path());
    }
  }
  return found;
}

// Moves back into `installed` every file of `staging` it lacks: undoes a repair
// that moved good files into staging before its commit failed.
void put_back_missing_files(const std::filesystem::path &staging, const std::filesystem::path &installed) {
  std::vector<std::filesystem::path> files;
  std::error_code ec;
  for (const auto &item : std::filesystem::recursive_directory_iterator(staging, ec)) {
    if (item.is_regular_file(ec)) {
      files.push_back(item.path().lexically_relative(staging));
    }
  }
  for (const auto &relative : files) {
    const auto target = installed / relative;
    if (!std::filesystem::exists(target)) {
      std::filesystem::create_directories(target.parent_path());
      std::filesystem::rename(staging / relative, target);
    }
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

ModelRemovalPlan plan_model_removal(const std::string &id, const ModelRegistryScan &scan,
                                    const ConfigDocument &config) {
  const auto &current = installed_package(id, scan);
  if (current.bundled) {
    throw std::runtime_error("'" + id + "' is bundled with the app and cannot be removed");
  }
  ModelRemovalPlan plan;
  plan.id = id;
  plan.display_name = current.display_name;
  plan.package_path = current.package_path;
  plan.package_bytes = folder_bytes(current.package_path);

  const auto directory = current.package_path.parent_path();
  for (const auto &previous : scan.previous_packages) {
    if (previous.id == id && previous.package_path.parent_path().parent_path() == directory) {
      plan.previous_path = previous.package_path;
      plan.previous_bytes = folder_bytes(previous.package_path);
    }
  }
  if (config.value.contains("profiles")) {
    for (const auto &profile : config.value.at("profiles").items()) {
      if (profile.value().value("model", "") == id) {
        plan.profiles.push_back(profile.key());
      }
    }
  }
  plan.bundled_becomes_active =
      std::any_of(scan.superseded_packages.begin(), scan.superseded_packages.end(),
                  [&](const ModelPackageCandidate &package) { return package.id == id && package.bundled; });
  plan.last_package = scan.discovered_packages.size() == 1 && !plan.bundled_becomes_active;
  return plan;
}

void remove_model_package(const ModelRemovalPlan &plan, bool keep_previous, const PackageSwitchGuard &guard) {
  run_guarded(guard, plan.id, [&] {
    remove_folder(plan.package_path, "'" + plan.id + "'");
    if (plan.previous_path && !keep_previous) {
      remove_folder(*plan.previous_path, "the previous version of '" + plan.id + "'");
    }
  });
}

ModelVerification verify_catalogue_package(const CatalogueEntry &entry, const ModelRegistryScan &scan,
                                           const DownloadProgressCallback &progress) {
  const auto &current = maintained_package(entry.id, scan);
  if (!current.revision) {
    throw std::runtime_error("'" + entry.id + "' is a local package; only catalogue installs can be verified");
  }
  if (*current.revision != catalogue_entry_revision(entry)) {
    throw std::runtime_error("'" + entry.id + "' is not at the catalogue revision; use Update instead");
  }

  ModelVerification result;
  result.id = entry.id;
  result.display_name = current.display_name;
  result.package_path = current.package_path;
  result.bundled = current.bundled;

  const auto check = [&](const std::string &relative_path, std::uint64_t size_bytes, const std::string &sha256,
                         bool downloaded) {
    ++result.files_checked;
    const auto path = current.package_path / relative_path;
    std::error_code ec;
    const bool missing = !std::filesystem::is_regular_file(path, ec);
    bool damaged = missing;
    if (!missing) {
      const auto size = std::filesystem::file_size(path, ec);
      damaged = ec || size != size_bytes ||
                sha256_hex_of_file(path, [&](std::uint64_t hashed) {
                  if (progress) progress(DownloadProgress{relative_path, hashed, size_bytes});
                }) != sha256;
    }
    if (damaged) {
      result.damaged.push_back(DamagedModelFile{relative_path, size_bytes, missing});
      if (downloaded) result.repair_download_bytes += size_bytes;
    }
  };
  for (const auto &file : entry.files) {
    check(file.relative_path, file.size_bytes, file.sha256, true);
  }
  for (const auto &file : entry.generated_files) {
    check(file.relative_path, file.contents.size(), sha256_hex(file.contents), false);
  }
  return result;
}

void repair_catalogue_package(const CatalogueEntry &entry, const ModelVerification &verification,
                              const ConfigDocument &config, const HttpFetchFunction &fetch,
                              const DownloadProgressCallback &progress, const PackageSwitchGuard &guard) {
  if (verification.damaged.empty()) {
    throw std::runtime_error("'" + entry.id + "' has no damaged files to repair");
  }
  if (verification.bundled) {
    throw std::runtime_error("'" + entry.id +
                             "' is bundled with the app; reinstall or upgrade the application to repair it");
  }
  const auto installed = verification.package_path;
  const auto directory = installed.parent_path();
  const auto staging = catalogue_staging_path(entry, directory);
  std::set<std::string> damaged;
  for (const auto &file : verification.damaged) {
    damaged.insert(file.relative_path);
  }

  // Anything else staged (an abandoned download, a rolled-back repair) would
  // collide with the good files moved in; damaged-file prefixes resume.
  std::error_code ec;
  if (std::filesystem::is_directory(staging, ec)) {
    std::vector<std::filesystem::path> stale;
    for (const auto &item : std::filesystem::recursive_directory_iterator(staging, ec)) {
      if (item.is_regular_file(ec) && damaged.count(item.path().lexically_relative(staging).generic_string()) == 0) {
        stale.push_back(item.path());
      }
    }
    for (const auto &path : stale) {
      std::filesystem::remove(path);
    }
  }
  std::filesystem::create_directories(staging);
  std::vector<std::string> good;
  for (const auto &file : entry.files) {
    if (damaged.count(file.relative_path)) {
      stage_catalogue_file(entry, file, staging, fetch, progress);
    } else {
      good.push_back(file.relative_path);
    }
  }

  run_guarded(guard, entry.id, [&] {
    if (!std::filesystem::is_directory(installed)) {
      throw std::runtime_error("'" + entry.id + "' is no longer installed at " + path_to_utf8(installed));
    }
    const auto journal = repair_journal_path(directory, entry.id);
    {
      std::ofstream output(journal, std::ios::binary | std::ios::trunc);
      if (!output || !(output << "repair\n" << path_to_utf8(installed.filename()))) {
        throw std::runtime_error("unable to write " + path_to_utf8(journal));
      }
    }
    const auto roll_back = [&](const std::string &reason) {
      put_back_missing_files(staging, installed);
      std::filesystem::remove(journal);
      throw std::runtime_error(reason);
    };

    // Good files move rather than copy, so a repair never needs a second copy
    // of a multi-gigabyte package.
    try {
      for (const auto &relative : good) {
        const auto target = staging / relative;
        std::filesystem::create_directories(target.parent_path());
        std::filesystem::rename(installed / relative, target);
      }
      write_catalogue_generated_files(entry, staging, config);
      validate_model_package(staging);
    } catch (const std::exception &error) {
      roll_back("could not assemble the repaired '" + entry.id + "': " + error.what());
    }

    const auto discard = repair_discard_path(directory, entry.id);
    std::filesystem::remove_all(discard);
    std::error_code rename_ec;
    std::filesystem::rename(installed, discard, rename_ec);
    if (rename_ec) {
      roll_back("could not move the damaged '" + entry.id + "' aside: " + rename_ec.message());
    }
    std::filesystem::rename(staging, installed, rename_ec);
    if (rename_ec) {
      std::filesystem::rename(discard, installed);
      roll_back("could not move the repaired '" + entry.id + "' into place: " + rename_ec.message());
    }
    std::filesystem::remove(journal);
    std::error_code cleanup_ec;
    std::filesystem::remove_all(discard, cleanup_ec);
  });
}

std::vector<std::string> recover_interrupted_model_updates(const ConfigDocument &config) {
  std::vector<std::string> messages;
  for (const auto &entry : config.value.at("modelRegistry").at("directories")) {
    const auto directory = resolve_registry_directory(config.paths.config_path, entry.get<std::string>());
    std::error_code ec;
    if (!std::filesystem::is_directory(directory, ec)) {
      continue;
    }
    // A removal cut short after its rename left only a dot-prefixed folder.
    remove_leftovers_in(directory);
    if (std::filesystem::is_directory(directory / ".previous", ec)) {
      remove_leftovers_in(directory / ".previous");
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

    // A repair cut short: with both folders present the good files were still
    // moving, so they go back; with only staging, the swap was under way.
    for (const auto &journal : dot_entries_with_suffix(directory, ".repairing")) {
      const auto name = journal.filename().string();
      const auto id = name.substr(1, name.size() - 11);
      std::filesystem::path installed;
      try {
        installed = repaired_folder(journal, id);
      } catch (const std::exception &error) {
        messages.push_back(std::string(error.what()) + "; left it in place.");
        continue;
      }
      const auto staging = directory / ("." + id + ".downloading");
      const auto discard = repair_discard_path(directory, id);
      const bool staged = std::filesystem::exists(staging);
      if (std::filesystem::exists(installed)) {
        if (staged) {
          put_back_missing_files(staging, installed);
          messages.push_back("Rolled back the interrupted repair of '" + id + "'; run Verify and repair again.");
        }
      } else if (validates(staging)) {
        std::filesystem::rename(staging, installed);
        messages.push_back("Completed the interrupted repair of '" + id + "'.");
      } else if (std::filesystem::exists(discard)) {
        std::filesystem::rename(discard, installed);
        if (staged) {
          put_back_missing_files(staging, installed);
        }
        messages.push_back("Restored '" + id + "' after an interrupted repair; run Verify and repair again.");
      } else {
        // Nothing to restore from: keep the journal, so every start reports
        // the stranded staged files instead of forgetting them.
        messages.push_back("Could not recover the interrupted repair of '" + id + "': " +
                           path_to_utf8(installed) + " is missing; left " + path_to_utf8(journal) + " in place.");
        continue;
      }
      std::filesystem::remove(journal);
    }
    for (const auto &discard : dot_entries_with_suffix(directory, ".damaged")) {
      std::filesystem::remove_all(discard, ec);
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
      if (std::filesystem::exists(installed) && validates(installed)) {
        const auto kept = previous_root / previous_folder_name(id, manifest_revision(parking));
        std::filesystem::remove_all(kept);
        std::filesystem::rename(parking, kept);
        messages.push_back("Completed the interrupted revert of '" + id + "'.");
      } else if (std::filesystem::exists(installed)) {
        // The restored version does not validate: as a completed revert would,
        // keep it as the previous version and put the working copy back.
        std::filesystem::rename(installed, previous_root / previous_folder_name(id, readable_revision(installed)));
        std::filesystem::rename(parking, installed);
        messages.push_back("Restored '" + id + "' after an interrupted revert; the previous version does not validate.");
      } else {
        std::filesystem::rename(parking, installed);
        messages.push_back("Restored '" + id + "' after an interrupted revert.");
      }
    }
  }
  return messages;
}

}  // namespace tts_host
