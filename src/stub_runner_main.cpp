#include "tts_host/process_stats.hpp"
#include "tts_host/runner_audio_output.hpp"
#include "tts_host/runner_control_io.hpp"
#include "tts_host/runner_protocol.hpp"
#include "tts_host/stub_runner.hpp"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>

int main() {
  try {
    tts_host::RunnerControlMessageParser parser;
    double last_time_to_first_chunk_ms = 0.0;
    std::uint64_t last_sample_count = 0;
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
          const auto synthesis_start = std::chrono::steady_clock::now();
          const auto frame = tts_host::make_stub_runner_synthesis_frame();
          response = tts_host::handle_stub_runner_synthesize_message(message);
          last_time_to_first_chunk_ms =
              std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() -
                                                         synthesis_start)
                  .count();
          last_sample_count = frame.sample_count;
          // See kokoro_runner_main.cpp: control response must precede the
          // audio frame, or a large frame can deadlock both processes.
          tts_host::write_stdout(tts_host::frame_runner_control_message(response));
          tts_host::RunnerAudioOutput::open_inherited().write_frame(frame);
          continue;
        } else if (method == "load") {
          response = tts_host::handle_stub_runner_load_message(message);
        } else if (method == "stats") {
          const auto request = tts_host::parse_runner_stats_request(message);
          response = tts_host::make_runner_stats_response(
              request, tts_host::peak_resident_set_size_bytes(), 0, last_time_to_first_chunk_ms,
              last_sample_count);
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
