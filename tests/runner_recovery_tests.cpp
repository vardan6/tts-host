#include "tts_host/recovering_runner.hpp"
#include "tts_host/stub_runner.hpp"
#include <atomic>
#include <cstdlib>
#include <fstream>
#include <future>
#include <iostream>
#include <thread>

using namespace std::chrono_literals;
void env(const char *key, const std::string &value) {
#ifdef _WIN32
  _putenv_s(key, value.c_str());
#else
  if (value.empty()) unsetenv(key); else setenv(key, value.c_str(), 1);
#endif
}
void require(bool condition, const char *message) {
  if (!condition) throw std::runtime_error(message);
}
int main(int argc, char **argv) {
  try {
    require(argc == 3, "expected fixture and scratch directory");
    const auto executable = std::filesystem::path(argv[1]);
    const auto marker = std::filesystem::path(argv[2]) / "runner-recovery-marker";
    env("TTS_TEST_RUNNER_MARKER", marker.string());
    const tts_host::RunnerDeadlines deadlines{150ms, 150ms, 150ms, 100ms};
    for (const auto *mode : {"death-once", "partial-once", "hang-once", "bad-reply-once"}) {
      env("TTS_TEST_RUNNER_MODE", mode);
      env("TTS_TEST_REQUIRE_LOAD", "1");
      std::filesystem::remove(marker);
      int restarts = 0;
      tts_host::RecoveringRunner runner(executable, std::filesystem::path("model"), {}, deadlines,
          {}, [&](const std::string &message) {
            require(message.find("retrying") != std::string::npos, "restart reason missing");
            ++restarts;
          });
      const auto first = runner.synthesize("already audible", 1.0);
      const auto recovered = runner.synthesize("fail", 1.0);
      const auto last = runner.synthesize("next sentence", 1.0);
      const auto expected = tts_host::make_stub_runner_synthesis_frame().payload;
      require(first.pcm == expected && recovered.pcm == expected && last.pcm == expected,
              "retry duplicated or lost PCM");
      require(restarts == 1, "failed chunk did not restart exactly once");
      require(runner.finish() == 0, "recovered process failed");
    }
    // A start that fails before initialize or load replies, or replies with
    // fields that fail validation, restarts once in prepare(); that restart is
    // the first chunk's, so a later failure of the chunk is final. Every start
    // failing is actionable after one restart.
    for (const std::string mode : {"init-death-once", "load-death-once", "init-death", "load-death",
                                   "bad-initialize-once", "bad-load-once", "bad-initialize", "bad-load"}) {
      env("TTS_TEST_RUNNER_MODE", mode);
      std::filesystem::remove(marker);
      int restarts = 0;
      tts_host::RecoveringRunner runner(executable, std::filesystem::path("model"), {}, deadlines,
          {}, [&](const std::string &message) {
            require(message.find("retrying its start") != std::string::npos, "start restart reason missing");
            ++restarts;
          });
      if (mode.ends_with("-once")) {
        runner.prepare();
        require(runner.loaded() && restarts == 1, "a failed start did not restart exactly once");
        require(runner.synthesize("hello", 1.0).pcm == tts_host::make_stub_runner_synthesis_frame().payload,
                "the restarted runner did not synthesize");
        require(runner.finish() == 0, "restarted process failed");
      } else {
        bool failed = false;
        try { runner.prepare(); }
        catch (const tts_host::RunnerLaunchError &error) {
          failed = std::string(error.what()).find("choose another model") != std::string::npos;
        }
        require(failed && restarts == 1 && !runner.loaded(),
                "a repeatedly failing start must be actionable after exactly one restart");
      }
    }
    {
      // The start's restart spends the first chunk's: its failure is final.
      env("TTS_TEST_RUNNER_MODE", "load-death-once");
      std::filesystem::remove(marker);
      int restarts = 0;
      tts_host::RecoveringRunner runner(executable, std::filesystem::path("model"), {}, deadlines,
          {}, [&](const std::string &) { ++restarts; });
      runner.prepare();
      runner.finish();  // the closed session fails the first chunk
      bool failed = false;
      try { runner.synthesize("hello", 1.0); }
      catch (const tts_host::RunnerLaunchError &error) {
        failed = std::string(error.what()).find("after one restart") != std::string::npos;
      }
      require(failed && restarts == 1, "the first chunk got a second restart after the start's");
    }
    // A rejected load through prepare() is not retried either.
    {
      env("TTS_TEST_RUNNER_MODE", "normal");
      env("TTS_STUB_RUNNER_REJECT_LOAD", "no usable GPU");
      int restarts = 0;
      tts_host::RecoveringRunner runner(executable, std::filesystem::path("model"), {}, deadlines,
          {}, [&](const std::string &) { ++restarts; });
      bool rejected = false;
      try { runner.prepare(); }
      catch (const tts_host::RunnerLoadRejected &) { rejected = true; }
      env("TTS_STUB_RUNNER_REJECT_LOAD", "");
      require(rejected && restarts == 0 && !runner.loaded(), "prepare() retried a rejected load");
    }
    // A rejected request reports its reason without restarting the runner,
    // discards its partial PCM, and leaves the session usable.
    {
      env("TTS_TEST_RUNNER_MODE", "reject");
      env("TTS_TEST_REQUIRE_LOAD", "1");
      int restarts = 0;
      tts_host::RecoveringRunner runner(executable, std::filesystem::path("model"), {}, deadlines,
          {}, [&](const std::string &) { ++restarts; });
      bool rejected = false;
      try { runner.synthesize("reject this", 1.0); }
      catch (const tts_host::RunnerRequestError &error) {
        rejected = std::string(error.what()) == "fixture rejected the request";
      }
      require(rejected && restarts == 0 && runner.loaded(),
              "a rejected request restarted the runner or lost its reason");
      require(runner.synthesize("next sentence", 1.0).pcm ==
                  tts_host::make_stub_runner_synthesis_frame().payload,
              "rejected request's partial PCM leaked into the next sentence");
      require(restarts == 0 && runner.finish() == 0, "runner did not survive a rejection");
    }
    // A rejected load is reported once with the runner's reason: no restart,
    // no retry, and the unloaded runner is shut down (ADR 0002).
    {
      env("TTS_TEST_RUNNER_MODE", "normal");
      env("TTS_STUB_RUNNER_REJECT_LOAD", "no usable GPU");
      const auto lifecycle = std::filesystem::path(argv[2]) / "runner-load-rejection-lifecycle";
      std::filesystem::remove(lifecycle);
      env("TTS_TEST_RUNNER_LIFECYCLE", lifecycle.string());
      int restarts = 0;
      std::string reported;
      tts_host::RecoveringRunner runner(executable, std::filesystem::path("model"), {}, deadlines,
          {}, [&](const std::string &) { ++restarts; });
      runner.set_load_rejected_callback([&](const std::string &reason) { reported += reason + ";"; });
      for (int use = 0; use != 2; ++use) {
        bool rejected = false;
        try { runner.synthesize("hello", 1.0); }
        catch (const tts_host::RunnerLoadRejected &error) {
          rejected = std::string(error.what()) == "no usable GPU";
        }
        require(rejected && !runner.loaded(), "a rejected load was not reported as rejected");
      }
      env("TTS_STUB_RUNNER_REJECT_LOAD", "");
      env("TTS_TEST_RUNNER_LIFECYCLE", "");
      require(restarts == 0, "a rejected load restarted the runner");
      // Each explicit use tries once more: two uses, two runner starts.
      require(reported == "no usable GPU;no usable GPU;", "load rejection callback not called per use");
      std::ifstream events(lifecycle);
      int starts = 0;
      for (std::string event; std::getline(events, event);) starts += event == "start";
      require(starts == 2, "a rejected load was retried within one use");
      require(runner.synthesize("hello", 1.0).pcm == tts_host::make_stub_runner_synthesis_frame().payload,
              "runner did not load once the rejection cleared");
      require(runner.finish() == 0, "runner failed after a cleared rejection");
    }
    // More than a pipe buffer before the control reply must not deadlock.
    // A death during that stream must discard its PCM before the retry.
    for (const auto *mode : {"audio-first", "audio-first-partial-once"}) {
      env("TTS_TEST_RUNNER_MODE", mode);
      std::filesystem::remove(marker);
      int restarts = 0;
      tts_host::RecoveringRunner runner(executable, std::filesystem::path("model"), {},
          {2s, 2s, 2s, 100ms}, {}, [&](const std::string &) { ++restarts; });
      for (int i = 0; i != 2; ++i) {
        const auto chunk = runner.synthesize("streamed sentence", 1.0);
        require(chunk.pcm == std::vector<std::uint8_t>(256 * 1024, 0x10),
                "audio before reply was duplicated or lost");
        require(chunk.format.total_sample_frames == chunk.pcm.size() / 2,
                "streamed sample count is incorrect");
      }
      require(restarts == (std::string(mode) == "audio-first" ? 0 : 1),
              "streaming recovery restarted the wrong number of times");
    }
    env("TTS_TEST_REQUIRE_LOAD", "");
    for (const auto *mode : {"death", "hang", "audio-hang", "audio-first-hang", "load-hang"}) {
      env("TTS_TEST_RUNNER_MODE", mode);
      int restarts = 0;
      const auto model = std::string(mode) == "load-hang"
          ? std::optional<std::filesystem::path>("model") : std::nullopt;
      tts_host::RecoveringRunner runner(executable, model, {}, deadlines, {},
          [&](const std::string &) { ++restarts; });
      bool failed = false;
      try { runner.synthesize("fail", 1.0); }
      catch (const tts_host::RunnerLaunchError &error) {
        failed = std::string(error.what()).find("choose another model") != std::string::npos;
      }
      require(failed && restarts == 1, "repeated failure must be actionable after exactly one retry");
    }
    for (const auto *mode : {"silent", "blocked-write", "stuck-exit", "drip-control", "close-input"}) {
      env("TTS_TEST_RUNNER_MODE", mode);
      const auto start = std::chrono::steady_clock::now();
      {
        tts_host::RunnerSession runner(executable, deadlines);
        bool failed = false;
        if (std::string(mode) == "stuck-exit") {
          runner.send_request(tts_host::make_runner_initialize_request(1));
          require(runner.finish() != 0, "hung exit must be terminated");
        } else {
          if (std::string(mode) == "close-input") std::this_thread::sleep_for(100ms);
          auto request = tts_host::make_runner_initialize_request(1);
          if (std::string(mode) == "blocked-write") request["padding"] = std::string(1024 * 1024, 'x');
          try { runner.send_request(request); }
          catch (const tts_host::RunnerLaunchError &) { failed = true; }
          require(failed, "hung I/O did not time out");
        }
      }
      require(std::chrono::steady_clock::now() - start < 2s, "shutdown exceeded bound");
    }
    for (const auto *mode : {"hang", "audio-first-hang"}) {
    env("TTS_TEST_RUNNER_MODE", mode);
      std::atomic_bool cancelled{false};
      int restarts = 0;
      auto pending = std::async(std::launch::async, [&] {
        tts_host::RunnerDeadlines slow{5s, 5s, 5s, 100ms};
        tts_host::RecoveringRunner runner(executable, {}, {}, slow,
            [&] { return cancelled.load(); }, [&](const std::string &) { ++restarts; });
        try { runner.synthesize("fail", 1.0); }
        catch (const tts_host::RunnerCancelled &) { return true; }
        return false;
      });
      std::this_thread::sleep_for(100ms);
      cancelled = true;
      require(pending.wait_for(1s) == std::future_status::ready && pending.get(),
              "Stop/Quit did not cancel runner wait promptly");
      require(restarts == 0, "explicit cancellation retried");
    }
    std::filesystem::remove(marker);
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
