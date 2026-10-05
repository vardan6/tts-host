#include "tts_host/model_session.hpp"
#include "tts_host/synthesis_pipeline.hpp"
#include "tts_host/path_encoding.hpp"
#include <cstdlib>
#include <atomic>
#include <thread>
#include <fstream>
#include <future>
#include <iostream>
#include <stdexcept>
using namespace std::chrono_literals;
namespace {
void require(bool value, const char *message) {
  if (!value) throw std::runtime_error(message);
}
void env(const char *key, const std::string &value) {
#ifdef _WIN32
  _putenv_s(key, value.c_str());
#else
  setenv(key, value.c_str(), 1);
#endif
}
int count(const std::filesystem::path &path, const std::string &event) {
  std::ifstream input(path);
  std::string line;
  int total = 0;
  while (std::getline(input, line)) if (line == event) ++total;
  return total;
}
}
int main(int argc, char **argv) {
  try {
    require(argc == 4, "expected fixture runner, config, scratch directory");
    const auto scratch = std::filesystem::absolute(argv[3]);
    std::filesystem::remove_all(scratch);
    std::filesystem::create_directories(scratch);
    struct Cleanup {
      std::filesystem::path path;
      ~Cleanup() { std::error_code error; std::filesystem::remove_all(path, error); }
    } cleanup{scratch};
    const auto runner = tts_host::runner_path_for_engine("stub", scratch);
    std::filesystem::copy_file(argv[1], runner);
    const auto events = scratch / "events.txt";
    env("TTS_TEST_RUNNER_MODE", "normal");
    env("TTS_TEST_REQUIRE_LOAD", "1");
    env("TTS_TEST_RUNNER_LIFECYCLE", tts_host::path_to_utf8(events));
    tts_host::ConfigDocument config;
    config.paths.config_path = std::filesystem::absolute(argv[2]);
    std::ifstream input(config.paths.config_path);
    config.value = nlohmann::json::parse(input);
    config.value["profiles"]["fast"]["loadPolicy"] = "onDemand";
    const auto selection = tts_host::resolve_model_runner_selection("stub-package", config, scratch);
    const tts_host::RunnerSelection pipeline_selection{selection.runner_path, selection.model_path,
        selection.voice_path, selection.model_id, selection.display_name, selection.engine, {}};
    tts_host::CliOptions options;
    tts_host::ModelSessionManager manager(scratch);
    manager.load_eager(config);
    require(!manager.status().loaded && count(events, "start") == 0,
            "onDemand started a runner before speech");
    const auto speak = [&] {
      auto lease = manager.acquire(selection);
      tts_host::run_synthesis_pipeline("First. Second.", options, config, pipeline_selection,
                                      nullptr, nullptr, nullptr, {}, &lease.runner());
      require(manager.status().loaded, "onDemand synthesis hid its loaded model until utterance release");
    };
    speak();
    speak();
    require(count(events, "start") == 1 && count(events, "load") == 1 &&
                count(events, "synthesize") == 4,
            "sequential utterances did not reuse the process and loaded model");
    require(manager.status().loaded, "speech completion discarded residency");

    // Settings' explicit Load must be reused by the subsequent speech lease.
    manager.load("stub-package", config);
    const auto before_use = std::chrono::steady_clock::now();
    {
      auto lease = manager.acquire(selection);
      auto checks = std::async(std::launch::async, [&] {
        require(!manager.unload_if_idle(1s, before_use + 1h), "active speech was idle-unloaded");
        require(manager.status().loaded, "status blocked or lost the active resident model");
        try { manager.unload(); throw std::logic_error("unload accepted while speech was busy"); }
        catch (const std::runtime_error &error) {
          require(std::string(error.what()).find("in use") != std::string::npos,
                  "busy model unload did not explain how to proceed");
        }
      });
      require(checks.wait_for(1s) == std::future_status::ready, "UI operations blocked on speech");
      checks.get();
      lease.runner().synthesize("Settings loaded this", 1.0);
      require(manager.status().loaded, "active utterance hid its known loaded model");
    }
    require(count(events, "start") == 2 && count(events, "load") == 2,
            "Settings load was not reused by speech");
    require(!manager.unload_if_idle(60s, before_use + 60s), "speech did not reset idle activity");
    require(manager.unload_if_idle(60s, std::chrono::steady_clock::now() + 60s),
            "resident model survived its idle deadline");
    speak();
    require(count(events, "start") == 3, "onDemand did not reload after idle unload");
    manager.unload();

    config.value["profiles"]["fast"]["loadPolicy"] = "eager";
    config.value["profiles"]["fast"]["idleUnloadSeconds"] = 0;
    manager.load_eager(config);
    require(manager.status().loaded && count(events, "start") == 4,
            "eager profile did not load before the first read");
    manager.load_eager(config);
    require(count(events, "start") == 4, "unchanged eager profile restarted its resident runner");
    require(!manager.unload_if_idle(1s, std::chrono::steady_clock::now() + 1h),
            "profile idle override failed to disable unloading");
    {
      auto lease = manager.acquire(selection, {}, 0s);
      lease.runner().synthesize("Eager read", 1.0);
    }
    require(count(events, "start") == 4 && count(events, "load") == 4,
            "eager model was reloaded for speech");
    manager.unload();

    // Cancellation invalidates a session; the next read receives fresh I/O
    // and a fresh callback, rather than the previous utterance's token.
    {
      auto lease = manager.acquire(selection, [] { return true; });
      try { lease.runner().synthesize("cancelled", 1.0); throw std::logic_error("ignored cancellation"); }
      catch (const tts_host::RunnerCancelled &) {}
    }
    require(!manager.status().loaded, "cancelled session still appeared loaded");
    speak();
    require(manager.status().loaded && count(events, "start") == 5,
            "cancellation poisoned the next resident request");
    manager.unload();

    // A different model replaces the single resident slot; its profile's idle
    // policy must not leak into subsequent reads through another profile.
    auto alternate = selection;
    alternate.model_id = "alternate";
    alternate.model_path = scratch / "alternate.bin";
    { std::ofstream(alternate.model_path) << "model"; }
    {
      auto lease = manager.acquire(alternate, {}, 0s);
      lease.runner().synthesize("Alternate", 1.0);
    }
    require(manager.status().model_id == "alternate", "model switching left stale metadata");
    speak();
    require(manager.status().model_id == "stub-package", "original model did not replace alternate");
    require(manager.unload_if_idle(1s, std::chrono::steady_clock::now() + 1h),
            "disabled profile timeout leaked into a different profile");

    // Resident requests preserve the same restart/reload/one-retry contract.
    env("TTS_TEST_RUNNER_MODE", "death-once");
    env("TTS_TEST_RUNNER_MARKER", tts_host::path_to_utf8(scratch / "death-marker"));
    const auto starts = count(events, "start");
    {
      auto lease = manager.acquire(selection);
      require(lease.runner().synthesize("fail then recover", 1.0).pcm.size() == 8,
              "resident recovery did not publish a complete chunk");
    }
    require(count(events, "start") == starts + 2, "resident recovery did not restart exactly once");
    speak();
    require(count(events, "start") == starts + 2, "recovered runner was not reused by the next read");
    manager.unload();

    env("TTS_TEST_RUNNER_MODE", "load-hang");
    std::atomic_bool cancelled{false};
    auto warming = std::async(std::launch::async, [&] {
      try { manager.load_eager(config, [&] { return cancelled.load(); }); }
      catch (const tts_host::RunnerCancelled &) { return true; }
      return false;
    });
    std::this_thread::sleep_for(50ms);
    cancelled = true;
    require(warming.wait_for(2s) == std::future_status::ready && warming.get(),
            "Quit/replacement could not cancel eager model warming");
    require(!manager.status().loaded, "cancelled warm-up reported a loaded model");
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
