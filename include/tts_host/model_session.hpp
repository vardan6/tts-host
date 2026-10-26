#pragma once

#include <chrono>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <mutex>
#include "tts_host/recovering_runner.hpp"

#include "tts_host/config_loader.hpp"
#include "tts_host/runner_launcher.hpp"
#include "tts_host/runner_selection.hpp"

namespace tts_host {

// ADR 0010 layout: <application directory>/runners/<engine>/tts-host-<engine>-runner.
std::filesystem::path runner_path_for_engine(const std::string &engine,
                                             const std::filesystem::path &application_directory);

// Resolves a model id against the configured registry
// (docs/design/architecture.md#model-packages-and-discovery). Throws if the id
// names no discovered package, or if no runner for the package's engine is
// installed under runner_directory (the application directory). The result
// carries no idle override; profile resolution adds the profile's.
RunnerSelection resolve_model_runner_selection(const std::string &model_id, const ConfigDocument &config,
                                               const std::filesystem::path &runner_directory);

struct ModelSessionStatus {
  bool loaded = false;
  // Empty unless loaded; describes the model the resident runner holds.
  std::string model_id;
  std::string display_name;
  std::string engine;
};

// One resident runner shared by tray speech and Settings. A speech lease
// serializes runner I/O across the entire utterance, including lookahead and
// playback. UI load/unload fails promptly while busy; timer polling never waits.
class ModelSessionManager {
 public:
  explicit ModelSessionManager(std::filesystem::path runner_directory);
  ~ModelSessionManager();
  ModelSessionManager(const ModelSessionManager &) = delete;
  ModelSessionManager &operator=(const ModelSessionManager &) = delete;

  class Lease {
   public:
    Lease(Lease &&other) noexcept;
    ~Lease();
    RecoveringRunner &runner() { return *owner_->session_; }
   private:
    friend class ModelSessionManager;
    Lease(ModelSessionManager &owner, std::unique_lock<std::mutex> lock);
    ModelSessionManager *owner_;
    std::unique_lock<std::mutex> lock_;
  };

  void load(const std::string &model_id, const ConfigDocument &config,
            std::optional<std::chrono::seconds> idle_override = {});
  // Loads `selection` ahead of speech: the eager profile's, picked by
  // eager_selection() (profile_resolution.hpp).
  void warm(const RunnerSelection &selection, std::function<bool()> cancelled = {});
  void unload();
  // Runs `switch_package` while no speech holds the runner, after releasing
  // a resident runner of `model_id` so its files are closed and the next use
  // reloads from the new folder. Throws without calling it while speech is
  // active: an utterance must finish or be stopped before replacement.
  void replace_package(const std::string &model_id, const std::function<void()> &switch_package);
  // The selection's idle_override replaces the registry's idle timeout while
  // it stays resident.
  Lease acquire(const RunnerSelection &selection, std::function<bool()> cancelled = {});
  bool unload_if_idle(std::chrono::seconds idle_unload_seconds,
                      std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now());
  ModelSessionStatus status() const;
  // Model id -> the runner's reason, for each model whose last load was
  // rejected; an entry goes once a load of it succeeds
  // (docs/design/architecture.md#load-rejection).
  std::map<std::string, std::string> load_failures() const;

 private:
  void unload_locked();
  void select_locked(const RunnerSelection &selection, bool force);
  void publish_status(ModelSessionStatus status);
  std::filesystem::path runner_directory_;
  std::mutex operation_mutex_;
  mutable std::mutex status_mutex_;
  std::unique_ptr<RecoveringRunner> session_;
  std::optional<RunnerSelection> selection_;
  ModelSessionStatus status_;
  std::map<std::string, std::string> load_failures_;
  std::chrono::steady_clock::time_point last_used_;
  std::optional<std::chrono::seconds> idle_override_;
};

}  // namespace tts_host
