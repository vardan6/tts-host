#pragma once

#include "tts_host/runner_protocol.hpp"

#include <cstdint>

namespace tts_host {

class RunnerAudioOutput {
 public:
  static RunnerAudioOutput open_inherited();

  void write_frame(const RunnerAudioFrame &frame) const;

 private:
  explicit RunnerAudioOutput(std::uintptr_t native_handle);

  std::uintptr_t native_handle_;
};

}  // namespace tts_host
