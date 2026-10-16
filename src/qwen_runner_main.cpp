#include "qwen_api.hpp"
#include "tts_host/model_registry.hpp"
#include "tts_host/process_stats.hpp"
#include "tts_host/runner_audio_output.hpp"
#include "tts_host/runner_control_io.hpp"
#include "tts_host/runner_protocol.hpp"
#include "tts_host/time_stretch.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <memory>
#include <thread>

namespace tts_host {
namespace {
// Rejects one request; the loaded model and generator stay usable (ADR 0002).
class QwenRequestRejected : public RunnerProtocolError {
 public:
  using RunnerProtocolError::RunnerProtocolError;
};

// Models never supply executable paths or code. This directory is selected by
// the developer at build time; release-layout decisions are a separate slice.
class QwenRunner {
 public:
  QwenRunner() : api_(path_from_utf8(TTS_QWEN_RUNTIME_DIR)) {}
  ~QwenRunner() { unload(); }
  void unload() {
    if (generator_) api_.mtmd_helper_gen_audio_free(generator_);
    if (reference_) api_.mtmd_bitmap_free(reference_);
    if (mtmd_) api_.mtmd_free(mtmd_);
    if (context_) api_.llama_free(context_);
    if (model_) api_.llama_model_free(model_);
    reference_ = nullptr; generator_ = nullptr; mtmd_ = nullptr;
    context_ = nullptr; model_ = nullptr;
  }
  void load(const RunnerLoadRequest &request) {
    const auto descriptor = path_from_utf8(request.model_path);
    if (descriptor.filename() != "model.json")
      throw RunnerProtocolError("qwen3-gguf modelPath must name the package model.json");
    const auto package = validate_model_package(descriptor.parent_path());
    if (package.engine != "qwen3-gguf") throw RunnerProtocolError("Not a qwen3-gguf package");
    const auto asset = [&](const char *name) {
      return std::filesystem::weakly_canonical(package.package_path /
          path_from_utf8(package.manifest.at("files").at(name).get<std::string>()));
    };
    const auto voice = asset("voice");
    if (request.voice_path && std::filesystem::weakly_canonical(path_from_utf8(*request.voice_path)) != voice)
      throw RunnerProtocolError("Qwen voice must be the reference declared in the package");
    // One installed reference voice in this evaluation slice. The package's
    // primary language selects the synthesis language explicitly.
    language_ = package.languages.front();
    if (language_ != "en" && language_ != "ru")
      throw RunnerProtocolError("Qwen evaluation package primary language must be en or ru");
    const auto voice_size = std::filesystem::file_size(voice);
    if (voice_size > 32 * 1024 * 1024) throw RunnerProtocolError("Qwen reference exceeds 32 MiB");
    std::ifstream input(voice, std::ios::binary);
    std::vector<unsigned char> reference(static_cast<size_t>(voice_size));
    if (!input.read(reinterpret_cast<char *>(reference.data()), static_cast<std::streamsize>(reference.size())))
      throw RunnerProtocolError("Cannot read Qwen speaker reference");
    unload();
    auto mp = api_.llama_model_default_params();
    mp.n_gpu_layers = 99;
    const auto weights = path_to_utf8(asset("model"));
    model_ = api_.llama_model_load_from_file(weights.c_str(), mp);
    if (!model_) throw RunnerProtocolError("Cannot load Qwen backbone; check GGUF and available memory");
    auto cp = api_.llama_context_default_params();
    cp.embeddings = true;
    cp.n_ctx = 2048; cp.n_batch = 512; cp.n_ubatch = 512;
    cp.n_threads = 4; cp.n_threads_batch = 4;
    context_ = api_.llama_init_from_model(model_, cp);
    if (!context_) throw RunnerProtocolError("Cannot create Qwen inference context");
    auto xp = api_.mtmd_context_params_default();
    xp.use_gpu = true; xp.n_threads = 4;
    const auto projection = path_to_utf8(asset("mmproj"));
    mtmd_ = api_.mtmd_init_from_file(projection.c_str(), model_, xp);
    if (!mtmd_) throw RunnerProtocolError("Cannot load Qwen mmproj; check matching package assets");
    const auto info = api_.mtmd_gen_audio_get_info(mtmd_);
    if (info.type != MTMD_GEN_AUDIO_TYPE_QWEN3TTS || info.sample_rate <= 0)
      throw RunnerProtocolError("Package mmproj is not Qwen3-TTS");
    sample_rate_ = static_cast<std::uint32_t>(info.sample_rate);
    auto wrapper = api_.mtmd_helper_bitmap_init_from_buf(mtmd_, reference.data(), reference.size(),
        false, api_.mtmd_helper_init_opt_default());
    reference_ = wrapper.bitmap;
    if (!reference_ || wrapper.video_ctx) throw RunnerProtocolError("Cannot decode Qwen audio reference");
    generator_ = api_.mtmd_helper_gen_audio_init(context_, mtmd_);
    if (!generator_) throw RunnerProtocolError("Cannot create Qwen audio generator");
  }
  void synthesize(const RunnerSynthesizeRequest &request) {
    if (!generator_) throw RunnerProtocolError("Qwen synthesize requires a loaded package");
    const auto output = RunnerAudioOutput::open_inherited();
    std::uint64_t sequence = 0;
    try {
      generate(request, output, sequence);
    } catch (const QwenRequestRejected &error) {
      // Close the stream before replying so the Host can discard any partial
      // PCM and keep the resident model instead of restarting it.
      last_first_ms_ = 0; last_samples_ = 0;
      output.write_frame({sequence, 0, kRunnerAudioFrameFlagEndOfStream, {}});
      write_stdout(frame_runner_control_message(
          make_runner_synthesize_error_response(request, error.what())));
      return;
    }
    output.write_frame({sequence, 0, kRunnerAudioFrameFlagEndOfStream, {}});
    write_stdout(frame_runner_control_message(make_runner_synthesize_response(
        request, sample_rate_, 1, "pcm_s16le", last_samples_)));
  }
  void generate(const RunnerSynthesizeRequest &request, const RunnerAudioOutput &output,
                std::uint64_t &sequence) {
    const auto start = std::chrono::steady_clock::now();
    last_first_ms_ = 0; last_samples_ = 0;
    api_.llama_memory_clear(api_.llama_get_memory(context_), true);
    // Sampler history and generator state are per request; weights stay resident.
    auto *sampler = api_.llama_sampler_chain_init(api_.llama_sampler_chain_default_params());
    auto release = [&](llama_sampler *p) { api_.llama_sampler_free(p); };
    std::unique_ptr<llama_sampler, decltype(release)> guard(sampler, release);
    api_.llama_sampler_chain_add(sampler, api_.llama_sampler_init_top_k(50));
    api_.llama_sampler_chain_add(sampler, api_.llama_sampler_init_top_p(1.0f, 1));
    api_.llama_sampler_chain_add(sampler, api_.llama_sampler_init_temp(0.9f));
    api_.llama_sampler_chain_add(sampler, api_.llama_sampler_init_dist(42));
    mtmd_helper_gen_audio_inp in{};
    in.prompt = request.text.c_str(); in.prompt_len = request.text.size();
    in.lang = language_.c_str(); in.speaker_ref = reference_;
    in.top_k = 50; in.top_p = 1.0f; in.seed = 42;
    in.out_type = MTMD_HELPER_GEN_AUDIO_OUTTYPE_PCM;
    if (api_.mtmd_helper_gen_audio_set_input(generator_, &in))
      throw RunnerProtocolError("Qwen prompt setup failed");
    int remaining;
    do {
      remaining = api_.mtmd_helper_gen_audio_step_prompt(generator_, 512);
      if (remaining < 0) throw QwenRequestRejected("Qwen prompt processing failed; text may exceed context");
    } while (remaining);
    // Qwen has no speed input, so its PCM is time-stretched without changing
    // pitch; the stretcher streams, so first-PCM latency is kept.
    TimeStretcher stretcher(sample_rate_, request.speed);
    uint64_t generated = 0;
    const auto emit = [&](const std::vector<float> &pcm) {
      for (size_t offset = 0; offset < pcm.size();) {
        const auto count = std::min<size_t>(pcm.size() - offset, 24000);
        RunnerAudioFrame audio{sequence++, static_cast<uint32_t>(count), 0, {}};
        audio.payload.reserve(count * 2);
        for (size_t i = 0; i < count; ++i) {
          const auto sample = static_cast<uint16_t>(static_cast<int16_t>(
              std::lround(std::clamp(pcm[offset + i], -1.0f, 1.0f) * 32767.0f)));
          audio.payload.push_back(static_cast<uint8_t>(sample & 255));
          audio.payload.push_back(static_cast<uint8_t>(sample >> 8));
        }
        if (!last_samples_) last_first_ms_ = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - start).count();
        output.write_frame(audio);
        last_samples_ += count;
        offset += count;
      }
    };
    const float *state = api_.llama_get_embeddings_ith(context_, -1);
    bool stop = false;
    for (int frame = 0; frame < 1024 && !stop; ++frame) {
      const auto token = api_.llama_sampler_sample(sampler, context_, -1);
      const float *next = nullptr;
      if (api_.mtmd_helper_gen_audio_step_gen(generator_, token, state, &next, &stop))
        throw RunnerProtocolError("Qwen audio generation failed");
      state = next;
      if (!state && !stop) throw RunnerProtocolError("Qwen stopped without end-of-speech");
      if ((frame + 1) % 12 == 0 || stop) {
        int32_t rate = 0;
        const char *data = nullptr;
        size_t bytes = 0;
        int64_t samples = 0;
        if (api_.mtmd_helper_gen_audio_get_output(generator_, &rate, &data, &bytes, &samples) ||
            samples < 0 || static_cast<uint64_t>(samples) < generated ||
            bytes != static_cast<size_t>(samples) * sizeof(float) || rate != static_cast<int32_t>(sample_rate_))
          throw RunnerProtocolError("Qwen returned invalid PCM output");
        const auto *pcm = reinterpret_cast<const float *>(data);
        for (auto i = generated; i < static_cast<uint64_t>(samples); ++i)
          if (!std::isfinite(pcm[i])) throw RunnerProtocolError("Qwen returned non-finite audio");
        emit(stretcher.push(pcm + generated, static_cast<size_t>(static_cast<uint64_t>(samples) - generated)));
        generated = static_cast<uint64_t>(samples);
      }
    }
    if (!stop) throw QwenRequestRejected("Qwen reached generation limit without end-of-speech; shorten the text");
    emit(stretcher.finish());
    if (!last_samples_) throw QwenRequestRejected("Qwen generated no audio");
  }
  nlohmann::json stats(const RunnerStatsRequest &request) const {
    auto result = make_runner_stats_response(request, peak_resident_set_size_bytes(), 0,
        last_first_ms_, last_samples_);
    // No reliable process peak VRAM API on Windows WDDM. Never present the
    // placeholder zero as a measurement or a memory-budget pass.
    result["result"]["vramMeasurement"] = "unavailable";
    return result;
  }
 private:
  QwenApi api_;
  llama_model *model_{};
  llama_context *context_{};
  mtmd_context *mtmd_{};
  mtmd_bitmap *reference_{};
  mtmd_helper_gen_audio *generator_{};
  std::string language_ = "en";
  uint32_t sample_rate_ = 24000;
  double last_first_ms_ = 0;
  uint64_t last_samples_ = 0;
};
}  // namespace
}  // namespace tts_host

int main() {
  try {
    // Lazy construction makes initialize and package errors available even
    // when the evaluation runtime is not installed correctly.
    std::unique_ptr<tts_host::QwenRunner> runner;
    tts_host::RunnerControlMessageParser parser;
    for (;;) {
      const auto bytes = tts_host::read_some_stdin();
      if (bytes.empty()) return EXIT_SUCCESS;
      for (const auto &message : parser.push(std::string_view(
          reinterpret_cast<const char *>(bytes.data()), bytes.size()))) {
        const auto method = message.value("method", "");
        nlohmann::json response;
        if (method == "initialize") {
          const auto request = tts_host::parse_runner_initialize_request(message);
          if (request.protocol_version != tts_host::kRunnerProtocolVersion)
            throw tts_host::RunnerProtocolError("Unsupported Qwen runner protocol version");
          response = tts_host::make_runner_initialize_response(request,
              {"load", "unload", "synthesize", "stats"});
        } else if (method == "load") {
          const auto request = tts_host::parse_runner_load_request(message);
          if (!runner) runner = std::make_unique<tts_host::QwenRunner>();
          runner->load(request);
          response = tts_host::make_runner_load_response(request);
        } else if (method == "unload") {
          const auto request = tts_host::parse_runner_unload_request(message);
          if (runner) runner->unload();
          response = tts_host::make_runner_unload_response(request);
        } else if (method == "synthesize") {
          if (!runner) throw tts_host::RunnerProtocolError("Qwen synthesize before load");
          runner->synthesize(tts_host::parse_runner_synthesize_request(message));
          continue;
        } else if (method == "stats") {
          if (!runner) throw tts_host::RunnerProtocolError("Qwen stats before load");
          response = runner->stats(tts_host::parse_runner_stats_request(message));
        } else throw tts_host::RunnerProtocolError("Unsupported Qwen runner method: " + method);
        tts_host::write_stdout(tts_host::frame_runner_control_message(response));
      }
    }
  } catch (const std::exception &error) {
    std::cerr << "qwen3-gguf: " << error.what() << '\n';
    return EXIT_FAILURE;
  }
}
