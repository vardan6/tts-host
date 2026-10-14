#pragma once

#include <chrono>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <mutex>
#include "tts_host/recovering_runner.hpp"

#include "tts_host/config_loader.hpp"
#include "tts_host/runner_launcher.hpp"

namespace tts_host {

// Everything needed to run one registry model: the runner executable for its
// declared engine plus the package files the runner has to load. Runner
// binaries are installed beside tts-host, named by convention
// tts-host-<engine>-runner[.exe].
struct ModelRunnerSelection {
  std::string model_id;
  std::string display_name;
  std::string engine;
  std::filesystem::path runner_path;
  std::filesystem::path model_path;
  std::optional<std::filesystem::path> voice_path;
};

std::filesystem::path runner_path_for_engine(const std::string &engine,
                                             const std::filesystem::path &runner_directory);

// Resolves a model id against the configured registry
// (docs/design/architecture.md#model-packages-and-discovery). Throws if the id
// names no discovered package, or if no runner for the package's engine is
// installed in runner_directory.
ModelRunnerSelection resolve_model_runner_selection(const std::string &model_id,
                                                    const ConfigDocument &config,
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
  void load_eager(const ConfigDocument &config, std::function<bool()> cancelled = {});
  void unload();
  Lease acquire(const ModelRunnerSelection &selection, std::function<bool()> cancelled = {},
                std::optional<std::chrono::seconds> idle_override = {});
  bool unload_if_idle(std::chrono::seconds idle_unload_seconds,
                      std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now());
  ModelSessionStatus status() const;

 private:
  void unload_locked();
  void select_locked(const ModelRunnerSelection &selection, bool force);
  void publish_status(ModelSessionStatus status);
  std::filesystem::path runner_directory_;
  std::mutex operation_mutex_;
  mutable std::mutex status_mutex_;
  std::unique_ptr<RecoveringRunner> session_;
  std::optional<ModelRunnerSelection> selection_;
  ModelSessionStatus status_;
  std::chrono::steady_clock::time_point last_used_;
  std::optional<std::chrono::seconds> idle_override_;
};

}  // namespace tts_host
