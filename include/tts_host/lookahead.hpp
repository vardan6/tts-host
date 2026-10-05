#pragma once

#include <chrono>
#include <future>

namespace tts_host {
// Only the synthesis task owns the runner. Its caller remains free to service
// retained playback/seek while one lookahead request is in flight.
template <typename Result, typename Pump, typename Cancel>
Result await_lookahead(std::future<Result> &pending, Pump pump, Cancel cancel) {
  try {
    while (pending.wait_for(std::chrono::milliseconds(10)) != std::future_status::ready) pump();
    pump();
    return pending.get();
  } catch (...) {
    // Stop outstanding work before future destruction joins its task.
    cancel();
    throw;
  }
}
}  // namespace tts_host
