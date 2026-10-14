#include "tts_host/runner_audio_output.hpp"
#include "tts_host/runner_control_io.hpp"
#include "tts_host/stub_runner.hpp"
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <thread>

void lifecycle(const std::string &event) {
  if (const auto path = std::getenv("TTS_TEST_RUNNER_LIFECYCLE"))
    std::ofstream(path, std::ios::app) << event << '\n';
}
void hang() { for (;;) std::this_thread::sleep_for(std::chrono::seconds(1)); }
int main() {
  lifecycle("start");
  const std::string mode = std::getenv("TTS_TEST_RUNNER_MODE");
  if (mode == "close-input") return 9;
  if (mode == "silent" || mode == "blocked-write") hang();
  tts_host::RunnerControlMessageParser parser;
  bool loaded = false;
  while (true) {
    const auto bytes = tts_host::read_some_stdin();
    if (bytes.empty()) { if (mode == "stuck-exit") hang(); return 0; }
    for (const auto &request : parser.push(
        std::string_view(reinterpret_cast<const char *>(bytes.data()), bytes.size()))) {
      const auto method = request.value("method", "");
      lifecycle(method);
      nlohmann::json response;
      if (method == "load") {
        if (mode == "load-hang") hang();
        loaded = true;
        response = tts_host::handle_stub_runner_load_message(request);
      } else if (method == "synthesize") {
        if (!loaded && std::getenv("TTS_TEST_REQUIRE_LOAD")) return 8;
        const auto text = request.at("params").at("text").get<std::string>();
        bool fail = text.starts_with("fail");
        if (mode == "gate-lookahead" && text.starts_with("Second")) {
          const std::filesystem::path gate(std::getenv("TTS_TEST_RUNNER_MARKER"));
          std::ofstream(gate.string() + ".ready") << "synthesizing";
          while (!std::filesystem::exists(gate))
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        if (mode.ends_with("-once") && fail) {
          const std::filesystem::path marker(std::getenv("TTS_TEST_RUNNER_MARKER"));
          fail = !std::filesystem::exists(marker);
          if (fail) std::ofstream(marker) << "failed";
        }
        if (mode.starts_with("death") && fail) return 9;
        if (mode.starts_with("hang") && fail) hang();
        response = tts_host::handle_stub_runner_synthesize_message(request);
        tts_host::write_stdout(tts_host::frame_runner_control_message(response));
        auto frame = tts_host::make_stub_runner_synthesis_frame();
        if ((mode.starts_with("partial") || mode == "audio-hang") && fail) {
          frame.flags = 0;
          tts_host::RunnerAudioOutput::open_inherited().write_frame(frame);
          if (mode == "audio-hang") hang();
          return 9;
        }
        tts_host::RunnerAudioOutput::open_inherited().write_frame(frame);
        continue;
      } else response = tts_host::handle_stub_runner_control_message(request);
      const auto framed = tts_host::frame_runner_control_message(response);
      if (mode == "drip-control") {
        for (char byte : framed) {
          tts_host::write_stdout(std::string(1, byte));
          std::this_thread::sleep_for(std::chrono::milliseconds(30));
        }
      } else tts_host::write_stdout(framed);
    }
  }
}
