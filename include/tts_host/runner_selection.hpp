#pragma once

#include <chrono>
#include <filesystem>
#include <optional>
#include <string>

namespace tts_host {

// Everything needed to run one model: the runner executable for its engine
// plus the package files the runner has to load. A --runner override names
// only the runner, with no model path or identity. Runner binaries are
// installed beside tts-host, named by convention tts-host-<engine>-runner[.exe].
struct RunnerSelection {
  std::filesystem::path runner_path;
  std::optional<std::filesystem::path> model_path;
  std::optional<std::filesystem::path> voice_path;
  std::string model_id, display_name, engine;
  // The selecting profile's idleUnloadSeconds, replacing the registry's.
  std::optional<std::chrono::seconds> idle_override;
};

}  // namespace tts_host
