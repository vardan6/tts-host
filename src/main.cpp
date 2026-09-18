#include "tts_host/catalogue_download.hpp"
#include "tts_host/clipboard.hpp"
#include "tts_host/config_loader.hpp"
#include "tts_host/language_selection.hpp"
#include "tts_host/model_catalogue.hpp"
#include "tts_host/model_import.hpp"
#include "tts_host/model_registry.hpp"
#include "tts_host/model_session.hpp"
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
#include <vector>

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

std::string join_languages(const std::vector<std::string> &languages) {
  std::string joined;
  for (const auto &language : languages) {
    if (!joined.empty()) {
      joined += ",";
    }
    joined += language;
  }
  return joined;
}

struct RunnerSelection {
  std::filesystem::path runner_path;
  std::optional<std::filesystem::path> model_path;
  std::optional<std::filesystem::path> voice_path;
};

// Model-id resolution lives in model_session.cpp so the settings window's
// Load/Unload controls and this CLI path agree on which runner and files a
// model id means.
RunnerSelection resolve_runner_selection_for_model(const std::string &model_id,
                                                   const tts_host::ConfigDocument &document,
                                                   const std::filesystem::path &argv0) {
  const auto selection =
      tts_host::resolve_model_runner_selection(model_id, document, executable_dir(argv0));
  return {selection.runner_path, selection.model_path, selection.voice_path};
}

// With no --model/--runner override, synthesis follows the same path a
// settings-window profile pick would: the request's language names a
// languageDefaults profile, and that profile's "model" is the model id to
// load. The language itself comes from --language, else the text's script,
// else English (docs/design/architecture.md#speech-pipeline).
RunnerSelection resolve_default_runner_selection(const tts_host::SelectedLanguage &language,
                                                 const tts_host::ConfigDocument &document,
                                                 const std::filesystem::path &argv0) {
  const auto &language_defaults = document.value.at("languageDefaults");
  if (!language_defaults.contains(language.tag)) {
    // An explicit tag the user typed is a mistake worth naming; a fallback to
    // English on a config that simply has no "en" entry is not, and keeps the
    // pre-profile stub-runner behavior.
    if (language.source == tts_host::LanguageSource::Explicit) {
      throw std::runtime_error("languageDefaults has no entry for language: " + language.tag);
    }
    return {default_runner_path(argv0), std::nullopt, std::nullopt};
  }
  const auto profile_name = language_defaults.at(language.tag).get<std::string>();
  const auto &profiles = document.value.at("profiles");
  if (!profiles.contains(profile_name)) {
    throw std::runtime_error("languageDefaults." + language.tag +
                             " names unknown profile: " + profile_name);
  }
  const auto &profile = profiles.at(profile_name);
  const auto model_id = profile.at("model").get<std::string>();
  std::cout << "Language: " << language.tag << " ("
            << tts_host::describe_language_source(language.source) << "), profile " << profile_name
            << ", model " << model_id << '\n';
  try {
    return resolve_runner_selection_for_model(model_id, document, argv0);
  } catch (const std::exception &primary_error) {
    if (!profile.contains("fallbackProfile")) {
      throw;
    }

    const auto fallback_profile_name = profile.at("fallbackProfile").get<std::string>();
    if (fallback_profile_name == profile_name) {
      throw std::runtime_error("profiles." + profile_name +
                               ".fallbackProfile must name a different profile");
    }
    if (!profiles.contains(fallback_profile_name)) {
      throw std::runtime_error("profiles." + profile_name + ".fallbackProfile names unknown profile: " +
                               fallback_profile_name);
    }

    const auto &fallback_profile = profiles.at(fallback_profile_name);
    const auto fallback_model_id = fallback_profile.at("model").get<std::string>();
    std::cout << "Profile " << profile_name << " is unavailable (" << primary_error.what()
              << "); using fallback profile " << fallback_profile_name << ", model "
              << fallback_model_id << '\n';
    try {
      return resolve_runner_selection_for_model(fallback_model_id, document, argv0);
    } catch (const std::exception &fallback_error) {
      throw std::runtime_error("Profile " + profile_name + " is unavailable (" + primary_error.what() +
                               "); fallback profile " + fallback_profile_name + " is also unavailable (" +
                               fallback_error.what() + ")");
    }
  }
}

// `spoken_text` is the normalized text, so script detection sees what will
// actually be spoken rather than the markup around it.
RunnerSelection resolve_runner_selection(const tts_host::CliOptions &options,
                                         const std::string &spoken_text,
                                         const tts_host::ConfigDocument &document,
                                         const std::filesystem::path &argv0) {
  if (options.runner_path_override.has_value()) {
    return {*options.runner_path_override, std::nullopt, std::nullopt};
  }

  if (options.model_id.has_value()) {
    return resolve_runner_selection_for_model(*options.model_id, document, argv0);
  }

  std::vector<std::string> configured_languages;
  for (const auto &entry : document.value.at("languageDefaults").items()) {
    configured_languages.push_back(entry.key());
  }
  const auto language =
      tts_host::select_request_language(options.language, spoken_text, configured_languages);
  return resolve_default_runner_selection(language, document, argv0);
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
  // Normalization is the host's job, not each client's, so every surface gets
  // it (docs/design/architecture.md#speech-pipeline). Runners receive speakable
  // text and never see markup. It happens before runner selection because the
  // language -- and so the profile, model, and engine runner -- is detected
  // from the text when the request does not name one.
  const auto spoken_text = tts_host::normalize_markdown(resolve_input_text(options));
  if (spoken_text.empty()) {
    throw std::runtime_error("nothing to synthesize: the text is empty once markup is removed");
  }

  const auto selection = resolve_runner_selection(options, spoken_text, document, argv0);

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
        tts_host::run_settings_window(document, executable_dir(argv[0]));
        return EXIT_SUCCESS;
      }

      // Tray mode (docs/design/architecture.md#desktop-integration): blocks
      // until the user chooses Quit. Windows only in this slice -- see
      // docs/adr/0007-native-ui-per-platform.md.
      tts_host::run_tray_icon(
          document, executable_dir(argv[0]),
          [&document, argv0 = std::filesystem::path(argv[0])](const std::string &text) {
            tts_host::CliOptions tray_options;
            tray_options.synthesize_text = text;
            tray_options.play_audio = true;
            run_synthesis(tray_options, document, argv0);
          });
      return EXIT_SUCCESS;
    }

    std::cout << "Headless host bootstrap complete\n";
    std::cout << "Config: " << document.paths.config_path.string() << '\n';
    std::cout << "Schema: " << document.paths.schema_path.string() << '\n';

    // Imports before listing, so --import-model with --list-models shows the
    // package that was just installed.
    if (options->import_model_path.has_value()) {
      const auto imported = tts_host::import_model_package(*options->import_model_path, document);
      std::cout << "Imported " << imported.display_name << " (" << imported.id << ") into "
                << imported.package_path.string() << '\n';
    }

    if (options->list_models) {
      const auto scan = tts_host::scan_model_registry(document);
      std::cout << "Discovered model packages: " << scan.discovered_packages.size() << '\n';
      for (const auto &package : scan.discovered_packages) {
        std::cout << "  OK  " << package.id << " (" << package.engine << ", "
                  << join_languages(package.languages) << ") -> " << package.manifest_path.string()
                  << '\n';
      }

      std::cout << "Unsupported registry entries: " << scan.unsupported_entries.size() << '\n';
      for (const auto &entry : scan.unsupported_entries) {
        std::cout << "  BAD " << entry.path.string() << " :: " << entry.reason << '\n';
      }
    }

    // The curated download catalogue
    // (docs/design/architecture.md#download-catalogue). Listing only: the
    // licence and size a user has to see before agreeing to a download, and
    // whether the registry already has the package. --download-model below
    // does the fetching, verifying, and installing.
    if (options->list_catalogue) {
      const auto &entries = tts_host::model_catalogue();
      tts_host::validate_catalogue(entries);
      const auto scan = tts_host::scan_model_registry(document);

      std::cout << "Downloadable model packages: " << entries.size() << '\n';
      for (const auto &entry : entries) {
        const bool installed = tts_host::catalogue_entry_installed(entry, scan);
        std::cout << "  " << (installed ? "HAVE" : "GET ") << ' ' << entry.id << " ("
                  << entry.engine << ", " << join_languages(entry.languages) << ", "
                  << tts_host::describe_download_size(tts_host::catalogue_entry_size_bytes(entry))
                  << ")\n";
        std::cout << "       " << entry.display_name << " :: "
                  << tts_host::describe_catalogue_license(entry) << '\n';
      }
    }

    // Fetches, verifies, and installs a catalogue entry -- WinHTTP on
    // Windows; other platforms throw a clear not-implemented error, same
    // convention as the tray and settings window (docs/design/architecture.md#download-catalogue).
    if (options->download_model_id.has_value()) {
      const auto &entries = tts_host::model_catalogue();
      const auto found =
          std::find_if(entries.begin(), entries.end(),
                       [&](const auto &entry) { return entry.id == *options->download_model_id; });
      if (found == entries.end()) {
        throw std::runtime_error("unknown catalogue entry id: " + *options->download_model_id);
      }

      const auto scan = tts_host::scan_model_registry(document);
      if (tts_host::catalogue_entry_installed(*found, scan)) {
        throw std::runtime_error("'" + found->id + "' is already installed");
      }

      const auto &directories = document.value.at("modelRegistry").at("directories");
      if (directories.empty()) {
        throw std::runtime_error(
            "no model directory is configured to download into (modelRegistry.directories is empty)");
      }

      std::filesystem::path destination_root;
      if (options->download_destination_dir.has_value()) {
        destination_root = std::filesystem::absolute(*options->download_destination_dir).lexically_normal();
        const bool matches_configured = std::any_of(
            directories.begin(), directories.end(), [&](const nlohmann::json &configured_entry) {
              return tts_host::resolve_registry_directory(
                        document.paths.config_path, configured_entry.get<std::string>()) ==
                    destination_root;
            });
        if (!matches_configured) {
          throw std::runtime_error("--model-directory must name one of modelRegistry.directories");
        }
      } else {
        destination_root = tts_host::resolve_registry_directory(document.paths.config_path,
                                                                 directories.front().get<std::string>());
      }

      std::cout << "Downloading " << found->display_name << " (" << found->id << ", "
                << tts_host::describe_download_size(tts_host::catalogue_entry_size_bytes(*found))
                << ")\n";
      const auto installed = tts_host::download_catalogue_entry(
          *found, destination_root, document, tts_host::fetch_url_to_file,
          [](const tts_host::DownloadProgress &download_progress) {
            std::cout << "  " << download_progress.relative_path << ": "
                      << download_progress.bytes_downloaded << " / " << download_progress.bytes_total
                      << " bytes\n";
          });
      std::cout << "Installed " << installed.display_name << " (" << installed.id << ") into "
                << installed.package_path.string() << '\n';
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
