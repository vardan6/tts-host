#pragma once

#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "tts_host/config_loader.hpp"
#include "tts_host/config_snapshot.hpp"
#include "tts_host/playback_controller.hpp"
#include "tts_host/tray_icon.hpp"

namespace tts_host {

// What the speech queue runs (docs/design/architecture.md#speech-queue). The
// Host's adapter wraps runner selection, the model session manager, and the
// synthesis pipeline; tests substitute an in-process fake.
class SpeechEngine {
 public:
  virtual ~SpeechEngine() = default;
  // Speaks one request under `config`, observing `control` for stop and
  // pause. Throws RunnerCancelled when stopped.
  virtual void speak(const std::string &text, const ConfigDocument &config, PlaybackControl &control,
                     PlaybackController &playback) = 0;
  // Loads the eager profile's model. `cancelled`, empty for the warm-up at
  // start, turns true once a read arrives or the queue stops; the engine then
  // throws RunnerCancelled.
  virtual void warm(const ConfigDocument &config, const std::function<bool()> &cancelled) = 0;
  // Releases a resident model unused for longer than `config` allows.
  virtual void unload_if_idle(const ConfigDocument &config) = 0;
};

// Serializes speech requests on one worker thread, off the tray's message
// loop. A request normally cancels the active utterance and replaces pending
// work; SpeechQueueMode::Enqueue keeps them. When nothing is pending, the
// worker warms the eager model after a profile change and polls for idle
// unload every `idle_poll`.
class SpeechQueue {
 public:
  // Applies the playback settings and warms the eager model before the worker
  // starts.
  SpeechQueue(ConfigDocument document, SpeechEngine &engine,
              std::chrono::milliseconds idle_poll = std::chrono::seconds(5));
  ~SpeechQueue();
  SpeechQueue(const SpeechQueue &) = delete;
  SpeechQueue &operator=(const SpeechQueue &) = delete;

  // `source` labels where the text came from for Now Playing.
  void submit(std::string text, SpeechQueueMode queue_mode, std::string source);
  // Stops the active utterance and drops pending ones.
  void stop();
  // Called on the tray thread after Settings edits its own document. The next
  // request sees the copy; an in-flight request keeps the snapshot it took.
  void publish_config(const ConfigDocument &document);
  PlaybackStatus status() const;
  PlaybackController &playback_controller() { return playback_controller_; }

 private:
  struct Request {
    std::string text;
    std::string source;
  };

  void run();

  ConfigSnapshot config_;
  SpeechEngine &engine_;
  std::chrono::milliseconds idle_poll_;
  mutable std::mutex mutex_;
  std::condition_variable condition_;
  std::deque<Request> pending_;
  PlaybackController playback_controller_;
  bool stopping_ = false;
  bool eager_pending_ = false;
  std::thread worker_;
};

}  // namespace tts_host
