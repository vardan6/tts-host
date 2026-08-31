#include "tts_host/clipboard.hpp"
#include "tts_host/config_loader.hpp"
#include "tts_host/model_registry.hpp"
#include "tts_host/playback_sink.hpp"
#include "tts_host/runner_launcher.hpp"
#include "tts_host/runner_protocol.hpp"
#include "tts_host/settings_window.hpp"
#include "tts_host/text_normalizer.hpp"
#include "tts_host/text_splitter.hpp"
#include "tts_host/tray_icon.hpp"
#include "tts_host/wav_writer.hpp"

#include <algorithm>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <utility>

namespace {

std::filesystem::path executable_dir(const std::filesystem::path &argv0) {
  std::error_code ec;
  const auto absolute = std::filesystem::absolute(argv0, ec);
  return (ec || argv0.empty()) ? std::filesystem::current_path() : absolute.parent_path();
}

std::filesystem::path default_runner_path(const std::filesystem::path &argv0) {
#ifdef _WIN32
  return executable_dir(argv0) / "tts-host-stub-runner.exe";
#else
  return executable_dir(argv0) / "tts-host-stub-runner";
#endif
}

// Runner binaries for a registry model's declared engine are installed
// beside tts-host, named by convention: tts-host-<engine>-runner[.exe].
std::filesystem::path runner_path_for_engine(const std::string &engine,
                                             const std::filesystem::path &argv0) {
#ifdef _WIN32
  return executable_dir(argv0) / ("tts-host-" + engine + "-runner.exe");
#else
  return executable_dir(argv0) / ("tts-host-" + engine + "-runner");
#endif
}

struct RunnerSelection {
  std::filesystem::path runner_path;
  std::optional<std::filesystem::path> model_path;
  std::optional<std::filesystem::path> voice_path;
};

RunnerSelection resolve_runner_selection_for_model(const std::string &model_id,
                                                   const tts_host::ConfigDocument &document,
                                                   const std::filesystem::path &argv0) {
  const auto scan = tts_host::scan_model_registry(document);
  const auto package = std::find_if(
      scan.discovered_packages.begin(), scan.discovered_packages.end(),
      [&](const tts_host::ModelPackageCandidate &candidate) {
        return candidate.id == model_id;
      });
  if (package == scan.discovered_packages.end()) {
    throw std::runtime_error("Unknown model id: " + model_id);
  }

  auto runner_path = runner_path_for_engine(package->engine, argv0);
  if (!std::filesystem::exists(runner_path)) {
    throw std::runtime_error("No runner installed for engine '" + package->engine +
                             "': expected " + runner_path.string());
  }
  const auto &files = package->manifest.at("files");
  const auto model_path = package->package_path / files.at("model").get<std::string>();
  std::optional<std::filesystem::path> voice_path;
  if (files.contains("voice")) {
    voice_path = package->package_path / files.at("voice").get<std::string>();
  }
  return {runner_path, model_path, voice_path};
}

// With no --model/--runner override, synthesis follows the same path a
// settings-window profile pick would: languageDefaults["en"] names a
// profile, and that profile's "model" is the model id to load. English is
// the only language surfaced end to end so far (see roadmap).
RunnerSelection resolve_default_runner_selection(const tts_host::ConfigDocument &document,
                                                 const std::filesystem::path &argv0) {
  const auto &language_defaults = document.value.at("languageDefaults");
  if (!language_defaults.contains("en")) {
    return {default_runner_path(argv0), std::nullopt, std::nullopt};
  }
  const auto profile_name = language_defaults.at("en").get<std::string>();
  const auto &profiles = document.value.at("profiles");
  if (!profiles.contains(profile_name)) {
    throw std::runtime_error("languageDefaults.en names unknown profile: " + profile_name);
  }
  const auto model_id = profiles.at(profile_name).at("model").get<std::string>();
  return resolve_runner_selection_for_model(model_id, document, argv0);
}

RunnerSelection resolve_runner_selection(const tts_host::CliOptions &options,
                                         const tts_host::ConfigDocument &document,
                                         const std::filesystem::path &argv0) {
  if (options.runner_path_override.has_value()) {
    return {*options.runner_path_override, std::nullopt, std::nullopt};
  }

  if (options.model_id.has_value()) {
    return resolve_runner_selection_for_model(*options.model_id, document, argv0);
  }

  return resolve_default_runner_selection(document, argv0);
}

// Resolves whichever of --synthesize/--stdin/--clipboard was given (parse_cli
// guarantees exactly one) to the raw text to speak.
std::string resolve_input_text(const tts_host::CliOptions &options) {
  if (options.synthesize_text.has_value()) {
    return *options.synthesize_text;
  }
  if (options.use_stdin_text) {
    std::ostringstream buffer;
    buffer << std::cin.rdbuf();
    return buffer.str();
  }
  return tts_host::read_clipboard_text();
}

void run_synthesis(const tts_host::CliOptions &options, const tts_host::ConfigDocument &document,
                   const std::filesystem::path &argv0) {
  const auto selection = resolve_runner_selection(options, document, argv0);

  tts_host::RunnerSession session(selection.runner_path);

  const auto initialize_response =
      session.send_request(tts_host::make_runner_initialize_request(1));
  const auto initialize_result = tts_host::parse_runner_initialize_response(initialize_response);
  if (initialize_result.protocol_version != tts_host::kRunnerProtocolVersion) {
    throw std::runtime_error("runner reported an unsupported protocol version");
  }

  if (selection.model_path.has_value()) {
    const auto load_response = session.send_request(tts_host::make_runner_load_request(
        2, selection.model_path->string(),
        selection.voice_path.has_value() ? std::optional<std::string>(selection.voice_path->string())
                                         : std::nullopt));
    tts_host::parse_runner_load_response(load_response);
  }

  // Normalization is the host's job, not each client's, so every surface gets
  // it (docs/design/architecture.md#speech-pipeline). Runners receive speakable
  // text and never see markup.
  const auto spoken_text = tts_host::normalize_markdown(resolve_input_text(options));
  if (spoken_text.empty()) {
    throw std::runtime_error("nothing to synthesize: the text is empty once markup is removed");
  }

  // Splitting into sentence-scale chunks and synthesizing them as separate
  // back-to-back requests is what lets a chunk start playing before the rest
  // of a long text is synthesized, and makes future cancellation cheap --
  // stop after the current chunk (docs/design/architecture.md#speech-pipeline).
  const auto chunks = tts_host::split_into_sentences(spoken_text);

  tts_host::SystemPlaybackSink playback_sink;
  const std::string output_device = document.value.at("audio").at("outputDevice").get<std::string>();

  std::vector<std::uint8_t> pcm_payload;
  std::uint32_t sample_rate_hz = 0;
  std::uint32_t channels = 0;
  std::uint64_t total_sample_frames = 0;

  // Plays each chunk on a background thread so the next chunk's runner round
  // trip (the send_request/read_audio_stream_until_end below) overlaps with
  // playback instead of waiting for it -- the "small lookahead" in
  // docs/design/architecture.md#speech-pipeline. Joining the previous
  // playback thread before starting the next one keeps the "two utterances
  // never play simultaneously" guarantee from the same doc.
  std::thread playback_thread;
  std::exception_ptr playback_error;
  const auto join_playback = [&playback_thread, &playback_error]() {
    if (playback_thread.joinable()) {
      playback_thread.join();
    }
    if (playback_error) {
      std::rethrow_exception(std::exchange(playback_error, nullptr));
    }
  };

  int next_request_id = 3;
  for (const auto &chunk : chunks) {
    const auto synthesize_response = session.send_request(
        tts_host::make_runner_synthesize_request(next_request_id++, chunk));
    const auto synthesize_result = tts_host::parse_runner_synthesize_response(synthesize_response);
    sample_rate_hz = synthesize_result.sample_rate_hz;
    channels = synthesize_result.channels;
    total_sample_frames += synthesize_result.total_sample_frames;

    const auto frames = session.read_audio_stream_until_end();
    std::vector<std::uint8_t> chunk_payload;
    for (const auto &frame : frames) {
      chunk_payload.insert(chunk_payload.end(), frame.payload.begin(), frame.payload.end());
    }

    if (options.play_audio) {
      join_playback();
      playback_thread = std::thread(
          [&playback_sink, sample_rate_hz = synthesize_result.sample_rate_hz,
           channels = static_cast<std::uint16_t>(synthesize_result.channels), chunk_payload, &output_device,
           &playback_error]() {
            try {
              playback_sink.play(sample_rate_hz, channels, chunk_payload, output_device);
            } catch (...) {
              playback_error = std::current_exception();
            }
          });
    }

    if (options.output_path.has_value()) {
      pcm_payload.insert(pcm_payload.end(), chunk_payload.begin(), chunk_payload.end());
    }
  }
  join_playback();

  std::optional<tts_host::RunnerStatsResponse> stats;
  if (options.report_stats) {
    const auto stats_response = session.send_request(tts_host::make_runner_stats_request(next_request_id++));
    stats = tts_host::parse_runner_stats_response(stats_response);
  }

  const auto exit_code = session.finish();
  if (exit_code != 0) {
    throw std::runtime_error("runner process exited with code " + std::to_string(exit_code));
  }

  if (options.output_path.has_value()) {
    tts_host::write_wav_file(*options.output_path, sample_rate_hz,
                             static_cast<std::uint16_t>(channels), 16, pcm_payload);
    std::cout << "Synthesized " << total_sample_frames << " sample frames to "
              << options.output_path->string() << '\n';
  }

  if (options.play_audio) {
    std::cout << "Played " << total_sample_frames << " sample frames\n";
  }

  if (stats.has_value()) {
    std::cout << "Stats: peak RSS " << stats->peak_rss_bytes << " bytes, peak VRAM "
              << stats->peak_vram_bytes << " bytes, time to first chunk "
              << stats->time_to_first_chunk_ms << " ms, sample count " << stats->sample_count
              << '\n';
  }
}

}  // namespace

int main(int argc, char **argv) {
  std::string cli_message;
  const auto options = tts_host::parse_cli(argc, argv, cli_message);
  if (!options.has_value()) {
    if (!cli_message.empty()) {
      const bool is_help = cli_message.rfind("Usage:", 0) == 0;
      std::ostream &stream = is_help ? std::cout : std::cerr;
      stream << cli_message << '\n';
      return is_help ? EXIT_SUCCESS : EXIT_FAILURE;
    }
    return EXIT_FAILURE;
  }

  try {
    const auto document = tts_host::load_config(*options, std::filesystem::path(argv[0]));

    if (!options->headless) {
      if (options->settings_window) {
        // Settings window (docs/design/architecture.md#desktop-integration):
        // opens independently of the tray, blocks until closed. Windows only
        // in this slice -- see docs/adr/0007-native-ui-per-platform.md.
        tts_host::run_settings_window(document);
        return EXIT_SUCCESS;
      }

      // Tray mode (docs/design/architecture.md#desktop-integration): blocks
      // until the user chooses Quit. Windows only in this slice -- see
      // docs/adr/0007-native-ui-per-platform.md.
      tts_host::run_tray_icon(document);
      return EXIT_SUCCESS;
    }

    std::cout << "Headless host bootstrap complete\n";
    std::cout << "Config: " << document.paths.config_path.string() << '\n';
    std::cout << "Schema: " << document.paths.schema_path.string() << '\n';

    if (options->list_models) {
      const auto scan = tts_host::scan_model_registry(document);
      std::cout << "Discovered model packages: " << scan.discovered_packages.size() << '\n';
      for (const auto &package : scan.discovered_packages) {
        std::cout << "  OK  " << package.id << " (" << package.engine << ", ";
        for (std::size_t index = 0; index < package.languages.size(); ++index) {
          if (index > 0) {
            std::cout << ",";
          }
          std::cout << package.languages[index];
        }
        std::cout << ") -> " << package.manifest_path.string() << '\n';
      }

      std::cout << "Unsupported registry entries: " << scan.unsupported_entries.size() << '\n';
      for (const auto &entry : scan.unsupported_entries) {
        std::cout << "  BAD " << entry.path.string() << " :: " << entry.reason << '\n';
      }
    }

    if (options->synthesize_text.has_value() || options->use_stdin_text || options->use_clipboard_text) {
      run_synthesis(*options, document, std::filesystem::path(argv[0]));
    }

    return EXIT_SUCCESS;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return EXIT_FAILURE;
  }
}
