#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "tts_host/model_registry.hpp"

namespace tts_host {

// One row of the Settings unified model list
// (docs/design/architecture.md#model-removal-repair-and-maintenance-actions):
// an installed package, a catalogue entry that is not installed, or an id
// that only has a kept previous version left.
struct ModelListRow {
  std::string id;
  std::string display_name;
  // "bundled", "per-user", or "catalogue" (offered, not installed).
  std::string source;
  // "current", "update available", "local", "not installed", or
  // "previous version only".
  std::string status;
  // The last rejected load's message, until a load of the id succeeds
  // (docs/design/architecture.md#load-rejection).
  std::optional<std::string> unavailable_reason;
  // Bytes on disk when installed (or of the kept previous version), the
  // download size when not installed.
  std::uintmax_t size_bytes = 0;
  bool loaded = false;
  bool installed = false;
  bool in_catalogue = false;
  bool update_available = false;
};

using FolderSize = std::function<std::uintmax_t(const std::filesystem::path &)>;

// Installed packages in scan order, then not-installed catalogue entries in
// catalogue order, then ids with only a previous version. `loaded_id` is
// empty when nothing is resident.
std::vector<ModelListRow> build_model_list(const ModelRegistryScan &scan, const std::string &loaded_id,
                                           const std::map<std::string, std::string> &load_failures,
                                           const FolderSize &folder_size);

// The details pane text for a row: identity, folder, licence, load result,
// and the superseded copies and previous versions carrying its id. Lines are
// separated by '\n'.
std::string describe_model_details(const ModelListRow &row, const ModelRegistryScan &scan);

// Folders in the model directories that could not be used, with the reason;
// empty when there are none.
std::string describe_unsupported_entries(const ModelRegistryScan &scan);

// Total size of the regular files under `folder`; 0 when it cannot be read.
std::uintmax_t folder_size_on_disk(const std::filesystem::path &folder);

}  // namespace tts_host
