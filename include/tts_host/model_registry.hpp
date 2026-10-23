#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "tts_host/config_loader.hpp"

namespace tts_host {

struct ModelPackageCandidate {
  std::string id;
  std::string display_name;
  std::string engine;
  std::vector<std::string> languages;
  std::filesystem::path package_path;
  std::filesystem::path manifest_path;
  nlohmann::json manifest;
  // ADR 0010 content revision written at catalogue install; absent for local
  // imports and hand-placed packages.
  std::optional<std::string> revision;
  // Found in the application's read-only bundled models/ rather than a
  // configured (writable) modelRegistry.directories entry.
  bool bundled = false;
};

struct UnsupportedRegistryEntry {
  std::filesystem::path path;
  std::string reason;
};

struct ModelRegistryScan {
  std::vector<ModelPackageCandidate> discovered_packages;
  std::vector<UnsupportedRegistryEntry> unsupported_entries;
  // Valid packages that lost ADR 0010 tier precedence to a discovered package
  // with the same id; never loaded.
  std::vector<ModelPackageCandidate> superseded_packages;
  // Versions an update replaced, kept for Revert in a writable directory's
  // .previous/ (ADR 0010); never loaded. Only identity fields and manifest
  // are filled in.
  std::vector<ModelPackageCandidate> previous_packages;
  // Packages whose manifest is valid but whose files cannot load (a missing,
  // empty, or non-GGUF Qwen asset). Also listed in unsupported_entries with
  // the reason; never loaded, kept only so Verify and repair can reach them.
  std::vector<ModelPackageCandidate> damaged_packages;
};

// Scans the configured directories (the writable tier) and, when it is a
// different directory, config.paths.bundled_models_dir (the read-only tier).
// One id twice within a tier is reported as unsupported. One id in both tiers
// resolves by ADR 0010 precedence: the copy whose revision matches the
// compiled catalogue wins, otherwise the writable copy; the loser is
// superseded. Dot-prefixed folders (staging, journals, .previous/) are not
// packages.
ModelRegistryScan scan_model_registry(const ConfigDocument &config);

// Resolves one modelRegistry.directories entry the way scan_model_registry
// does: absolute entries as written, relative entries against the directory
// holding config.json.
std::filesystem::path resolve_registry_directory(const std::filesystem::path &config_path,
                                                 const std::string &entry);

// Validates a single candidate package directory with exactly the checks
// scan_model_registry applies to a registry entry, throwing std::runtime_error
// carrying the same actionable reason the scan would have reported. Used by
// model import to reject a package before it is copied into a registry
// directory.
ModelPackageCandidate validate_model_package(const std::filesystem::path &package_path);

// True if two scans differ in what they found: discovered package ids/paths/revisions,
// superseded and previous-version paths, or unsupported-entry paths/reasons. Used by directory watching to decide whether
// a poll needs to refresh UI (and reset selection) or can be a no-op, since
// scan_model_registry re-validates every manifest on every call and always
// returns a fresh (if identical) result.
bool registry_scan_changed(const ModelRegistryScan &previous, const ModelRegistryScan &current);

}  // namespace tts_host
