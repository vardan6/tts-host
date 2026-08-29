#pragma once

#include "tts_host/runner_protocol.hpp"

#include <nlohmann/json.hpp>

namespace tts_host {

nlohmann::json handle_stub_runner_control_message(const nlohmann::json &message);
nlohmann::json handle_stub_runner_load_message(const nlohmann::json &message);
RunnerAudioFrame make_stub_runner_synthesis_frame();
nlohmann::json handle_stub_runner_synthesize_message(const nlohmann::json &message);

}  // namespace tts_host
