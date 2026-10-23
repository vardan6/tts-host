#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "tts_host/catalogue_download.hpp"
#include "tts_host/config_loader.hpp"
#include "tts_host/model_catalogue.hpp"
#include "tts_host/model_import.hpp"
#include "tts_host/model_registry.hpp"

namespace tts_host {

// Runs `switch_package` -- the folder renames that change which files a model
// id resolves to -- serialized with model loading. The Host passes
// ModelSessionManager::replace_package, which refuses while speech holds the
// runner and releases a resident runner of `model_id` first; a headless run
// with no resident runner just calls it.
using PackageSwitchGuard =
    std::function<void(const std::string &model_id, const std::function<void()> &switch_package)>;

// The damaged copy of `id` maintenance acts on, or null when a loadable copy
// exists or none is damaged. Scan order puts the copy that wins tier
// precedence first (ADR 0010).
const ModelPackageCandidate *damaged_package(const std::string &id, const ModelRegistryScan &scan);

// The writable directory catalogue installs and updates target: the first
// configured modelRegistry.directories entry. Throws when none is configured.
std::filesystem::path writable_model_directory(const ConfigDocument &config);

// Replaces the installed `entry.id` with the catalogue's revision
// (docs/design/architecture.md#model-installation-and-updates, ADR 0010).
// Downloads and validates into staging first; only then, inside `guard`,
// moves the old folder to `.previous/<id>@<revision>/` (replacing any older
// previous version) and the staged one into place. A per-user package is
// replaced in its own directory; a bundled one is never written and the update
// goes to writable_model_directory(), the bundled copy becoming the previous
// version. A damaged package (damaged_package()) is replaced and discarded,
// not kept as the previous version. Throws, leaving the working package in
// place, when the installed package is local, already current, or any step
// fails. The staged download
// survives a failed commit so a retry resumes it.
ImportedModelPackage update_catalogue_package(const CatalogueEntry &entry, const ModelRegistryScan &scan,
                                              const ConfigDocument &config, const HttpFetchFunction &fetch,
                                              const DownloadProgressCallback &progress,
                                              const PackageSwitchGuard &guard);

// Swaps the installed `id` with its kept previous version, so the version
// reverted from becomes the previous one. When the only previous version is a
// bundled copy an update superseded, removes the per-user copy instead
// (ADR 0010). Throws when there is nothing to revert to, or when the restored
// package does not validate, in which case both folders are put back.
void revert_model_package(const std::string &id, const ModelRegistryScan &scan, const PackageSwitchGuard &guard);

// Deletes the kept previous version of `id`. Returns how many folders went.
std::size_t delete_previous_model_versions(const std::string &id, const ModelRegistryScan &scan);

// Deletes per-user copies of `id` that lost tier precedence. Bundled copies
// are read-only and stay. Returns how many folders went.
std::size_t remove_superseded_model_copies(const std::string &id, const ModelRegistryScan &scan);

// What Remove… of an installed package would do, shown in its confirmation
// (docs/design/architecture.md#model-removal-repair-and-maintenance-actions).
struct ModelRemovalPlan {
  std::string id;
  std::string display_name;
  std::filesystem::path package_path;
  std::uintmax_t package_bytes = 0;
  // The kept .previous/ version in the package's directory, deleted with it
  // unless the user keeps it.
  std::optional<std::filesystem::path> previous_path;
  std::uintmax_t previous_bytes = 0;
  // Profiles whose model is `id`: unavailable after removal unless a bundled
  // copy becomes active again. Never switched to another model.
  std::vector<std::string> profiles;
  // The removed per-user copy shadowed a bundled one, which becomes active.
  bool bundled_becomes_active = false;
  // Nothing will be left to speak with (only possible in portable mode or
  // with no bundled models).
  bool last_package = false;
};

// Plans removing the installed or damaged `id`. Throws when it is not
// installed or is a bundled package in installed mode, which is read-only
// (ADR 0010).
ModelRemovalPlan plan_model_removal(const std::string &id, const ModelRegistryScan &scan,
                                    const ConfigDocument &config);

// Deletes the planned package, and its kept previous version unless
// `keep_previous`, inside `guard`. Each folder is first renamed to a dot-prefixed
// `.<name>.removing` beside it, so a file Windows holds open fails the removal
// with the package intact rather than half-deleted; recovery deletes leftovers.
void remove_model_package(const ModelRemovalPlan &plan, bool keep_previous, const PackageSwitchGuard &guard);

// One file of a catalogue package that does not match the compiled catalogue.
struct DamagedModelFile {
  std::string relative_path;
  // The catalogue size: what a repair fetches, or writes for a host-written file.
  std::uint64_t size_bytes = 0;
  bool missing = false;
};

// What Verify and repair… found
// (docs/design/architecture.md#model-removal-repair-and-maintenance-actions).
struct ModelVerification {
  std::string id;
  std::string display_name;
  std::filesystem::path package_path;
  std::size_t files_checked = 0;
  std::vector<DamagedModelFile> damaged;
  // Bytes a repair downloads: damaged files that are not host-written.
  std::uint64_t repair_download_bytes = 0;
  // A bundled package in installed mode is read-only: reported, never repaired.
  bool bundled = false;
};

// Hashes every file of the installed `entry.id` against `entry`, reporting
// each file's hashing through `progress` (which may throw to cancel). Throws
// when the package is not installed, is local, or is not at the entry's
// revision -- an older revision would differ everywhere, and Update replaces it.
ModelVerification verify_catalogue_package(const CatalogueEntry &entry, const ModelRegistryScan &scan,
                                           const DownloadProgressCallback &progress);

// Re-downloads only the damaged files of a verified package into the
// `.<id>.downloading` staging folder, then, inside `guard`, moves the good files
// over, validates, and swaps the staged package into place through the
// `.<id>.repairing` journal. The damaged copy is discarded, not kept as the
// previous version, and an existing `.previous/` version is left alone. Throws,
// leaving the installed package whole, when there is nothing to repair, the
// package is bundled, or any step fails; fetched files stay staged for a retry.
void repair_catalogue_package(const CatalogueEntry &entry, const ModelVerification &verification,
                              const ConfigDocument &config, const HttpFetchFunction &fetch,
                              const DownloadProgressCallback &progress, const PackageSwitchGuard &guard);

// Completes or rolls back an update, revert, or repair interrupted mid-commit
// in any configured directory, and finishes deleting interrupted removals. Run at
// start before the first scan. Returns one message per recovered package.
std::vector<std::string> recover_interrupted_model_updates(const ConfigDocument &config);

}  // namespace tts_host
