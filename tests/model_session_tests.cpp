#include "tts_host/model_session.hpp"
#include "tts_host/model_registry.hpp"
#include "tts_host/path_encoding.hpp"

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

    // Settings and tray share this resolution/load path. Exercise native paths
    // and UTF-8 JSON independently of the machine's active ANSI code page.
    {
      const auto root = std::filesystem::temp_directory_path() /
          tts_host::path_from_utf8("tts-unicode-" + std::to_string(
              std::chrono::steady_clock::now().time_since_epoch().count()) +
              "-\xd5\x80\xd5\xa1\xd5\xb5-\xd0\xa0\xd1\x83\xd1\x81");
      struct Cleanup {
        std::filesystem::path root;
        ~Cleanup() { std::error_code error; std::filesystem::remove_all(root, error); }
      } cleanup{root};
      const auto models = root / tts_host::path_from_utf8("\xd0\xbc\xd0\xbe\xd0\xb4\xd0\xb5\xd0\xbb\xd0\xb8");
      const auto package = models / "package";
      std::filesystem::create_directories(package);
      const auto model_name = std::string("\xd5\xb4\xd5\xb8\xd5\xa4\xd5\xa5\xd5\xac.bin");
      const auto voice_name = std::string("\xd0\xb3\xd0\xbe\xd0\xbb\xd0\xbe\xd1\x81.bin");
      auto manifest = tts_host::scan_model_registry(config).discovered_packages.front().manifest;
      manifest["id"] = "unicode-package";
      manifest["engine"] = "stub";
      manifest["$schema"] = tts_host::path_to_utf8(std::filesystem::absolute(
          std::filesystem::path(argv[2]).parent_path() / "../../../schemas/model.schema.json"));
      manifest["files"] = {{"model", model_name}, {"voice", voice_name}};
      std::ofstream(package / "model.json") << manifest.dump();
      std::ofstream(package / tts_host::path_from_utf8(model_name)) << "model";
      std::ofstream(package / tts_host::path_from_utf8(voice_name)) << "voice";
      std::filesystem::copy_file(stub_runner, root / stub_runner.filename());
      auto unicode_config = config;
      unicode_config.paths.config_path = root / "config.json";
      unicode_config.value["modelRegistry"]["directories"] =
          nlohmann::json::array({tts_host::path_to_utf8(models)});
      const auto selected = tts_host::resolve_model_runner_selection(
          "unicode-package", unicode_config, root);
      require(selected.model_path == package / tts_host::path_from_utf8(model_name) &&
                  *selected.voice_path == package / tts_host::path_from_utf8(voice_name),
              "Unicode manifest paths changed during resolution");
      const auto wire = tts_host::parse_runner_load_request(tts_host::make_runner_load_request(
          2, tts_host::path_to_utf8(selected.model_path),
          tts_host::path_to_utf8(*selected.voice_path)));
      require(tts_host::path_from_utf8(wire.model_path) == selected.model_path &&
                  tts_host::path_from_utf8(*wire.voice_path) == *selected.voice_path,
              "UTF-8 load paths did not round trip");
      tts_host::ModelSessionManager unicode_sessions(root);
      unicode_sessions.load("unicode-package", unicode_config);
      require(unicode_sessions.status().loaded, "Unicode Settings model did not load");
      unicode_sessions.unload();
#ifndef _WIN32
      // Exercise the production decoder and voice-file reader as well as the
      // stub handshake. Native Windows ONNX loading has a separately tracked
      // DLL/API-version mismatch; its runner still compiles in that build.
      const auto kokoro = tts_host::runner_path_for_engine("kokoro-onnx", runner_directory);
      std::filesystem::copy_file(kokoro, root / kokoro.filename());
      std::filesystem::copy_file(runner_directory / "libonnxruntime.so.1",
                                 root / "libonnxruntime.so.1");
      std::filesystem::copy_file(std::filesystem::path(argv[2]).parent_path() /
                                    "../kokoro_runner/placeholder.onnx", selected.model_path,
                                std::filesystem::copy_options::overwrite_existing);
      std::vector<float> voice(256, 0.0f);
      {
        std::ofstream output(*selected.voice_path, std::ios::binary);
        output.write(reinterpret_cast<const char *>(voice.data()), voice.size() * sizeof(float));
      }
      tts_host::RunnerSession kokoro_session(root / kokoro.filename());
      tts_host::parse_runner_initialize_response(
          kokoro_session.send_request(tts_host::make_runner_initialize_request(1)));
      tts_host::parse_runner_load_response(kokoro_session.send_request(
          tts_host::make_runner_load_request(2, tts_host::path_to_utf8(selected.model_path),
                                           tts_host::path_to_utf8(*selected.voice_path))));
      require(kokoro_session.finish() == 0, "Kokoro Unicode model/voice load failed");
#endif

    }

    // A Qwen load needs all package assets, so the descriptor, rather than
    // just the GGUF backbone, crosses the existing engine-specific load path.
    {
      const auto root = std::filesystem::temp_directory_path() /
          ("tts-qwen-package-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
      struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() { std::error_code ec; std::filesystem::remove_all(path, ec); }
      } cleanup{root};
      const auto package = root / "models" / "qwen";
      std::filesystem::create_directories(package);
      auto manifest = tts_host::scan_model_registry(config).discovered_packages.front().manifest;
      manifest["$schema"] = tts_host::path_to_utf8(std::filesystem::absolute(
          std::filesystem::path(argv[2]).parent_path() / "../../../schemas/model.schema.json"));
      manifest["id"] = "qwen-test";
      manifest["engine"] = "qwen3-gguf";
      manifest["files"] = {{"model", "model.gguf"}, {"mmproj", "mmproj.gguf"},
          {"voice", "reference.wav"}, {"voiceLicense", "reference-license.txt"}};
      std::ofstream(package / "model.gguf") << "GGUFfixture";
      std::ofstream(package / "mmproj.gguf") << "GGUFfixture";
      std::ofstream(package / "reference.wav") << "reference";
      std::ofstream(package / "reference-license.txt") << "test fixture";
      const auto save = [&] { std::ofstream(package / "model.json") << manifest.dump(); };
      save();
      std::filesystem::copy_file(stub_runner, tts_host::runner_path_for_engine("qwen3-gguf", root));
      auto qwen_config = config;
      qwen_config.paths.config_path = root / "config.json";
      qwen_config.value["modelRegistry"]["directories"] = nlohmann::json::array({"models"});
      const auto selected = tts_host::resolve_model_runner_selection("qwen-test", qwen_config, root);
      require(selected.model_path == package / "model.json" &&
              selected.voice_path == package / "reference.wav",
              "Qwen resolution did not pass its package descriptor and voice");
      const auto rejects = [&](const std::string &needle) {
        try { tts_host::validate_model_package(package); }
        catch (const std::exception &error) {
          require(std::string(error.what()).find(needle) != std::string::npos,
                  "Qwen validation reported an unexpected error");
          return;
        }
        throw std::runtime_error("invalid Qwen package was accepted");
      };
      manifest["files"].erase("mmproj"); save(); rejects("requires /files/mmproj");
      manifest["files"]["mmproj"] = "absent.gguf"; save(); rejects("non-empty file");
      manifest["files"]["mmproj"] = "mmproj.gguf"; save();
      std::ofstream(package / "mmproj.gguf", std::ios::trunc) << "wrong";
      rejects("requires GGUF");
      std::ofstream(package / "mmproj.gguf", std::ios::trunc) << "GGUFfixture";
      manifest["files"]["voice"] = "../outside.wav"; save(); rejects("escapes the package root");
      manifest["files"]["voice"] = "reference.wav";
      manifest["files"].erase("voiceLicense"); save(); rejects("requires /files/voiceLicense");
      manifest["files"]["voiceLicense"] = "reference-license.txt";
      manifest["languages"] = {"en", "ru"}; save(); rejects("exactly one language");
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
