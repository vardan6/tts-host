#include "tts_host/kokoro_runner.hpp"
#include "tts_host/runner_audio_output.hpp"
#include "tts_host/runner_control_io.hpp"
#include "tts_host/runner_protocol.hpp"

#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>

int main() {
  try {
    tts_host::KokoroOnnxRunner runner;
    tts_host::RunnerControlMessageParser parser;
    while (true) {
      const auto bytes = tts_host::read_some_stdin();
      if (bytes.empty()) {
        break;
      }
      for (const auto &message :
          parser.push(std::string_view(reinterpret_cast<const char *>(bytes.data()), bytes.size()))) {
        nlohmann::json response;
        const auto method = message.value("method", "");
        if (method == "synthesize") {
          const auto request = tts_host::parse_runner_synthesize_request(message);
          const auto frame = runner.run_synthesis(request.text);
          tts_host::RunnerAudioOutput::open_inherited().write_frame(frame);
          response = runner.make_synthesize_response(message, frame);
        } else if (method == "load") {
          response = runner.handle_load_message(message);
        } else {
          response = runner.handle_control_message(message);
        }
        tts_host::write_stdout(tts_host::frame_runner_control_message(response));
      }
    }
    return EXIT_SUCCESS;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return EXIT_FAILURE;
  }
}
