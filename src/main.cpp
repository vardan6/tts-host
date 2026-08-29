#include "tts_host/clipboard.hpp"
#include "tts_host/config_loader.hpp"
#include "tts_host/model_registry.hpp"
#include "tts_host/runner_launcher.hpp"
#include "tts_host/runner_protocol.hpp"
#include "tts_host/text_normalizer.hpp"
#include "tts_host/wav_writer.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>

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

RunnerSelection resolve_runner_selection(const tts_host::CliOptions &options,
                                         const tts_host::ConfigDocument &document,
                                         const std::filesystem::path &argv0) {
  if (options.runner_path_override.has_value()) {
    return {*options.runner_path_override, std::nullopt, std::nullopt};
  }

  if (options.model_id.has_value()) {
    const auto scan = tts_host::scan_model_registry(document);
    const auto package = std::find_if(
        scan.discovered_packages.begin(), scan.discovered_packages.end(),
        [&](const tts_host::ModelPackageCandidate &candidate) {
          return candidate.id == *options.model_id;
        });
    if (package == scan.discovered_packages.end()) {
      throw std::runtime_error("Unknown model id: " + *options.model_id);
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

  return {default_runner_path(argv0), std::nullopt, std::nullopt};
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

void synthesize_to_wav(const tts_host::CliOptions &options, const tts_host::ConfigDocument &document,
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

  const auto synthesize_response =
      session.send_request(tts_host::make_runner_synthesize_request(3, spoken_text));
  const auto synthesize_result = tts_host::parse_runner_synthesize_response(synthesize_response);

  const auto frames = session.read_audio_stream_until_end();
  std::vector<std::uint8_t> pcm_payload;
  for (const auto &frame : frames) {
    pcm_payload.insert(pcm_payload.end(), frame.payload.begin(), frame.payload.end());
  }

  const auto exit_code = session.finish();
  if (exit_code != 0) {
    throw std::runtime_error("runner process exited with code " + std::to_string(exit_code));
  }

  tts_host::write_wav_file(*options.output_path, synthesize_result.sample_rate_hz,
                           static_cast<std::uint16_t>(synthesize_result.channels), 16, pcm_payload);

  std::cout << "Synthesized " << synthesize_result.total_sample_frames << " sample frames to "
            << options.output_path->string() << '\n';
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
      synthesize_to_wav(*options, document, std::filesystem::path(argv[0]));
    }

    return EXIT_SUCCESS;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return EXIT_FAILURE;
  }
}
