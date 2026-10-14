#include "tts_host/runner_audio_output.hpp"

#include <charconv>
#include <cerrno>
#include <cstdlib>
#include <string>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace tts_host {
namespace {

std::uintptr_t parse_inherited_handle(const char *environment_variable) {
  const auto *value = std::getenv(environment_variable);
  if (value == nullptr || *value == '\0') {
    throw RunnerProtocolError(std::string("runner audio endpoint is missing ") + environment_variable);
  }

  std::uintptr_t handle = 0;
  const auto *end = value + std::char_traits<char>::length(value);
  const auto [parsed_end, error] = std::from_chars(value, end, handle);
  if (error != std::errc{} || parsed_end != end || handle == 0) {
    throw RunnerProtocolError(std::string("runner audio endpoint has an invalid ") +
                              environment_variable);
  }
  return handle;
}

}  // namespace

RunnerAudioOutput::RunnerAudioOutput(std::uintptr_t native_handle) : native_handle_(native_handle) {}

RunnerAudioOutput RunnerAudioOutput::open_inherited() {
#ifdef _WIN32
  return RunnerAudioOutput(parse_inherited_handle("TTS_HOST_AUDIO_HANDLE"));
#else
  return RunnerAudioOutput(parse_inherited_handle("TTS_HOST_AUDIO_FD"));
#endif
}

void RunnerAudioOutput::write_frame(const RunnerAudioFrame &frame) const {
  const auto bytes = frame_runner_audio_message(frame);
  std::size_t offset = 0;

#ifdef _WIN32
  const auto handle = reinterpret_cast<HANDLE>(native_handle_);
  while (offset < bytes.size()) {
    DWORD written = 0;
    const auto remaining = static_cast<DWORD>(bytes.size() - offset);
    if (!WriteFile(handle, bytes.data() + offset, remaining, &written, nullptr) || written == 0) {
      throw RunnerProtocolError("could not write runner audio frame (Windows error " +
                                std::to_string(GetLastError()) + ")");
    }
    offset += written;
  }
#else
  const auto file_descriptor = static_cast<int>(native_handle_);
  while (offset < bytes.size()) {
    const auto written = write(file_descriptor, bytes.data() + offset, bytes.size() - offset);
    if (written < 0) {
      if (errno == EINTR) {
        continue;
      }
      throw RunnerProtocolError("could not write runner audio frame (errno " +
                                std::to_string(errno) + ")");
    }
    if (written == 0) {
      throw RunnerProtocolError("could not write runner audio frame: pipe closed");
    }
    offset += static_cast<std::size_t>(written);
  }
#endif
}

}  // namespace tts_host
