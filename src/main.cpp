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
#include "tts_host/playback_controller.hpp"
#include "tts_host/playback_sink.hpp"
#include "tts_host/runner_launcher.hpp"
#include "tts_host/recovering_runner.hpp"
#include "tts_host/runner_protocol.hpp"
#include "tts_host/settings_window.hpp"
#include "tts_host/startup_registration.hpp"
#include "tts_host/text_normalizer.hpp"
#include "tts_host/text_splitter.hpp"
#include "tts_host/tray_icon.hpp"
#include "tts_host/wav_writer.hpp"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstdlib>
#include <exception>
#include <deque>
#include <filesystem>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
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
    return {default_runner_path(argv0), std::nullopt, std::nullopt, {}, {}, {}, {}};
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
    auto selected = resolve_runner_selection_for_model(model_id, document, argv0);
    if (profile.contains("idleUnloadSeconds"))
      selected.idle_override = std::chrono::seconds(profile.at("idleUnloadSeconds").get<int>());
    return selected;
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
      auto selected = resolve_runner_selection_for_model(fallback_model_id, document, argv0);
      if (fallback_profile.contains("idleUnloadSeconds"))
        selected.idle_override = std::chrono::seconds(fallback_profile.at("idleUnloadSeconds").get<int>());
      return selected;
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
    return {*options.runner_path_override, std::nullopt, std::nullopt, {}, {}, {}, {}};
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

  const auto selection = resolve_runner_selection(options, spoken_text, document, argv0);

  if (sessions) {
    const tts_host::ModelRunnerSelection model{selection.model_id, selection.display_name,
        selection.engine, selection.runner_path, selection.model_path.value_or(std::filesystem::path{}),
        selection.voice_path};
    auto lease = sessions->acquire(model,
        [control] { return control && control->cancelled(); }, selection.idle_override);
    tts_host::run_synthesis_pipeline(spoken_text, options, document, selection,
                                     control, playback_controller, nullptr, {}, &lease.runner());
  } else {
    tts_host::run_synthesis_pipeline(spoken_text, options, document, selection,
                                     control, playback_controller);
  }
}

// Keeps tray requests asynchronous from the Win32 message loop. A request
// normally cancels the active utterance and replaces pending work; the Queue
// clipboard command is the explicit opt-in path that preserves it.
class TraySpeechScheduler {
 public:
  TraySpeechScheduler(tts_host::ConfigDocument document, std::filesystem::path argv0)
      : config_(std::move(document)), argv0_(std::move(argv0)),
        sessions_(executable_dir(argv0_)) {
    tts_host::apply_playback_settings(*config_.current(), playback_controller_);
    try { sessions_.load_eager(*config_.current()); }
    catch (const std::exception &error) {
      std::cerr << "Eager model load failed: " << error.what() << '\n';
    }
    worker_ = std::thread(&TraySpeechScheduler::run, this);
  }

  ~TraySpeechScheduler() {
    {
      std::lock_guard lock(mutex_);
      stopping_ = true;
      playback_controller_.stop();
      pending_.clear();
    }
    condition_.notify_one();
    worker_.join();
  }

  void submit(std::string text, tts_host::SpeechQueueMode queue_mode) {
    {
      std::lock_guard lock(mutex_);
      if (queue_mode == tts_host::SpeechQueueMode::Interrupt) {
        pending_.clear();
        playback_controller_.stop();
      }
      pending_.push_back(std::move(text));
    }
    condition_.notify_one();
  }

  tts_host::ModelSessionManager &sessions() { return sessions_; }

  tts_host::PlaybackController &playback_controller() { return playback_controller_; }

  // Called on the tray thread after Settings edits its own document. The next
  // request sees the copy; an in-flight request keeps the snapshot it took.
  void publish_config(const tts_host::ConfigDocument &document) {
    tts_host::apply_playback_settings(document, playback_controller_);
    const auto previous = config_.current();
    const bool profiles_changed = previous->value.at("profiles") != document.value.at("profiles") ||
        previous->value.at("languageDefaults") != document.value.at("languageDefaults");
    config_.publish(document);
    if (profiles_changed) {
      { std::lock_guard lock(mutex_); eager_pending_ = true; }
      condition_.notify_one();
    }
  }

  std::string playback_command(const std::string &command) {
    if (command == "pause") playback_controller_.pause();
    if (command == "resume") playback_controller_.resume();
    if (command == "stop") {
      std::lock_guard lock(mutex_);
      pending_.clear();
      playback_controller_.stop();
    }
    const auto status = playback_controller_.status();
    const char *state = "idle";
    switch (status.state) {
      case tts_host::PlaybackState::Idle: state = "idle"; break;
      case tts_host::PlaybackState::Preparing: state = "preparing"; break;
      case tts_host::PlaybackState::Playing: state = "playing"; break;
      case tts_host::PlaybackState::Paused: state = "paused"; break;
      case tts_host::PlaybackState::Stopping: state = "stopping"; break;
      case tts_host::PlaybackState::Error: state = "error"; break;
    }
    return nlohmann::json{{"state", state}, {"positionSeconds", status.position_seconds},
                          {"generatedSeconds", status.generated_seconds},
                          {"error", status.error}}.dump();
  }

 private:
  void run() {
    while (true) {
      std::string text;
      std::shared_ptr<tts_host::PlaybackControl> control;
      std::shared_ptr<const tts_host::ConfigDocument> config;
      {
        std::unique_lock lock(mutex_);
        condition_.wait_for(lock, std::chrono::seconds(5),
                            [this] { return stopping_ || !pending_.empty() || eager_pending_; });
        if (stopping_) {
          return;
        }
        if (pending_.empty()) {
          config = config_.current();
          const bool eager = std::exchange(eager_pending_, false);
          lock.unlock();
          if (eager) {
            try {
              sessions_.load_eager(*config, [this] {
                std::lock_guard lock(mutex_);
                return stopping_ || !pending_.empty();
              });
            } catch (const tts_host::RunnerCancelled &) {
              // A new read or Quit cancels background warming immediately.
            } catch (const std::exception &error) {
              std::cerr << "Eager model load failed: " << error.what() << '\n';
            }
          }
          sessions_.unload_if_idle(std::chrono::seconds(
              config->value.at("modelRegistry").at("idleUnloadSeconds").get<int>()));
          continue;
        }
        text = std::move(pending_.front());
        pending_.pop_front();
        control = playback_controller_.begin();
        config = config_.current();
      }

      try {
        tts_host::CliOptions options;
        options.synthesize_text = std::move(text);
        options.play_audio = true;
        run_synthesis(options, *config, argv0_, control.get(), &playback_controller_, &sessions_);
      } catch (const tts_host::RunnerCancelled &) {
        // Stop/replacement terminates runner I/O without retry or an error state.
      } catch (const std::exception &error) {
        playback_controller_.fail(control, error.what());
        std::cerr << "Tray speech request failed: " << error.what() << '\n';
      }

      playback_controller_.finish(control);
    }
  }

  tts_host::ConfigSnapshot config_;
  std::filesystem::path argv0_;
  tts_host::ModelSessionManager sessions_;
  std::mutex mutex_;
  std::condition_variable condition_;
  std::deque<std::string> pending_;
  tts_host::PlaybackController playback_controller_;
  bool stopping_ = false;
  bool eager_pending_ = false;
  std::thread worker_;
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

#ifdef _WIN32
    if (!options->headless) {
      const bool launch_on_login =
          document.value.value("startup", nlohmann::json::object()).value("launchOnLogin", true);
      try {
        tts_host::set_start_at_login(launch_on_login, document.paths.config_path);
      } catch (const std::exception &error) {
        std::cerr << "Could not update the Windows login startup entry: " << error.what() << '\n';
      }
    }
#endif

    if (!options->headless) {
      if (options->settings_window) {
        // Settings window (docs/design/architecture.md#desktop-integration):
        // opens independently of the tray, blocks until closed. Windows only
        // in this slice -- see docs/adr/0007-native-ui-per-platform.md.
        tts_host::run_settings_window(document, executable_dir(tts_host::path_from_utf8(argv[0])));
        return EXIT_SUCCESS;
      }

      // Tray mode (docs/design/architecture.md#desktop-integration): blocks
      // until the user chooses Quit. Windows only in this slice -- see
      // docs/adr/0007-native-ui-per-platform.md.
      TraySpeechScheduler tray_speech_scheduler(document, tts_host::path_from_utf8(argv[0]));
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
          [&tray_speech_scheduler](std::string text) {
            tray_speech_scheduler.submit(std::move(text), tts_host::SpeechQueueMode::Interrupt);
          },
          [&tray_speech_scheduler](const std::string &command) {
            return tray_speech_scheduler.playback_command(command);
          });
      std::cout << "Local API listening on " << server.at("host") << ':' << server.at("port")
                << '\n';
      tts_host::run_tray_icon(
          document, executable_dir(tts_host::path_from_utf8(argv[0])),
          [&tray_speech_scheduler](const std::string &text, tts_host::SpeechQueueMode queue_mode) {
            tray_speech_scheduler.submit(text, queue_mode);
          }, tray_speech_scheduler.playback_controller(),
          [&tray_speech_scheduler](const tts_host::ConfigDocument &edited) {
            tray_speech_scheduler.publish_config(edited);
          }, &tray_speech_scheduler.sessions());
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
                  << join_languages(package.languages) << ") -> " << tts_host::path_to_utf8(package.manifest_path)
                  << '\n';
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
                << tts_host::path_to_utf8(installed.package_path) << '\n';
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
