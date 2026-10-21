#pragma once

#include <filesystem>
#include <functional>
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
// version. Throws, leaving the working package in place, when the installed
// package is local, already current, or any step fails. The staged download
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

// Completes or rolls back an update or revert interrupted mid-commit in any
// configured directory. Run at start before the first scan. Returns one
// message per recovered package.
std::vector<std::string> recover_interrupted_model_updates(const ConfigDocument &config);

}  // namespace tts_host
