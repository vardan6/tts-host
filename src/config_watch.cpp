#include "tts_host/config_watch.hpp"

#include <exception>
#include <system_error>
#include <utility>

namespace tts_host {
namespace {

ConfigFileWatcher::Stamp stamp_of(const std::filesystem::path &path) {
  std::error_code time_error;
  std::error_code size_error;
  const auto time = std::filesystem::last_write_time(path, time_error);
  const auto size = std::filesystem::file_size(path, size_error);
  if (time_error || size_error) return {};
  return {true, time, size};
}

// The local API listener is bound once at startup, so every server value --
// not only host and port -- waits for a restart.
std::vector<std::string> restart_only_changes(const nlohmann::json &before,
                                              const nlohmann::json &after) {
  const auto server_of = [](const nlohmann::json &config) {
    return config.value("server", nlohmann::json::object());
  };
  const auto old_server = server_of(before);
  const auto new_server = server_of(after);
  std::vector<std::string> changed;
  for (const auto &[key, value] : old_server.items()) {
    if (!new_server.contains(key) || new_server.at(key) != value) changed.push_back("/server/" + key);
  }
  for (const auto &[key, value] : new_server.items()) {
    if (!old_server.contains(key)) changed.push_back("/server/" + key);
  }
  return changed;
}

}  // namespace

ConfigReload ConfigFileWatcher::poll(ConfigDocument &document) {
  const auto stamp = stamp_of(document.paths.config_path);
  if (settled_ && *settled_ == stamp) {
    pending_.reset();
    return {};
  }
  if (!pending_ || *pending_ != stamp) {
    pending_ = stamp;  // still being written, or just changed: judge it next poll
    return {};
  }
  settled_ = stamp;
  pending_.reset();

  ConfigReload reload;
  if (!stamp.exists) {
    reload.outcome = ConfigReload::Outcome::Rejected;
    reload.error = "the configuration file is missing";
    return reload;
  }
  try {
    auto loaded = reload_config(document.paths);
    if (loaded.value == document.value) return reload;
    reload.restart_required = restart_only_changes(document.value, loaded.value);
    document = std::move(loaded);
    reload.outcome = ConfigReload::Outcome::Applied;
  } catch (const std::exception &error) {
    reload.outcome = ConfigReload::Outcome::Rejected;
    reload.error = error.what();
  }
  return reload;
}

}  // namespace tts_host
