#include "tts_host/runner_control_io.hpp"

#include "tts_host/runner_protocol.hpp"

#include <array>

#ifdef _WIN32
#include <windows.h>
#else
#include <cerrno>
#include <unistd.h>
#endif

namespace tts_host {

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
      throw RunnerProtocolError("runner could not write to stdout");
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
      throw RunnerProtocolError("runner could not write to stdout");
    }
    offset += static_cast<std::size_t>(written);
  }
#endif
}

}  // namespace tts_host
