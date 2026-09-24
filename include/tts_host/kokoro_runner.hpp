#pragma once

#include "tts_host/runner_protocol.hpp"

#include <filesystem>
#include <optional>
#include <vector>

#include <nlohmann/json.hpp>
#include <onnxruntime_cxx_api.h>

namespace tts_host {

// Speaks the runner protocol (docs/adr/0002-runner-protocol.md) backed by a
// real ONNX Runtime session, proving the vendored toolchain executes inside
// the actual runner process rather than only the standalone smoketest. The
// host supplies the model (and, for the real Kokoro-82M model, a voice
// embedding) to load via a `load` request; until that arrives, the runner
// has no session and cannot synthesize.
//
// Two model contracts are supported at `synthesize` time, distinguished by
// the loaded ONNX graph's input count: a single-input placeholder identity
// model (used by CTest, see tests/fixtures/kokoro_runner/placeholder.onnx),
// and the real Kokoro-82M contract (`input_ids`, `style`, `speed`). The real
// path phonemizes request text through espeak-ng, then bounds the resulting
// ids to the model's 510-id input limit.
class KokoroOnnxRunner {
 public:
  KokoroOnnxRunner();

  nlohmann::json handle_control_message(const nlohmann::json &message);
  nlohmann::json handle_load_message(const nlohmann::json &message);
  // Releases the ONNX Runtime session and voice table, returning the runner to
  // its pre-load state: a later synthesize fails as it would before any load,
  // and a later load makes it usable again.
  nlohmann::json handle_unload_message(const nlohmann::json &message);
  std::vector<RunnerAudioFrame> run_synthesis(std::string_view text, double speed = 1.0);
  nlohmann::json make_synthesize_response(const nlohmann::json &message,
                                          const std::vector<RunnerAudioFrame> &frames);

 private:
  std::vector<RunnerAudioFrame> run_placeholder_identity_synthesis();
  std::vector<RunnerAudioFrame> run_kokoro_synthesis(std::string_view text, double speed);

  Ort::Env env_;
  std::optional<Ort::Session> session_;
  // Flattened per-phoneme-count style table loaded from the voice file
  // (rows of 256 floats each); see run_kokoro_synthesis for row selection.
  std::vector<float> voice_style_;
};

}  // namespace tts_host
