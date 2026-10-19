#include "tts_host/model_session.hpp"
#include "tts_host/path_encoding.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

#include "tts_host/model_registry.hpp"
#include "tts_host/runner_protocol.hpp"

namespace tts_host {

std::filesystem::path runner_path_for_engine(const std::string &engine,
                                             const std::filesystem::path &runner_directory) {
#ifdef _WIN32
  return runner_directory / ("tts-host-" + engine + "-runner.exe");
#else
  return runner_directory / ("tts-host-" + engine + "-runner");
#endif
}

ModelRunnerSelection resolve_model_runner_selection(
    const std::string &model_id, const ConfigDocument &config,
    const std::filesystem::path &runner_directory) {
  const auto scan = scan_model_registry(config);
  const auto package =
      std::find_if(scan.discovered_packages.begin(), scan.discovered_packages.end(),
                   [&](const ModelPackageCandidate &candidate) { return candidate.id == model_id; });
  if (package == scan.discovered_packages.end()) {
    throw std::runtime_error("Unknown model id: " + model_id);
  }

  auto runner_path = runner_path_for_engine(package->engine, runner_directory);
  if (!std::filesystem::exists(runner_path)) {
    throw std::runtime_error("No runner installed for engine '" + package->engine + "': expected " +
                             path_to_utf8(runner_path));
  }

  const auto &files = package->manifest.at("files");
  ModelRunnerSelection selection;
  selection.model_id = package->id;
  selection.display_name = package->display_name;
  selection.engine = package->engine;
  selection.runner_path = std::move(runner_path);
  selection.model_path = package->package_path / path_from_utf8(files.at("model").get<std::string>());
  // Qwen needs the backbone, mmproj, and reference voice. Pass its validated
  // manifest as the engine's load descriptor without executing package code.
  if (package->engine == "qwen3-gguf") selection.model_path = package->manifest_path;
  if (files.contains("voice")) {
    selection.voice_path = package->package_path / path_from_utf8(files.at("voice").get<std::string>());
  }
  return selection;
}

ModelSessionManager::ModelSessionManager(std::filesystem::path runner_directory)
    : runner_directory_(std::move(runner_directory)) {}

ModelSessionManager::~ModelSessionManager() {
  // The owner must stop/join speech before destroying its manager.
  std::lock_guard lock(operation_mutex_);
  try { unload_locked(); } catch (const std::exception &) {}
}

void ModelSessionManager::publish_status(ModelSessionStatus status) {
  std::lock_guard lock(status_mutex_);
  status_ = std::move(status);
}
ModelSessionStatus ModelSessionManager::status() const {
  std::lock_guard lock(status_mutex_);
  return status_;
}

void ModelSessionManager::unload_locked() {
  publish_status({});
  if (session_) session_->unload();
  session_.reset();
  selection_.reset();
  idle_override_.reset();
}

void ModelSessionManager::select_locked(const ModelRunnerSelection &selection, bool force) {
  if (!force && session_ && selection_ &&
      selection_->runner_path == selection.runner_path &&
      selection_->model_path == selection.model_path &&
      selection_->voice_path == selection.voice_path) {
    selection_ = selection;
    return;
  }
  unload_locked();
  session_ = std::make_unique<RecoveringRunner>(selection.runner_path,
      selection.model_path.empty() ? std::nullopt : std::optional(selection.model_path),
      selection.voice_path);
  selection_ = selection;
  session_->set_state_callback([this](bool loaded) {
    const auto &selected = *selection_;
    publish_status(loaded
        ? ModelSessionStatus{true, selected.model_id, selected.display_name, selected.engine}
        : ModelSessionStatus{});
  });
}

void ModelSessionManager::load(const std::string &model_id, const ConfigDocument &config,
    std::optional<std::chrono::seconds> idle_override) {
  const auto selection = resolve_model_runner_selection(model_id, config, runner_directory_);
  std::unique_lock lock(operation_mutex_, std::try_to_lock);
  if (!lock) throw std::runtime_error("Model is in use; stop speech before loading another model");
  select_locked(selection, true);
  try {
    session_->prepare();
    publish_status({true, selection.model_id, selection.display_name, selection.engine});
    last_used_ = std::chrono::steady_clock::now();
    idle_override_ = idle_override;
  } catch (...) { unload_locked(); throw; }
}

void ModelSessionManager::load_eager(const ConfigDocument &config,
    std::function<bool()> cancelled) {
  // This manager has one resident slot. Prefer an eager language default,
  // then the first eager profile in deterministic configuration-key order.
  const auto &profiles = config.value.at("profiles");
  std::string chosen;
  for (const auto &entry : config.value.at("languageDefaults").items()) {
    const auto name = entry.value().get<std::string>();
    if (profiles.contains(name) && profiles.at(name).value("loadPolicy", "onDemand") == "eager") {
      chosen = name;
      break;
    }
  }
  if (chosen.empty()) {
    for (const auto &entry : profiles.items()) {
      if (entry.value().value("loadPolicy", "onDemand") == "eager") { chosen = entry.key(); break; }
    }
  }
  if (!chosen.empty()) {
    const auto idle_override = profiles.at(chosen).contains("idleUnloadSeconds")
        ? std::optional(std::chrono::seconds(profiles.at(chosen).at("idleUnloadSeconds").get<int>()))
        : std::nullopt;
    const auto selected = resolve_model_runner_selection(
        profiles.at(chosen).at("model").get<std::string>(), config, runner_directory_);
    auto lease = acquire(selected, std::move(cancelled), idle_override);
    lease.runner().prepare();
  }
}

void ModelSessionManager::unload() {
  std::unique_lock lock(operation_mutex_, std::try_to_lock);
  if (!lock) throw std::runtime_error("Model is in use; stop speech before unloading it");
  unload_locked();
}

void ModelSessionManager::replace_package(const std::string &model_id,
                                          const std::function<void()> &switch_package) {
  std::unique_lock lock(operation_mutex_, std::try_to_lock);
  if (!lock) throw std::runtime_error("Model is in use; stop speech before replacing " + model_id);
  if (selection_ && selection_->model_id == model_id) unload_locked();
  switch_package();
}

ModelSessionManager::Lease::Lease(ModelSessionManager &owner, std::unique_lock<std::mutex> lock)
    : owner_(&owner), lock_(std::move(lock)) {}
ModelSessionManager::Lease::Lease(Lease &&other) noexcept
    : owner_(std::exchange(other.owner_, nullptr)), lock_(std::move(other.lock_)) {}
ModelSessionManager::Lease::~Lease() {
  if (!owner_) return;
  owner_->session_->set_cancelled({});
  owner_->last_used_ = std::chrono::steady_clock::now();
  const auto &selected = *owner_->selection_;
  owner_->publish_status(owner_->session_->loaded()
      ? ModelSessionStatus{true, selected.model_id, selected.display_name, selected.engine}
      : ModelSessionStatus{});
}

ModelSessionManager::Lease ModelSessionManager::acquire(const ModelRunnerSelection &selection,
    std::function<bool()> cancelled, std::optional<std::chrono::seconds> idle_override) {
  std::unique_lock lock(operation_mutex_);
  select_locked(selection, false);
  session_->set_cancelled(std::move(cancelled));
  idle_override_ = idle_override;
  return Lease(*this, std::move(lock));
}

bool ModelSessionManager::unload_if_idle(std::chrono::seconds idle_unload_seconds,
                                         std::chrono::steady_clock::time_point now) {
  std::unique_lock lock(operation_mutex_, std::try_to_lock);
  if (!lock || !session_) return false;
  const auto timeout = idle_override_.value_or(idle_unload_seconds);
  if (timeout.count() <= 0 || now - last_used_ < timeout) return false;
  unload_locked();
  return true;
}

}  // namespace tts_host
