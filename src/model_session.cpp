#include "tts_host/model_session.hpp"

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
                             runner_path.string());
  }

  const auto &files = package->manifest.at("files");
  ModelRunnerSelection selection;
  selection.model_id = package->id;
  selection.display_name = package->display_name;
  selection.engine = package->engine;
  selection.runner_path = std::move(runner_path);
  selection.model_path = package->package_path / files.at("model").get<std::string>();
  if (files.contains("voice")) {
    selection.voice_path = package->package_path / files.at("voice").get<std::string>();
  }
  return selection;
}

ModelSessionManager::ModelSessionManager(std::filesystem::path runner_directory)
    : runner_directory_(std::move(runner_directory)) {}

ModelSessionManager::~ModelSessionManager() {
  try {
    unload();
  } catch (const std::exception &) {
    // A runner that misbehaves on shutdown must not propagate out of a
    // destructor; the process is going away with us either way.
  }
}

void ModelSessionManager::load(const std::string &model_id, const ConfigDocument &config) {
  const auto selection = resolve_model_runner_selection(model_id, config, runner_directory_);

  // Resolve before unloading, so a bad model id or a missing runner leaves the
  // currently resident model untouched.
  unload();

  auto session = std::make_unique<RunnerSession>(selection.runner_path);

  const auto initialize_response = session->send_request(make_runner_initialize_request(1));
  const auto initialize_result = parse_runner_initialize_response(initialize_response);
  if (initialize_result.protocol_version != kRunnerProtocolVersion) {
    throw std::runtime_error("runner reported an unsupported protocol version");
  }

  const auto load_response = session->send_request(make_runner_load_request(
      2, selection.model_path.string(),
      selection.voice_path.has_value() ? std::optional<std::string>(selection.voice_path->string())
                                       : std::nullopt));
  parse_runner_load_response(load_response);

  session_ = std::move(session);
  status_ = ModelSessionStatus{true, selection.model_id, selection.display_name, selection.engine};
  loaded_at_ = std::chrono::steady_clock::now();
}

void ModelSessionManager::unload() {
  if (!session_) {
    return;
  }
  // Clear the state first: whatever the runner does on the way out, this
  // manager no longer holds a loaded model.
  auto session = std::move(session_);
  status_ = ModelSessionStatus{};
  try {
    parse_runner_unload_response(session->send_request(make_runner_unload_request(3)));
  } catch (const std::exception &) {
    // Releasing the weights before exit is the polite path, not the load
    // bearing one: a runner that is already dead, wedged, or too old to know
    // the request still gets terminated below, which frees them anyway.
  }
  // The runner's exit code is not actionable here: the model is unloaded
  // either way, and the caller asked for exactly that.
  session->finish();
}

bool ModelSessionManager::unload_if_idle(std::chrono::seconds idle_unload_seconds,
                                         std::chrono::steady_clock::time_point now) {
  if (!session_ || idle_unload_seconds.count() <= 0) {
    return false;
  }
  if (now - loaded_at_ < idle_unload_seconds) {
    return false;
  }
  unload();
  return true;
}

}  // namespace tts_host
