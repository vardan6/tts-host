#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "tts_host/config_loader.hpp"

namespace tts_host {

struct ConfigReload {
  enum class Outcome { Unchanged, Applied, Rejected };
  Outcome outcome = Outcome::Unchanged;
  // Rejected: the failing JSON path and reason; the caller keeps its document.
  std::string error;
  // Applied: JSON pointers whose new values wait for a restart.
  std::vector<std::string> restart_required;
};

// Notices external edits to config.json (docs/design/architecture.md#live-reload).
// The tray thread polls it with the document it owns. A changed file is read
// only after one further poll sees it unchanged, so a half-written save is
// not reported. Content equal to the document -- the Settings window's own
// saves -- is not a change. Each settled file version is judged once, so an
// invalid edit is reported once and then waits for the next save.
class ConfigFileWatcher {
 public:
  struct Stamp {
    bool exists = false;
    std::filesystem::file_time_type time{};
    std::uintmax_t size = 0;
    bool operator==(const Stamp &other) const {
      return exists == other.exists && time == other.time && size == other.size;
    }
    bool operator!=(const Stamp &other) const { return !(*this == other); }
  };

  ConfigReload poll(ConfigDocument &document);

 private:
  // Empty until the first poll, which therefore reads the file, so an edit
  // racing startup is not lost.
  std::optional<Stamp> settled_;
  std::optional<Stamp> pending_;
};

}  // namespace tts_host
