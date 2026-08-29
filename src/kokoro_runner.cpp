#include "tts_host/kokoro_runner.hpp"

#include "tts_host/espeak_phonemizer.hpp"
#include "tts_host/kokoro_phoneme_mapping.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

namespace tts_host {
namespace {

Ort::Session open_session(Ort::Env &env, const std::filesystem::path &model_path) {
  Ort::SessionOptions session_options;
#ifdef _WIN32
  return Ort::Session(env, model_path.wstring().c_str(), session_options);
#else
  return Ort::Session(env, model_path.string().c_str(), session_options);
#endif
}

inline constexpr std::size_t kKokoroStyleWidth = 256;

// Kokoro-82M voice packs store one 256-float style row per possible phoneme
// count (raw little-endian float32, no header): row `n - 1` is used for an
// n-phoneme utterance, clamped to the table's row count — see
// onnx-community/Kokoro-82M-v1.0-ONNX and thewh1teagle/kokoro-onnx's
// `_style_for`. The whole table is loaded here; the row is selected per
// synthesis request based on the (currently hardcoded) phoneme count.
std::vector<float> load_voice_style_table(const std::filesystem::path &voice_path) {
  std::ifstream stream(voice_path, std::ios::binary | std::ios::ate);
  if (!stream) {
    throw RunnerProtocolError("kokoro-onnx runner could not open voice file: " + voice_path.string());
  }
  const auto byte_size = static_cast<std::size_t>(stream.tellg());
  if (byte_size == 0 || byte_size % (kKokoroStyleWidth * sizeof(float)) != 0) {
    throw RunnerProtocolError("kokoro-onnx runner voice file is not a multiple of " +
                              std::to_string(kKokoroStyleWidth) + " floats: " + voice_path.string());
  }
  stream.seekg(0);
  std::vector<float> table(byte_size / sizeof(float));
  stream.read(reinterpret_cast<char *>(table.data()), static_cast<std::streamsize>(byte_size));
  if (static_cast<std::size_t>(stream.gcount()) != byte_size) {
    throw RunnerProtocolError("kokoro-onnx runner failed to read voice file: " + voice_path.string());
  }
  return table;
}

std::vector<std::uint8_t> encode_pcm_s16le(const float *samples, std::size_t count) {
  std::vector<std::uint8_t> payload(count * 2);
  for (std::size_t index = 0; index < count; ++index) {
    const float clamped = std::clamp(samples[index], -1.0f, 1.0f);
    const auto sample = static_cast<std::int16_t>(std::lround(clamped * 32767.0f));
    payload[index * 2] = static_cast<std::uint8_t>(static_cast<std::uint16_t>(sample) & 0xff);
    payload[index * 2 + 1] = static_cast<std::uint8_t>((static_cast<std::uint16_t>(sample) >> 8) & 0xff);
  }
  return payload;
}

}  // namespace

KokoroOnnxRunner::KokoroOnnxRunner()
    : env_(ORT_LOGGING_LEVEL_WARNING, "tts-host-kokoro-onnx-runner") {}

nlohmann::json KokoroOnnxRunner::handle_control_message(const nlohmann::json &message) {
  const auto request = parse_runner_initialize_request(message);
  if (request.protocol_version != kRunnerProtocolVersion) {
    throw RunnerProtocolError("kokoro-onnx runner does not support protocol version " +
                              std::to_string(request.protocol_version));
  }
  return make_runner_initialize_response(request,
                                         {"load", "unload", "synthesize", "cancel", "stats"});
}

nlohmann::json KokoroOnnxRunner::handle_load_message(const nlohmann::json &message) {
  const auto request = parse_runner_load_request(message);
  session_.emplace(open_session(env_, request.model_path));
  voice_style_.clear();
  if (request.voice_path.has_value()) {
    voice_style_ = load_voice_style_table(*request.voice_path);
  }
  return make_runner_load_response(request);
}

RunnerAudioFrame KokoroOnnxRunner::run_synthesis(std::string_view text) {
  if (!session_.has_value()) {
    throw RunnerProtocolError("kokoro-onnx runner received synthesize before load");
  }

  // The placeholder identity model (single "input"/"output") and the real
  // Kokoro-82M model (three named inputs) are told apart by input count,
  // so the same runner binary serves both the fast CTest fixture and real
  // weights without a protocol change.
  if (session_->GetInputCount() == 1) {
    return run_placeholder_identity_synthesis();
  }
  return run_kokoro_synthesis(text);
}

RunnerAudioFrame KokoroOnnxRunner::run_placeholder_identity_synthesis() {
  // Running the placeholder identity model here (rather than returning
  // synthetic audio like the stub runner) proves the ONNX Runtime session
  // executes end to end inside the real runner process.
  std::array<float, 1> input_values{42.0f};
  std::array<std::int64_t, 1> input_shape{1};
  Ort::MemoryInfo memory_info = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
  Ort::Value input_tensor = Ort::Value::CreateTensor<float>(
      memory_info, input_values.data(), input_values.size(), input_shape.data(), input_shape.size());

  constexpr const char *input_names[] = {"input"};
  constexpr const char *output_names[] = {"output"};
  auto outputs =
      session_->Run(Ort::RunOptions{nullptr}, input_names, &input_tensor, 1, output_names, 1);

  const float output_value = *outputs.front().GetTensorMutableData<float>();
  if (output_value != input_values[0]) {
    throw RunnerProtocolError(
        "kokoro-onnx runner inference did not return the expected identity output");
  }

  return {.sequence_number = 0,
          .sample_count = 4,
          .flags = kRunnerAudioFrameFlagEndOfStream,
          .payload = {0x00, 0x00, 0x00, 0x10, 0x00, 0xf0, 0x00, 0x00}};
}

RunnerAudioFrame KokoroOnnxRunner::run_kokoro_synthesis(std::string_view text) {
  if (voice_style_.empty()) {
    throw RunnerProtocolError("kokoro-onnx runner received synthesize without a loaded voice");
  }

  // Capture arbitrary-text IPA through the isolated espeak-ng process rather
  // than linking GPL-licensed code into this Apache-2.0 runner (ADR 0005),
  // then translate it to Kokoro's sparse phoneme vocabulary (ADR 0006).
  const auto ipa = phonemize_with_espeak_ng(default_espeak_ng_executable(), "en-us", text);
  const auto phoneme_ids = translate_ipa_to_kokoro_token_ids(ipa);
  if (phoneme_ids.empty()) {
    throw RunnerProtocolError("kokoro-onnx runner produced no phonemes for the requested text");
  }

  // Padded with the model's id-0 token at both ends per its input contract
  // (see the previously hardcoded fixture this replaced).
  std::vector<std::int64_t> input_ids;
  input_ids.reserve(phoneme_ids.size() + 2);
  input_ids.push_back(0);
  input_ids.insert(input_ids.end(), phoneme_ids.begin(), phoneme_ids.end());
  input_ids.push_back(0);
  const std::size_t phoneme_count = phoneme_ids.size();
  std::array<std::int64_t, 2> input_ids_shape{1, static_cast<std::int64_t>(input_ids.size())};

  const std::size_t style_row_count = voice_style_.size() / kKokoroStyleWidth;
  const std::size_t style_row_index = std::min(phoneme_count, style_row_count) - 1;
  float *style_row = voice_style_.data() + style_row_index * kKokoroStyleWidth;
  std::array<std::int64_t, 2> style_shape{1, static_cast<std::int64_t>(kKokoroStyleWidth)};

  std::array<float, 1> speed{1.0f};
  std::array<std::int64_t, 1> speed_shape{1};

  Ort::MemoryInfo memory_info = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
  Ort::AllocatorWithDefaultOptions allocator;

  // Input names are resolved from the loaded graph rather than hardcoded:
  // published Kokoro ONNX exports vary the phoneme-tensor name between
  // "input_ids" and "tokens" depending on conversion tooling/version.
  std::vector<Ort::AllocatedStringPtr> input_name_holders;
  std::vector<const char *> input_names;
  std::vector<Ort::Value> inputs;
  for (std::size_t index = 0; index < session_->GetInputCount(); ++index) {
    input_name_holders.push_back(session_->GetInputNameAllocated(index, allocator));
    const std::string name = input_name_holders.back().get();
    input_names.push_back(input_name_holders.back().get());
    if (name == "style") {
      inputs.push_back(Ort::Value::CreateTensor<float>(memory_info, style_row, kKokoroStyleWidth,
                                                        style_shape.data(), style_shape.size()));
    } else if (name == "speed") {
      inputs.push_back(Ort::Value::CreateTensor<float>(memory_info, speed.data(), speed.size(),
                                                        speed_shape.data(), speed_shape.size()));
    } else {
      inputs.push_back(Ort::Value::CreateTensor<std::int64_t>(
          memory_info, input_ids.data(), input_ids.size(), input_ids_shape.data(), input_ids_shape.size()));
    }
  }

  std::vector<Ort::AllocatedStringPtr> output_name_holders;
  std::vector<const char *> output_names;
  for (std::size_t index = 0; index < session_->GetOutputCount(); ++index) {
    output_name_holders.push_back(session_->GetOutputNameAllocated(index, allocator));
    output_names.push_back(output_name_holders.back().get());
  }

  auto outputs = session_->Run(Ort::RunOptions{nullptr}, input_names.data(), inputs.data(), inputs.size(),
                               output_names.data(), output_names.size());

  // Kokoro exports return the audio waveform as the first output (a second
  // "duration"/timing output may follow depending on export variant).
  const auto element_count = outputs.front().GetTensorTypeAndShapeInfo().GetElementCount();
  const float *samples = outputs.front().GetTensorData<float>();

  return {.sequence_number = 0,
          .sample_count = static_cast<std::uint32_t>(element_count),
          .flags = kRunnerAudioFrameFlagEndOfStream,
          .payload = encode_pcm_s16le(samples, element_count)};
}

nlohmann::json KokoroOnnxRunner::make_synthesize_response(const nlohmann::json &message,
                                                          const RunnerAudioFrame &frame) {
  const auto request = parse_runner_synthesize_request(message);
  return make_runner_synthesize_response(request, 24000, 1, "pcm_s16le", frame.sample_count);
}

}  // namespace tts_host
