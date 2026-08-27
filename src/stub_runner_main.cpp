#include "tts_host/runner_audio_output.hpp"
#include "tts_host/runner_protocol.hpp"
#include "tts_host/stub_runner.hpp"

#include <array>
#include <cstdlib>
#include <iostream>

int main() {
  try {
    tts_host::RunnerControlMessageParser parser;
    std::array<char, 4096> buffer{};
    while (std::cin.read(buffer.data(), static_cast<std::streamsize>(buffer.size())) || std::cin.gcount() > 0) {
      for (const auto &message : parser.push(std::string_view(buffer.data(), std::cin.gcount()))) {
        nlohmann::json response;
        if (message.value("method", "") == "synthesize") {
          const auto frame = tts_host::make_stub_runner_synthesis_frame();
          tts_host::RunnerAudioOutput::open_inherited().write_frame(frame);
          response = tts_host::handle_stub_runner_synthesize_message(message);
        } else {
          response = tts_host::handle_stub_runner_control_message(message);
        }
        std::cout << tts_host::frame_runner_control_message(response);
        std::cout.flush();
      }
    }
    return EXIT_SUCCESS;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return EXIT_FAILURE;
  }
}
