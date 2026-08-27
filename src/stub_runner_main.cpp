#include "tts_host/runner_audio_output.hpp"
#include "tts_host/runner_protocol.hpp"
#include "tts_host/stub_runner.hpp"

#include <array>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <cerrno>
#include <unistd.h>
#endif

namespace {

// The control channel is a pipe, not a regular file: buffered iostream reads
// (std::cin.read) block until the requested count is fully satisfied rather
// than returning whatever is already available, which deadlocks against a
// host that is waiting on a reply to a short request. Read raw, short reads
// instead.
std::vector<std::uint8_t> read_some_stdin() {
  std::array<char, 4096> buffer{};
#ifdef _WIN32
  const HANDLE handle = GetStdHandle(STD_INPUT_HANDLE);
  DWORD bytes_read = 0;
  if (!ReadFile(handle, buffer.data(), static_cast<DWORD>(buffer.size()), &bytes_read, nullptr)) {
    return {};
  }
  return std::vector<std::uint8_t>(buffer.begin(), buffer.begin() + bytes_read);
#else
  while (true) {
    const auto bytes_read = read(STDIN_FILENO, buffer.data(), buffer.size());
    if (bytes_read < 0) {
      if (errno == EINTR) {
        continue;
      }
      return {};
    }
    return std::vector<std::uint8_t>(buffer.begin(), buffer.begin() + bytes_read);
  }
#endif
}

void write_stdout(const std::string &bytes) {
  std::size_t offset = 0;
#ifdef _WIN32
  const HANDLE handle = GetStdHandle(STD_OUTPUT_HANDLE);
  while (offset < bytes.size()) {
    DWORD written = 0;
    if (!WriteFile(handle, bytes.data() + offset, static_cast<DWORD>(bytes.size() - offset),
                   &written, nullptr) ||
        written == 0) {
      throw tts_host::RunnerProtocolError("stub runner could not write to stdout");
    }
    offset += written;
  }
#else
  while (offset < bytes.size()) {
    const auto written = write(STDOUT_FILENO, bytes.data() + offset, bytes.size() - offset);
    if (written < 0) {
      if (errno == EINTR) {
        continue;
      }
      throw tts_host::RunnerProtocolError("stub runner could not write to stdout");
    }
    offset += static_cast<std::size_t>(written);
  }
#endif
}

}  // namespace

int main() {
  try {
    tts_host::RunnerControlMessageParser parser;
    while (true) {
      const auto bytes = read_some_stdin();
      if (bytes.empty()) {
        break;
      }
      for (const auto &message :
          parser.push(std::string_view(reinterpret_cast<const char *>(bytes.data()), bytes.size()))) {
        nlohmann::json response;
        if (message.value("method", "") == "synthesize") {
          const auto frame = tts_host::make_stub_runner_synthesis_frame();
          tts_host::RunnerAudioOutput::open_inherited().write_frame(frame);
          response = tts_host::handle_stub_runner_synthesize_message(message);
        } else {
          response = tts_host::handle_stub_runner_control_message(message);
        }
        write_stdout(tts_host::frame_runner_control_message(response));
      }
    }
    return EXIT_SUCCESS;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return EXIT_FAILURE;
  }
}
