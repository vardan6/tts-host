#include "tts_host/activity_log.hpp"
#include "tts_host/catalogue_download.hpp"
#include "tts_host/path_encoding.hpp"
#include "tts_host/audio_history.hpp"
#include "tts_host/clipboard.hpp"
#include "tts_host/config_loader.hpp"
#include "tts_host/config_snapshot.hpp"
#include "tts_host/language_selection.hpp"
#include "tts_host/synthesis_pipeline.hpp"
#include "tts_host/local_api_server.hpp"
#include "tts_host/model_catalogue.hpp"
#include "tts_host/model_import.hpp"
#include "tts_host/model_registry.hpp"
#include "tts_host/model_session.hpp"
#include "tts_host/model_update.hpp"
#include "tts_host/playback_controller.hpp"
#include "tts_host/playback_sink.hpp"
#include "tts_host/runner_launcher.hpp"
#include "tts_host/recovering_runner.hpp"
#include "tts_host/runner_protocol.hpp"
#include "tts_host/settings_window.hpp"
#include "tts_host/speech_queue.hpp"
#include "tts_host/startup_choice.hpp"
#ifdef _WIN32
#include "tts_host/startup_registration.hpp"
#endif
#include "tts_host/text_normalizer.hpp"
#include "tts_host/text_splitter.hpp"
#include "tts_host/tray_icon.hpp"
#include "tts_host/wav_writer.hpp"

#include <algorithm>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#include <cstdio>
#include <fcntl.h>
#include <io.h>
#endif

namespace {

void report_fatal_error(const std::string &message, bool desktop) {
  std::cerr << message << '\n';
#ifdef _WIN32
  if (desktop) {
    const auto wide_message = tts_host::path_from_utf8(message).wstring();
    MessageBoxW(nullptr, wide_message.c_str(), L"TTS Host — unable to start",
                MB_OK | MB_ICONERROR);
  }
#else
  (void)desktop;
#endif
}

std::filesystem::path executable_dir(const std::filesystem::path &argv0) {
  std::error_code ec;
  const auto absolute = std::filesystem::absolute(argv0, ec);
  return (ec || argv0.empty()) ? std::filesystem::current_path() : absolute.parent_path();
}

std::filesystem::path default_runner_path(const std::filesystem::path &argv0) {
  return tts_host::runner_path_for_engine("stub", executable_dir(argv0));
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

using tts_host::RunnerSelection;

// Model-id resolution lives in model_session.cpp so the settings window's
// Load/Unload controls and this CLI path agree on which runner and files a
// model id means.
RunnerSelection resolve_runner_selection_for_model(const std::string &model_id,
                                                   const tts_host::ConfigDocument &document,
                                                   const std::filesystem::path &argv0) {
  const auto selection =
      tts_host::resolve_model_runner_selection(model_id, document, executable_dir(argv0));
  return {selection.runner_path, selection.model_path, selection.voice_path,
          selection.model_id, selection.display_name, selection.engine, {}};
}

// A runner selection plus the profile whose fallbackProfile may still replace
// it if the runner rejects its load. fallback_profile is empty when no
// fallback remains: none configured, an explicit --model/--runner, or the
// fallback already replaced an unresolvable model.
struct RunnerChoice {
  RunnerSelection selection;
  std::string profile_name;
  std::string fallback_profile;
};

RunnerSelection resolve_profile_selection(const nlohmann::json &profile,
                                          const tts_host::ConfigDocument &document,
                                          const std::filesystem::path &argv0) {
  auto selected = resolve_runner_selection_for_model(profile.at("model").get<std::string>(), document, argv0);
  if (profile.contains("idleUnloadSeconds"))
    selected.idle_override = std::chrono::seconds(profile.at("idleUnloadSeconds").get<int>());
  return selected;
}

// Replaces profile_name's model, unavailable for `reason`, with its
// fallbackProfile and logs why (docs/design/architecture.md#configuration).
RunnerSelection resolve_fallback_selection(const std::string &profile_name, const std::string &reason,
                                           const tts_host::ConfigDocument &document,
                                           const std::filesystem::path &argv0) {
  const auto &profiles = document.value.at("profiles");
  const auto fallback_profile_name = profiles.at(profile_name).at("fallbackProfile").get<std::string>();
  if (fallback_profile_name == profile_name) {
    throw std::runtime_error("profiles." + profile_name +
                             ".fallbackProfile must name a different profile");
  }
  if (!profiles.contains(fallback_profile_name)) {
    throw std::runtime_error("profiles." + profile_name + ".fallbackProfile names unknown profile: " +
                             fallback_profile_name);
  }

  const auto &fallback_profile = profiles.at(fallback_profile_name);
  const auto message = "Profile " + profile_name + " is unavailable (" + reason +
                       "); using fallback profile " + fallback_profile_name + ", model " +
                       fallback_profile.at("model").get<std::string>();
  std::cout << message << '\n';
  tts_host::record_activity(message);
  try {
    return resolve_profile_selection(fallback_profile, document, argv0);
  } catch (const std::exception &fallback_error) {
    throw std::runtime_error("Profile " + profile_name + " is unavailable (" + reason +
                             "); fallback profile " + fallback_profile_name + " is also unavailable (" +
                             fallback_error.what() + ")");
  }
}

// With no --model/--runner override, synthesis follows the same path a
// settings-window profile pick would: the request's language names a
// languageDefaults profile, and that profile's "model" is the model id to
// load. The language itself comes from --language, else the text's script,
// else English (docs/design/architecture.md#speech-pipeline).
RunnerChoice resolve_default_runner_selection(const tts_host::SelectedLanguage &language,
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
    return {{default_runner_path(argv0), std::nullopt, std::nullopt, {}, {}, {}, {}}, {}, {}};
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
    return {resolve_profile_selection(profile, document, argv0), profile_name,
            profile.value("fallbackProfile", std::string())};
  } catch (const std::exception &primary_error) {
    if (!profile.contains("fallbackProfile")) {
      throw;
    }
    return {resolve_fallback_selection(profile_name, primary_error.what(), document, argv0),
            profile_name, {}};
  }
}

// `spoken_text` is the normalized text, so script detection sees what will
// actually be spoken rather than the markup around it.
RunnerChoice resolve_runner_selection(const tts_host::CliOptions &options,
                                      const std::string &spoken_text,
                                      const tts_host::ConfigDocument &document,
                                      const std::filesystem::path &argv0) {
  if (options.runner_path_override.has_value()) {
    return {{*options.runner_path_override, std::nullopt, std::nullopt, {}, {}, {}, {}}, {}, {}};
  }

  if (options.model_id.has_value()) {
    return {resolve_runner_selection_for_model(*options.model_id, document, argv0), {}, {}};
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
                   const std::filesystem::path &argv0,
                   tts_host::PlaybackControl *control = nullptr,
                   tts_host::PlaybackController *playback_controller = nullptr,
                   tts_host::ModelSessionManager *sessions = nullptr) {
  // Normalization is the host's job, not each client's, so every surface gets
  // it (docs/design/architecture.md#speech-pipeline). Runners receive speakable
  // text and never see markup. It happens before runner selection because the
  // language -- and so the profile, model, and engine runner -- is detected
  // from the text when the request does not name one.
  const auto spoken_text = tts_host::normalize_markdown(resolve_input_text(options));
  if (spoken_text.empty()) {
    throw std::runtime_error("nothing to synthesize: the text is empty once markup is removed");
  }

  auto choice = resolve_runner_selection(options, spoken_text, document, argv0);
  const auto cancelled = [control] { return control && control->cancelled(); };

  // The model loads before the first sentence, so a rejected load has emitted
  // no audio and the profile's fallback can serve the whole request. A later
  // failure never switches models (docs/design/architecture.md#load-rejection).
  const auto prepared = [&](tts_host::RecoveringRunner &runner) {
    try {
      runner.prepare();
      return true;
    } catch (const tts_host::RunnerLoadRejected &error) {
      if (choice.fallback_profile.empty()) throw;
      choice.selection = resolve_fallback_selection(choice.profile_name, error.what(), document, argv0);
      choice.fallback_profile.clear();
      return false;
    }
  };

  for (;;) {
    const auto selection = choice.selection;
    if (sessions) {
      const tts_host::ModelRunnerSelection model{selection.model_id, selection.display_name,
          selection.engine, selection.runner_path, selection.model_path.value_or(std::filesystem::path{}),
          selection.voice_path};
      auto lease = sessions->acquire(model, cancelled, selection.idle_override);
      if (!prepared(lease.runner())) continue;
      tts_host::run_synthesis_pipeline(spoken_text, options, document, selection,
                                       control, playback_controller, nullptr, {}, &lease.runner());
    } else {
      tts_host::RecoveringRunner runner(selection.runner_path, selection.model_path,
                                        selection.voice_path, {}, cancelled);
      if (!prepared(runner)) continue;
      tts_host::run_synthesis_pipeline(spoken_text, options, document, selection,
                                       control, playback_controller, nullptr, {}, &runner);
      if (const auto exit_code = runner.finish(); exit_code != 0) {
        throw std::runtime_error("runner process exited with code " + std::to_string(exit_code));
      }
    }
    return;
  }
}

// The Host's speech engine (docs/design/architecture.md#speech-queue): the
// profile chain, the shared model session, and the synthesis pipeline.
class HostSpeechEngine final : public tts_host::SpeechEngine {
 public:
  explicit HostSpeechEngine(std::filesystem::path argv0)
      : argv0_(std::move(argv0)), sessions_(executable_dir(argv0_)) {}

  void speak(const std::string &text, const tts_host::ConfigDocument &config, tts_host::PlaybackControl &control,
             tts_host::PlaybackController &playback) override {
    tts_host::CliOptions options;
    options.synthesize_text = text;
    options.play_audio = true;
    run_synthesis(options, config, argv0_, &control, &playback, &sessions_);
  }

  void warm(const tts_host::ConfigDocument &config, const std::function<bool()> &cancelled) override {
    sessions_.load_eager(config, cancelled);
  }

  void unload_if_idle(const tts_host::ConfigDocument &config) override {
    sessions_.unload_if_idle(
        std::chrono::seconds(config.value.at("modelRegistry").at("idleUnloadSeconds").get<int>()));
  }

  tts_host::ModelSessionManager &sessions() { return sessions_; }

 private:
  std::filesystem::path argv0_;
  tts_host::ModelSessionManager sessions_;
};

}  // namespace

int run_main(int argc, char **argv) {
  std::string cli_message;
  const auto options = tts_host::parse_cli(argc, argv, cli_message);
  if (!options.has_value()) {
    if (!cli_message.empty()) {
      const bool is_help = cli_message.rfind("Usage:", 0) == 0;
      std::ostream &stream = is_help ? std::cout : std::cerr;
      if (is_help) stream << cli_message << '\n';
      else {
        const bool headless = std::any_of(argv + 1, argv + argc,
            [](const char *argument) { return std::string(argument) == "--headless"; });
        report_fatal_error(cli_message, !headless);
      }
      return is_help ? EXIT_SUCCESS : EXIT_FAILURE;
    }
    return EXIT_FAILURE;
  }

  try {
    tts_host::AudioHistory::clean_abandoned_spools();
    auto document = tts_host::load_config(*options, tts_host::path_from_utf8(argv[0]));
    // Before anything scans the registry, finish or roll back a model update
    // or revert cut short by a crash or power loss.
    for (const auto &message : tts_host::recover_interrupted_model_updates(document)) {
      tts_host::record_activity(message);
      if (options->headless) std::cout << message << '\n';
    }

    // Launches never create, repoint, or remove the login startup entry; only
    // the Settings switch and the first-run answer do
    // (docs/adr/0008-os-owned-login-startup.md).
    if (!options->headless) {
      if (options->settings_window) {
        // Settings window (docs/design/architecture.md#desktop-integration):
        // opens independently of the tray, blocks until closed. Windows only
        // in this slice -- see docs/adr/0007-native-ui-per-platform.md.
        tts_host::run_settings_window(document, executable_dir(tts_host::path_from_utf8(argv[0])));
        return EXIT_SUCCESS;
      }

#ifdef _WIN32
      // First desktop launch in this data directory asks once whether to start
      // at login (docs/design/architecture.md#login-startup). A failure is
      // reported but never prevents the Host from starting.
      try {
        tts_host::ask_startup_choice_once(
            document.paths.data_dir,
            document.value.value("startup", nlohmann::json::object()).value("launchOnLogin", true),
            tts_host::show_startup_choice_dialog,
            [&document] { tts_host::set_start_at_login(true, document.paths.config_path); });
      } catch (const std::exception &error) {
        tts_host::record_activity(std::string("Could not record the login startup choice: ") + error.what());
        std::cerr << "Could not record the login startup choice: " << error.what() << '\n';
      }
#endif

      // Tray mode (docs/design/architecture.md#desktop-integration): blocks
      // until the user chooses Quit. Windows only in this slice -- see
      // docs/adr/0007-native-ui-per-platform.md.
      HostSpeechEngine speech_engine(tts_host::path_from_utf8(argv[0]));
      tts_host::SpeechQueue speech_queue(document, speech_engine);
      const auto &server = document.value.at("server");
      if (server.value("authentication", "none") != "none") {
        throw std::runtime_error(
            "local API authentication modes other than 'none' are reserved but not implemented");
      }
      std::vector<std::string> allowed_origins;
      for (const auto &origin : server.value("allowedOrigins", nlohmann::json::array())) {
        allowed_origins.push_back(origin.get<std::string>());
      }
      tts_host::LocalApiServer local_api(
          server.at("host").get<std::string>(),
          static_cast<unsigned short>(server.at("port").get<int>()), std::move(allowed_origins),
          [&speech_queue](std::string text) {
            speech_queue.submit(std::move(text), tts_host::SpeechQueueMode::Interrupt, "Local API");
          },
          [&speech_queue](const std::string &command) {
            if (command == "pause") speech_queue.playback_controller().pause();
            if (command == "resume") speech_queue.playback_controller().resume();
            if (command == "stop") speech_queue.stop();
            return speech_queue.status();
          });
      std::cout << "Local API listening on " << server.at("host") << ':' << server.at("port")
                << '\n';
      tts_host::run_tray_icon(
          document, executable_dir(tts_host::path_from_utf8(argv[0])),
          [&speech_queue](const std::string &text, tts_host::SpeechQueueMode queue_mode,
                          const std::string &source) {
            speech_queue.submit(text, queue_mode, source);
          }, speech_queue.playback_controller(),
          [&speech_queue](const tts_host::ConfigDocument &edited) {
            speech_queue.publish_config(edited);
          }, &speech_engine.sessions(),
          [&speech_queue] { speech_queue.stop(); });
      return EXIT_SUCCESS;
    }

    std::cout << "Headless host bootstrap complete\n";
    std::cout << "Config: " << tts_host::path_to_utf8(document.paths.config_path) << '\n';
    std::cout << "Schema: " << tts_host::path_to_utf8(document.paths.schema_path) << '\n';

    // Imports before listing, so --import-model with --list-models shows the
    // package that was just installed.
    if (options->import_model_path.has_value()) {
      const auto imported = tts_host::import_model_package(*options->import_model_path, document);
      std::cout << "Imported " << imported.display_name << " (" << imported.id << ") into "
                << tts_host::path_to_utf8(imported.package_path) << '\n';
    }

    if (options->list_models) {
      const auto scan = tts_host::scan_model_registry(document);
      std::cout << "Discovered model packages: " << scan.discovered_packages.size() << '\n';
      for (const auto &package : scan.discovered_packages) {
        std::cout << "  OK  " << package.id << " (" << package.engine << ", "
                  << join_languages(package.languages) << ") ["
                  << tts_host::describe_package_status(tts_host::package_status(package))
                  << (package.bundled ? ", bundled" : "") << "] -> "
                  << tts_host::path_to_utf8(package.manifest_path) << '\n';
      }

      std::cout << "Superseded model packages: " << scan.superseded_packages.size() << '\n';
      for (const auto &package : scan.superseded_packages) {
        std::cout << "  OLD " << package.id << " [" << (package.bundled ? "bundled" : "per-user")
                  << "] -> " << tts_host::path_to_utf8(package.manifest_path) << '\n';
      }

      std::cout << "Previous model versions: " << scan.previous_packages.size() << '\n';
      for (const auto &package : scan.previous_packages) {
        std::cout << "  PREV " << package.id << " -> " << tts_host::path_to_utf8(package.package_path) << '\n';
      }

      std::cout << "Unsupported registry entries: " << scan.unsupported_entries.size() << '\n';
      for (const auto &entry : scan.unsupported_entries) {
        std::cout << "  BAD " << tts_host::path_to_utf8(entry.path) << " :: " << entry.reason << '\n';
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
        const auto installed = tts_host::catalogue_entry_package(entry, scan);
        std::cout << "  " << (installed ? "HAVE" : "GET ") << ' ' << entry.id << " ("
                  << entry.engine << ", " << join_languages(entry.languages) << ", "
                  << tts_host::describe_download_size(tts_host::catalogue_entry_size_bytes(entry)) << ")";
        if (installed) {
          std::cout << " [" << tts_host::describe_package_status(tts_host::package_status(*installed)) << ']';
        }
        std::cout << '\n';
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
                << tts_host::path_to_utf8(installed.package_path) << '\n';
    }

    // Manual model maintenance (docs/design/architecture.md#model-installation-and-updates).
    // A headless run holds no resident runner, so the package switch needs no guard.
    if (options->update_model_id.has_value()) {
      const auto scan = tts_host::scan_model_registry(document);
      const auto &entries = tts_host::model_catalogue();
      const auto found = std::find_if(entries.begin(), entries.end(),
                                      [&](const auto &entry) { return entry.id == *options->update_model_id; });
      if (found == entries.end()) {
        throw std::runtime_error("unknown catalogue entry id: " + *options->update_model_id);
      }
      std::cout << "Updating " << found->display_name << " (" << found->id << ", "
                << tts_host::describe_download_size(tts_host::catalogue_entry_size_bytes(*found)) << ")\n";
      const auto updated = tts_host::update_catalogue_package(
          *found, scan, document, tts_host::fetch_url_to_file,
          [](const tts_host::DownloadProgress &download_progress) {
            std::cout << "  " << download_progress.relative_path << ": " << download_progress.bytes_downloaded
                      << " / " << download_progress.bytes_total << " bytes\n";
          },
          nullptr);
      std::cout << "Updated " << updated.id << " in " << tts_host::path_to_utf8(updated.package_path) << '\n';
    }
    if (options->revert_model_id.has_value()) {
      tts_host::revert_model_package(*options->revert_model_id, tts_host::scan_model_registry(document), nullptr);
      std::cout << "Reverted " << *options->revert_model_id << '\n';
    }
    if (options->delete_previous_model_id.has_value()) {
      const auto removed = tts_host::delete_previous_model_versions(*options->delete_previous_model_id,
                                                                     tts_host::scan_model_registry(document));
      std::cout << "Deleted " << removed << " previous version(s) of " << *options->delete_previous_model_id << '\n';
    }
    if (options->remove_superseded_model_id.has_value()) {
      const auto removed = tts_host::remove_superseded_model_copies(*options->remove_superseded_model_id,
                                                                     tts_host::scan_model_registry(document));
      std::cout << "Removed " << removed << " superseded copy(ies) of " << *options->remove_superseded_model_id
                << '\n';
    }
    // Report only, never repair: the release build uses it to reject a staged
    // package that differs from the compiled catalogue
    // (docs/design/architecture.md#distribution).
    if (options->verify_model_id.has_value()) {
      const auto &entries = tts_host::model_catalogue();
      const auto found = std::find_if(entries.begin(), entries.end(),
                                      [&](const auto &entry) { return entry.id == *options->verify_model_id; });
      if (found == entries.end()) {
        throw std::runtime_error("unknown catalogue entry id: " + *options->verify_model_id);
      }
      const auto scan = tts_host::scan_model_registry(document);
      const auto installed = tts_host::catalogue_entry_package(*found, scan);
      const auto expected = tts_host::catalogue_entry_revision(*found);
      if (installed && installed->revision && *installed->revision != expected) {
        throw std::runtime_error("'" + found->id + "' in " + tts_host::path_to_utf8(installed->package_path) +
                                 " is at revision " + *installed->revision + "; the catalogue has " + expected);
      }
      const auto verification = tts_host::verify_catalogue_package(*found, scan, nullptr);
      if (!verification.damaged.empty()) {
        std::string damaged;
        for (const auto &file : verification.damaged) {
          damaged += "\n  " + file.relative_path + (file.missing ? " (missing)" : " (differs)");
        }
        throw std::runtime_error("'" + found->id + "' in " + tts_host::path_to_utf8(verification.package_path) +
                                 " differs from the catalogue:" + damaged);
      }
      std::cout << "Verified " << found->id << ": " << verification.files_checked
                << " file(s) match the catalogue in " << tts_host::path_to_utf8(verification.package_path)
                << '\n';
    }

    if (options->synthesize_text.has_value() || options->use_stdin_text || options->use_clipboard_text) {
      run_synthesis(*options, document, tts_host::path_from_utf8(argv[0]));
    }

    return EXIT_SUCCESS;
  } catch (const std::exception &error) {
    report_fatal_error(error.what(), !options->headless);
    return EXIT_FAILURE;
  }
}

#ifdef _WIN32
// A GUI subsystem prevents Explorer/login launches from creating a console.
// CLI invocations attach to the caller's console while preserving inherited
// pipe/file handles used by CTest and command-line redirection.
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
  int argc = 0;
  auto wide_argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  if (!wide_argv) return EXIT_FAILURE;
  std::vector<std::string> arguments;
  arguments.reserve(argc);
  bool cli = false;
  for (int index = 0; index < argc; ++index) {
    arguments.push_back(tts_host::path_to_utf8(std::filesystem::path(wide_argv[index])));
    if (arguments.back() == "--headless" || arguments.back() == "--help" ||
        arguments.back() == "-h") cli = true;
  }
  LocalFree(wide_argv);
  if (cli) AttachConsole(ATTACH_PARENT_PROCESS);
  const struct { DWORD id; int fd; FILE *stream; } streams[] = {
      {STD_INPUT_HANDLE, 0, stdin}, {STD_OUTPUT_HANDLE, 1, stdout},
      {STD_ERROR_HANDLE, 2, stderr}};
  for (const auto &stream : streams) {
    const auto existing = _get_osfhandle(stream.fd);
    if (existing != -1 && existing != -2) {
      SetStdHandle(stream.id, reinterpret_cast<HANDLE>(existing));
      clearerr(stream.stream);
      continue;
    }
    const auto handle = GetStdHandle(stream.id);
    if (!handle || handle == INVALID_HANDLE_VALUE) continue;
    HANDLE copy = nullptr;
    if (!DuplicateHandle(GetCurrentProcess(), handle, GetCurrentProcess(), &copy,
                         0, FALSE, DUPLICATE_SAME_ACCESS)) continue;
    const auto fd = _open_osfhandle(reinterpret_cast<intptr_t>(copy),
                                  stream.fd == 0 ? _O_RDONLY : _O_WRONLY);
    if (fd < 0) { CloseHandle(copy); continue; }
    if (fd != stream.fd) {
      _dup2(fd, stream.fd);
      _close(fd);
    }
    // Runner launch uses Win32 standard handles, so keep them in sync with
    // the CRT after replacing descriptors. _dup2 may close the old handle.
    SetStdHandle(stream.id, reinterpret_cast<HANDLE>(_get_osfhandle(stream.fd)));
    clearerr(stream.stream);
  }
  std::vector<char *> argv;
  argv.reserve(argc);
  for (auto &argument : arguments) argv.push_back(argument.data());
  return run_main(argc, argv.data());
}
#else
int main(int argc, char **argv) { return run_main(argc, argv); }
#endif
