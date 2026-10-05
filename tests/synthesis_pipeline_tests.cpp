#include "tts_host/synthesis_pipeline.hpp"
#include <chrono>
#include <cstdlib>
#include <future>
#include <iostream>
#include <thread>

using namespace std::chrono_literals;
void require(bool condition, const char *message) {
  if (!condition) throw std::runtime_error(message);
}
void env(const char *name, const std::string &value) {
#ifdef _WIN32
  _putenv_s(name, value.c_str());
#else
  setenv(name, value.c_str(), 1);
#endif
}
class Endpoint final : public tts_host::PlaybackSink {
 public:
  std::promise<void> started;
  std::promise<std::uint64_t> replayed;
  std::atomic_int calls{0};
  void play(std::uint32_t, std::uint16_t, const std::vector<std::uint8_t> &,
            const std::string &, tts_host::PlaybackControl *) override {}
  bool play_from(std::uint32_t rate, std::uint16_t, const std::vector<std::uint8_t> &pcm,
                 const std::string &, std::uint64_t frame, double origin,
                 tts_host::PlaybackControl *control, std::uint64_t generation) override {
    require(rate == 24000 && pcm.size() == 8, "pipeline changed source PCM");
    if (calls.fetch_add(1) == 0) {
      control->update_position(origin + 3.0 / rate);
      started.set_value();
    } else replayed.set_value(frame);
    while (!control->cancelled() && control->seek_generation() == generation)
      std::this_thread::sleep_for(1ms);
    return control->seek_generation() != generation;
  }
};
int main(int argc, char **argv) {
  try {
    require(argc == 3, "expected runner and scratch directory");
    const auto gate = std::filesystem::path(argv[2]) / "lookahead-gate";
    const auto ready = std::filesystem::path(gate.string() + ".ready");
    env("TTS_TEST_RUNNER_MARKER", gate.string());
    for (bool paused : {false, true}) {
      env("TTS_TEST_RUNNER_MODE", "gate-lookahead");
      std::filesystem::remove(gate);
      std::filesystem::remove(ready);
      Endpoint endpoint;
      auto started = endpoint.started.get_future();
      auto replayed = endpoint.replayed.get_future();
      tts_host::PlaybackController controller;
      auto control = controller.begin();
      tts_host::ConfigDocument config;
      config.value = {{"audio", {{"outputDevice", "system-default"}}}};
      tts_host::CliOptions options;
      options.play_audio = true;
      auto pipeline = std::async(std::launch::async, [&] {
        try {
          tts_host::run_synthesis_pipeline("First. Second.", options, config,
              {argv[1], {}, {}}, control.get(), &controller, &endpoint,
              {5s, 5s, 5s, 100ms});
        } catch (const tts_host::RunnerCancelled &) { return true; }
        return false;
      });
      // Always cancel/join on an assertion failure too.
      struct Cleanup {
        std::shared_ptr<tts_host::PlaybackControl> control;
        ~Cleanup() { control->cancel(); }
      } cleanup{control};
      require(started.wait_for(2s) == std::future_status::ready, "first chunk did not play");
      const auto deadline = std::chrono::steady_clock::now() + 2s;
      while (!std::filesystem::exists(ready) && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(5ms);
      require(std::filesystem::exists(ready), "lookahead runner was not blocked");
      if (paused) controller.pause();
      require(control->request_seek(1.0 / 24000), "retained seek refused");
      require(replayed.wait_for(500ms) == std::future_status::ready,
              "retained replay waited for blocked lookahead synthesis");
      require(replayed.get() == 1, "seek did not use retained source frame");
      require(control->paused() == paused, "seek changed pause state");
      require(!std::filesystem::exists(gate), "runner completed before seek verification");
      control->cancel();
      require(pipeline.wait_for(2s) == std::future_status::ready && pipeline.get(),
              "Stop/Quit did not join synthesis and playback");
      std::filesystem::remove(ready);
    }
    // A repeated runner failure while PCM is playing must cancel and join
    // the endpoint before unwinding (a joinable std::thread must not terminate
    // the Host). The failed sentence is never published to the sink.
    env("TTS_TEST_RUNNER_MODE", "death");
    Endpoint endpoint;
    tts_host::PlaybackController controller;
    auto control = controller.begin();
    tts_host::ConfigDocument config;
    config.value = {{"audio", {{"outputDevice", "system-default"}}}};
    tts_host::CliOptions options;
    options.play_audio = true;
    bool failed = false;
    try {
      tts_host::run_synthesis_pipeline("First. fail.", options, config, {argv[1], {}, {}},
          control.get(), &controller, &endpoint, {500ms, 500ms, 500ms, 100ms});
    } catch (const tts_host::RunnerLaunchError &error) {
      failed = std::string(error.what()).find("one restart") != std::string::npos;
    }
    require(failed && control->cancelled() && endpoint.calls == 1,
            "runner failure did not join playback or replayed a failed chunk");
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
