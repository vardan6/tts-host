#pragma once

#include "tts_host/runner_protocol.hpp"

#include <chrono>
#include <functional>
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

class RunnerCancelled : public RunnerLaunchError {
 public:
  RunnerCancelled() : RunnerLaunchError("runner operation cancelled") {}
};

struct RunnerDeadlines {
  std::chrono::milliseconds control{30000};
  std::chrono::milliseconds load{600000};
  std::chrono::milliseconds synthesis{600000};
  std::chrono::milliseconds shutdown{1000};
};

// Launches a runner process per docs/adr/0002-runner-protocol.md: a
// Content-Length-framed JSON-RPC control channel over the child's
// stdin/stdout, plus a host-created, one-way audio pipe whose write end the
// child inherits via TTS_HOST_AUDIO_FD/TTS_HOST_AUDIO_HANDLE.
class RunnerSession {
 public:
  explicit RunnerSession(const std::filesystem::path &runner_executable,
                         RunnerDeadlines deadlines = {},
                         std::function<bool()> cancelled = {});
  ~RunnerSession();

  RunnerSession(const RunnerSession &) = delete;
  RunnerSession &operator=(const RunnerSession &) = delete;

  // Sends a request and returns the runner's next control message. This
  // protocol version has no concurrent requests, so replies are matched by
  // arrival order rather than by id.
  nlohmann::json send_request(const nlohmann::json &request,
                              std::chrono::milliseconds timeout = std::chrono::milliseconds::zero());

  // Reads audio frames until one carries kRunnerAudioFrameFlagEndOfStream, or
  // the audio pipe closes.
  std::vector<RunnerAudioFrame> read_audio_stream_until_end();

  // Closes the control channel's write end (signals the runner to exit) and
  // waits for a bounded grace period, then kills a stuck process. Idempotent.
  // The destructor calls it if the caller has not.
  int finish();

 private:
  nlohmann::json receive_control_message();
  void close_control_input();
  void check_wait() const;
  std::vector<std::uint8_t> read_some(std::uintptr_t handle);
  void write_all(const std::string &bytes);
  RunnerDeadlines deadlines_;
  std::function<bool()> cancelled_;
  std::chrono::steady_clock::time_point operation_deadline_;

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
