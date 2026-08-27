#pragma once

#include "tts_host/runner_protocol.hpp"

#include <cstdint>
#include <deque>
#include <filesystem>
#include <stdexcept>
#include <vector>

#include <nlohmann/json.hpp>

namespace tts_host {

class RunnerLaunchError : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};

// Launches a runner process per docs/adr/0002-runner-protocol.md: a
// Content-Length-framed JSON-RPC control channel over the child's
// stdin/stdout, plus a host-created, one-way audio pipe whose write end the
// child inherits via TTS_HOST_AUDIO_FD/TTS_HOST_AUDIO_HANDLE.
class RunnerSession {
 public:
  explicit RunnerSession(const std::filesystem::path &runner_executable);
  ~RunnerSession();

  RunnerSession(const RunnerSession &) = delete;
  RunnerSession &operator=(const RunnerSession &) = delete;

  // Sends a request and returns the runner's next control message. This
  // protocol version has no concurrent requests, so replies are matched by
  // arrival order rather than by id.
  nlohmann::json send_request(const nlohmann::json &request);

  // Reads audio frames until one carries kRunnerAudioFrameFlagEndOfStream, or
  // the audio pipe closes.
  std::vector<RunnerAudioFrame> read_audio_stream_until_end();

  // Closes the control channel's write end (signals the runner to exit) and
  // waits for the process. Safe to call at most once; the destructor calls it
  // if the caller has not.
  int finish();

 private:
  nlohmann::json receive_control_message();
  void close_control_input();

  std::uintptr_t control_write_handle_;
  std::uintptr_t control_read_handle_;
  std::uintptr_t audio_read_handle_;
  std::uintptr_t process_handle_;

  bool control_input_closed_ = false;
  bool finished_ = false;

  RunnerControlMessageParser control_parser_;
  RunnerAudioFrameParser audio_parser_;
  std::deque<nlohmann::json> pending_control_messages_;
};

}  // namespace tts_host
