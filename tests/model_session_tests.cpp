#include "tts_host/model_session.hpp"

#include "tts_host/runner_protocol.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

#include <nlohmann/json.hpp>

namespace {

void require(bool condition, const std::string &message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

// The manager only needs the parsed config plus the path its registry
// directories are relative to, so this skips load_config's schema validation
// (covered by tests/config_loader_tests.cpp) and reads the fixture directly.
tts_host::ConfigDocument load_fixture_config(const std::filesystem::path &config_path) {
  std::ifstream input(config_path);
  require(static_cast<bool>(input), "unable to open " + config_path.string());
  tts_host::ConfigDocument document;
  document.value = nlohmann::json::parse(input);
  document.paths.config_path = config_path;
  return document;
}

void expect_load_error(tts_host::ModelSessionManager &sessions,
                       const tts_host::ConfigDocument &config, const std::string &model_id,
                       const std::string &expected_message) {
  try {
    sessions.load(model_id, config);
  } catch (const std::exception &error) {
    require(std::string(error.what()).find(expected_message) != std::string::npos,
            "loading '" + model_id + "' reported '" + error.what() + "', expected '" +
                expected_message + "'");
    return;
  }
  throw std::runtime_error("loading '" + model_id + "' unexpectedly succeeded");
}

}  // namespace

int main(int argc, char **argv) {
  try {
    require(argc == 3, "usage: tts-host-model-session-tests <runner-directory> <fixture-config>");
    const std::filesystem::path runner_directory(argv[1]);
    const auto config = load_fixture_config(std::filesystem::path(argv[2]));

    // The tts-host-<engine>-runner naming convention is what makes the
    // fixture's "stub" engine resolve to the stub runner built beside these
    // tests.
    const auto stub_runner = tts_host::runner_path_for_engine("stub", runner_directory);
    require(stub_runner.filename().stem() == "tts-host-stub-runner",
            "engine 'stub' resolved to unexpected runner " + stub_runner.string());

    const auto selection =
        tts_host::resolve_model_runner_selection("stub-package", config, runner_directory);
    require(selection.display_name == "Stub Package" && selection.engine == "stub",
            "resolved selection carries the wrong package metadata");
    require(selection.model_path.filename() == "model.bin",
            "resolved selection points at the wrong model file: " + selection.model_path.string());
    require(!selection.voice_path.has_value(),
            "a package with no voice file reported a voice path");

    // The wire `unload` against a real runner subprocess: the runner answers
    // it and stays alive, so the same process can load and synthesize again
    // afterwards rather than having to be respawned.
    {
      tts_host::RunnerSession session(stub_runner);
      tts_host::parse_runner_initialize_response(
          session.send_request(tts_host::make_runner_initialize_request(1)));
      tts_host::parse_runner_load_response(
          session.send_request(tts_host::make_runner_load_request(2, selection.model_path.string())));

      const auto unload_response =
          session.send_request(tts_host::make_runner_unload_request("unload-1"));
      require(tts_host::parse_runner_unload_response(unload_response).id == "unload-1",
              "stub runner subprocess did not answer the unload request");

      tts_host::parse_runner_load_response(
          session.send_request(tts_host::make_runner_load_request(4, selection.model_path.string())));
      const auto synthesize_response =
          session.send_request(tts_host::make_runner_synthesize_request(5, "after unload"));
      require(tts_host::parse_runner_synthesize_response(synthesize_response).total_sample_frames == 4,
              "the runner did not synthesize after being unloaded and loaded again");
      static_cast<void>(session.read_audio_stream_until_end());
      require(session.finish() == 0, "the runner exited with a failure after unload");
    }

    tts_host::ModelSessionManager sessions(runner_directory);
    require(!sessions.status().loaded, "a new manager reported a loaded model");

    sessions.load("stub-package", config);
    require(sessions.status().loaded && sessions.status().model_id == "stub-package" &&
                sessions.status().display_name == "Stub Package" &&
                sessions.status().engine == "stub",
            "loading did not report the loaded model");

    // Loading again is how the user switches models: the previous runner is
    // replaced, not accumulated.
    sessions.load("stub-package", config);
    require(sessions.status().loaded, "reloading the same model left nothing loaded");

    // Resolution failures leave the resident model alone.
    expect_load_error(sessions, config, "does-not-exist", "Unknown model id");
    require(sessions.status().loaded && sessions.status().model_id == "stub-package",
            "an unknown model id unloaded the resident model");
    expect_load_error(sessions, config, "no-runner-package", "No runner installed for engine");
    require(sessions.status().loaded && sessions.status().model_id == "stub-package",
            "a missing engine runner unloaded the resident model");

    sessions.unload();
    require(!sessions.status().loaded, "unloading left a model loaded");
    // Unloading an empty manager is a no-op rather than an error, so repeated
    // Unload clicks are harmless.
    sessions.unload();
    require(!sessions.status().loaded, "a second unload reported a loaded model");

    // Idle timeout: modelRegistry.idleUnloadSeconds acting on a resident
    // session, driven by an injected clock so the test does not sleep.
    {
      sessions.load("stub-package", config);
      const auto loaded_now = std::chrono::steady_clock::now();

      require(!sessions.unload_if_idle(std::chrono::seconds(60), loaded_now),
              "unload_if_idle fired before the timeout elapsed");
      require(sessions.status().loaded, "unload_if_idle unloaded before the timeout elapsed");

      require(!sessions.unload_if_idle(std::chrono::seconds(60), loaded_now + std::chrono::seconds(59)),
              "unload_if_idle fired one second early");
      require(sessions.status().loaded, "unload_if_idle unloaded one second early");

      require(sessions.unload_if_idle(std::chrono::seconds(60), loaded_now + std::chrono::seconds(60)),
              "unload_if_idle did not fire once the timeout elapsed");
      require(!sessions.status().loaded, "unload_if_idle left a model loaded past the timeout");

      // idleUnloadSeconds <= 0 disables idle unloading regardless of elapsed time.
      sessions.load("stub-package", config);
      require(!sessions.unload_if_idle(std::chrono::seconds(0), loaded_now + std::chrono::hours(1)),
              "unload_if_idle fired with idleUnloadSeconds disabled");
      require(sessions.status().loaded, "a disabled idle timeout unloaded the resident model");
      sessions.unload();

      // No resident model: a no-op, not an error.
      require(!sessions.unload_if_idle(std::chrono::seconds(60), loaded_now + std::chrono::hours(1)),
              "unload_if_idle fired with nothing loaded");
    }
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
  return 0;
}
