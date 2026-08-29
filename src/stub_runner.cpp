#include "tts_host/stub_runner.hpp"

#include <string>

namespace tts_host {

nlohmann::json handle_stub_runner_control_message(const nlohmann::json &message) {
  const auto request = parse_runner_initialize_request(message);
  if (request.protocol_version != kRunnerProtocolVersion) {
    throw RunnerProtocolError("stub runner does not support protocol version " +
                              std::to_string(request.protocol_version));
  }
  return make_runner_initialize_response(
      request, {"load", "unload", "synthesize", "cancel", "stats"});
}

nlohmann::json handle_stub_runner_load_message(const nlohmann::json &message) {
  const auto request = parse_runner_load_request(message);
  return make_runner_load_response(request);
}

RunnerAudioFrame make_stub_runner_synthesis_frame() {
  return {.sequence_number = 0,
          .sample_count = 4,
          .flags = kRunnerAudioFrameFlagEndOfStream,
          .payload = {0x00, 0x00, 0x00, 0x10, 0x00, 0xf0, 0x00, 0x00}};
}

nlohmann::json handle_stub_runner_synthesize_message(const nlohmann::json &message) {
  const auto request = parse_runner_synthesize_request(message);
  const auto frame = make_stub_runner_synthesis_frame();
  return make_runner_synthesize_response(request, 24000, 1, "pcm_s16le", frame.sample_count);
}

}  // namespace tts_host
