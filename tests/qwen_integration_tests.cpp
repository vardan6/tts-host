#include "tts_host/recovering_runner.hpp"
#include "tts_host/path_encoding.hpp"
#include "tts_host/wav_writer.hpp"
#include <chrono>
#include <iostream>
#include <stdexcept>

using namespace std::chrono_literals;
namespace {
void require(bool condition, const char *message) {
  if (!condition) throw std::runtime_error(message);
}
}
// Opt-in real-model check, deliberately outside the default GPU-free CTest suite.
int main(int argc, char **argv) {
  try {
    require(argc == 4, "usage: qwen-integration-tests <runner> <model.json> <out.wav>");
    const auto descriptor = tts_host::path_from_utf8(argv[2]);
    int restarts = 0;
    tts_host::RecoveringRunner runner(tts_host::path_from_utf8(argv[1]), descriptor, {},
        {30s, 120s, 120s, 250ms}, {}, [&](const std::string &) { ++restarts; });
    runner.prepare();
    for (int i = 0; i != 2; ++i) {
      const auto start = std::chrono::steady_clock::now();
      const auto chunk = runner.synthesize("The morning light reaches the quiet garden.", 1.0);
      const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
      const auto stats = runner.stats();
      require(chunk.format.sample_rate_hz == 24000 && chunk.format.channels == 1 &&
          chunk.format.sample_format == "pcm_s16le" && !chunk.pcm.empty() &&
          chunk.pcm.size() == chunk.format.total_sample_frames * 2,
          "invalid Qwen PCM or final sample count");
      require(stats.sample_count == chunk.format.total_sample_frames &&
          stats.time_to_first_chunk_ms > 0 && stats.time_to_first_chunk_ms < elapsed * 1000,
          "Qwen first-chunk statistics do not reflect incremental output");
      require(stats.vram_measurement == "unavailable", "Qwen memory must not be reported as measured zero");
      tts_host::write_wav_file(tts_host::path_from_utf8(argv[3]),
          chunk.format.sample_rate_hz, static_cast<uint16_t>(chunk.format.channels), 16, chunk.pcm);
      std::cout << "synthesis " << i + 1 << ": " << chunk.format.total_sample_frames
                << " frames, " << elapsed << " s total, " << stats.time_to_first_chunk_ms
                << " ms to first PCM\n";
    }
    require(restarts == 0, "normal synthesis unexpectedly restarted the runner");
    // Unsupported speed is a request rejection: no restart or model reload.
    bool rejected = false;
    try { runner.synthesize("This speed is not supported yet.", 1.5); }
    catch (const tts_host::RunnerRequestError &error) {
      rejected = std::string(error.what()).find("speed 1.0") != std::string::npos;
    }
    require(rejected && restarts == 0 && runner.loaded(),
            "unsupported speed restarted the runner or lost its reason");
    require(!runner.synthesize("Speech continues at normal speed.", 1.0).pcm.empty(),
            "Qwen could not synthesize after a rejected request");
    runner.unload();
    require(!runner.loaded(), "unload left the session loaded");
    runner.prepare();
    const auto cancel_at = std::chrono::steady_clock::now() + 500ms;
    runner.set_cancelled([&] { return std::chrono::steady_clock::now() >= cancel_at; });
    bool cancelled = false;
    const auto cancel_start = std::chrono::steady_clock::now();
    try { runner.synthesize("This long sentence should be cancelled before it finishes speaking.", 1.0); }
    catch (const tts_host::RunnerCancelled &) { cancelled = true; }
    require(cancelled && !runner.loaded() && restarts == 0,
            "explicit cancellation must discard the session without retry");
    require(std::chrono::steady_clock::now() - cancel_start < 3s, "Qwen cancellation exceeded its bound");
    runner.set_cancelled({});
    require(!runner.synthesize("Reading works again after cancellation.", 1.0).pcm.empty(),
            "Qwen could not restart after cancellation");
    runner.unload();
    std::cout << "Resident reuse, unload/reload, cancellation and subsequent synthesis passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
