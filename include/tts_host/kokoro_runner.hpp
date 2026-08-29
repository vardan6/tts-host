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
// path still feeds a hardcoded phoneme token sequence rather than the
// `synthesize` request's actual text — text-to-phoneme conversion via
// espeak-ng is a separate, not-yet-implemented slice (see roadmap.md).
class KokoroOnnxRunner {
 public:
  KokoroOnnxRunner();

  nlohmann::json handle_control_message(const nlohmann::json &message);
  nlohmann::json handle_load_message(const nlohmann::json &message);
  RunnerAudioFrame run_synthesis(std::string_view text);
  nlohmann::json make_synthesize_response(const nlohmann::json &message, const RunnerAudioFrame &frame);

 private:
  RunnerAudioFrame run_placeholder_identity_synthesis();
  RunnerAudioFrame run_kokoro_synthesis(std::string_view text);

  Ort::Env env_;
  std::optional<Ort::Session> session_;
  // Flattened per-phoneme-count style table loaded from the voice file
  // (rows of 256 floats each); see run_kokoro_synthesis for row selection.
  std::vector<float> voice_style_;
};

}  // namespace tts_host
