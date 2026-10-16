#include "tts_host/recovering_runner.hpp"
#include "tts_host/path_encoding.hpp"
#include "tts_host/wav_writer.hpp"
#include <chrono>
#include <cmath>
#include <cstdint>
#include <string>
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
    // Speed is a pitch-preserving stretch of the generated audio. Generation
    // length itself varies between runs even with a fixed seed (about 5-20%),
    // so only the scale is checked here; time_stretch_tests pins exact lengths.
    const std::string sentence = "Speech speed changes the reading pace.";
    const auto normal = runner.synthesize(sentence, 1.0).format.total_sample_frames;
    uint64_t previous = UINT64_MAX;
    for (const double speed : {0.5, 1.5, 2.0}) {
      const auto frames = runner.synthesize(sentence, speed).format.total_sample_frames;
      const auto expected = static_cast<double>(normal) / speed;
      std::cout << "speed " << speed << ": " << frames << " frames, expected about " << expected << '\n';
      require(frames < previous && std::abs(static_cast<double>(frames) - expected) < 0.3 * expected,
              "Qwen speed did not scale the synthesized duration");
      previous = frames;
    }
    require(restarts == 0, "speed changes restarted the runner");
    // A prompt beyond the context is a request rejection: no restart or reload.
    std::string too_long;
    while (too_long.size() < 20000) too_long += "This sentence keeps the prompt growing. ";
    bool rejected = false;
    try { runner.synthesize(too_long, 1.0); }
    catch (const tts_host::RunnerRequestError &error) {
      rejected = std::string(error.what()).find("context") != std::string::npos;
    }
    require(rejected && restarts == 0 && runner.loaded(),
            "an over-context prompt restarted the runner or lost its reason");
    require(!runner.synthesize("Speech continues after a rejected request.", 1.0).pcm.empty(),
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
    std::cout << "Resident reuse, speed, rejection, unload/reload, cancellation and subsequent synthesis passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
