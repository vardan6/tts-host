#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "tts_host/playback_controller.hpp"

namespace tts_host {

// The /v1/playback reply: state, positionSeconds, generatedSeconds, error.
std::string playback_status_json(const PlaybackStatus &status);

class LocalApiServer {
 public:
  using SelectionHandler = std::function<void(std::string)>;
  // Receives a validated status/pause/resume/stop command and returns the
  // playback status, which the server sends as JSON.
  using PlaybackHandler = std::function<PlaybackStatus(const std::string &)>;

  LocalApiServer(std::string host, unsigned short port, std::vector<std::string> allowed_origins,
                 SelectionHandler on_selection, PlaybackHandler on_playback = {});
  ~LocalApiServer();
  LocalApiServer(const LocalApiServer &) = delete;
  LocalApiServer &operator=(const LocalApiServer &) = delete;
  unsigned short port() const;

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace tts_host
