#pragma once

#include <chrono>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

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

// Keeps at most one model resident in a live runner process, which is what the
// settings window's Load/Unload controls act on
// (docs/design/architecture.md#desktop-integration, "model manager (install,
// download, remove, load/unload, idle timeout)").
//
// Unload sends the protocol's `unload` request and then terminates the runner
// process. With one runner process per model (docs/adr/0002-runner-protocol.md)
// process exit is what ultimately frees the weights, so the request is
// best-effort: it lets the runner release them cleanly first, and a runner that
// cannot answer is terminated regardless.
//
// The residency lasts as long as the owning process, so a model loaded from
// the settings window is not yet reused by CLI synthesis, which runs one-shot
// and launches its own runner. That gap closes once a long-lived request
// source exists (the local API server, or a tray session that synthesizes).
// Config's maximumLoadedGpuModels is not honored yet; this manager holds
// exactly one model until told otherwise.
class ModelSessionManager {
 public:
  explicit ModelSessionManager(std::filesystem::path runner_directory);
  ~ModelSessionManager();

  ModelSessionManager(const ModelSessionManager &) = delete;
  ModelSessionManager &operator=(const ModelSessionManager &) = delete;

  // Launches the model's runner and completes the initialize/load handshake.
  // Any model already resident is unloaded first, including when the load
  // fails, so a failed load never leaves a stale session behind.
  void load(const std::string &model_id, const ConfigDocument &config);

  // Terminates the resident runner process. Does nothing when none is loaded.
  void unload();

  // Unloads the resident model if it has been loaded for at least
  // idle_unload_seconds without being reloaded, per
  // modelRegistry.idleUnloadSeconds and
  // docs/design/architecture.md#desktop-integration ("model manager ...
  // idle timeout"). idle_unload_seconds <= 0 disables idle unloading, since
  // the schema's minimum of 0 otherwise has no meaning. There is no
  // synthesis activity to reset the clock yet (docs/requirements/product.md
  // "Models"), so "idle" currently means "time since load", not "time since
  // last use" -- the intended default caller is a timer in whatever process
  // called load(), since that is the only thing that can know it should
  // check. Returns whether it unloaded.
  bool unload_if_idle(std::chrono::seconds idle_unload_seconds,
                      std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now());

  const ModelSessionStatus &status() const { return status_; }

 private:
  std::filesystem::path runner_directory_;
  std::unique_ptr<RunnerSession> session_;
  ModelSessionStatus status_;
  std::chrono::steady_clock::time_point loaded_at_;
};

}  // namespace tts_host
