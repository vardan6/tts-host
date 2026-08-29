#include "tts_host/runner_audio_output.hpp"
#include "tts_host/runner_control_io.hpp"
#include "tts_host/runner_protocol.hpp"
#include "tts_host/stub_runner.hpp"

#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>

int main() {
  try {
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
          const auto frame = tts_host::make_stub_runner_synthesis_frame();
          response = tts_host::handle_stub_runner_synthesize_message(message);
          // See kokoro_runner_main.cpp: control response must precede the
          // audio frame, or a large frame can deadlock both processes.
          tts_host::write_stdout(tts_host::frame_runner_control_message(response));
          tts_host::RunnerAudioOutput::open_inherited().write_frame(frame);
          continue;
        } else if (method == "load") {
          response = tts_host::handle_stub_runner_load_message(message);
        } else {
          response = tts_host::handle_stub_runner_control_message(message);
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
