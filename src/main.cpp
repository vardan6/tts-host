#include "tts_host/config_loader.hpp"
#include "tts_host/model_registry.hpp"
#include "tts_host/runner_launcher.hpp"
#include "tts_host/runner_protocol.hpp"
#include "tts_host/wav_writer.hpp"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

namespace {

std::filesystem::path default_runner_path(const std::filesystem::path &argv0) {
  std::error_code ec;
  const auto absolute = std::filesystem::absolute(argv0, ec);
  const auto exe_dir = (ec || argv0.empty()) ? std::filesystem::current_path() : absolute.parent_path();
#ifdef _WIN32
  return exe_dir / "tts-host-stub-runner.exe";
#else
  return exe_dir / "tts-host-stub-runner";
#endif
}

void synthesize_to_wav(const tts_host::CliOptions &options, const std::filesystem::path &argv0) {
  const auto runner_path =
      options.runner_path_override.value_or(default_runner_path(argv0));

  tts_host::RunnerSession session(runner_path);

  const auto initialize_response =
      session.send_request(tts_host::make_runner_initialize_request(1));
  const auto initialize_result = tts_host::parse_runner_initialize_response(initialize_response);
  if (initialize_result.protocol_version != tts_host::kRunnerProtocolVersion) {
    throw std::runtime_error("runner reported an unsupported protocol version");
  }

  const auto synthesize_response = session.send_request(
      tts_host::make_runner_synthesize_request(2, *options.synthesize_text));
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

    if (options->synthesize_text.has_value()) {
      synthesize_to_wav(*options, std::filesystem::path(argv[0]));
    }

    return EXIT_SUCCESS;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return EXIT_FAILURE;
  }
}
