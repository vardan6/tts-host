#include "tts_host/speech_queue.hpp"

#include <chrono>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>

#include "tts_host/runner_launcher.hpp"

namespace {

using namespace std::chrono_literals;
using tts_host::SpeechQueueMode;

void require(bool condition, const std::string &message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

tts_host::ConfigDocument config(const std::string &model = "kokoro") {
  tts_host::ConfigDocument document;
  document.value = {
      {"audio", nlohmann::json::object()},
      {"modelRegistry", {{"idleUnloadSeconds", 600}}},
      {"profiles", {{"fast", {{"model", model}, {"loadPolicy", "eager"}}}}},
      {"languageDefaults", {{"en", "fast"}}},
  };
  return document;
}

// Records every call; speech and warming can block until released or
// cancelled, and one text can be made to fail.
class FakeEngine final : public tts_host::SpeechEngine {
 public:
  void speak(const std::string &text, const tts_host::ConfigDocument &config, tts_host::PlaybackControl &control,
             tts_host::PlaybackController &) override {
    {
      std::lock_guard lock(mutex_);
      started.push_back(text);
      models.push_back(config.value.at("profiles").at("fast").at("model").get<std::string>());
    }
    changed_.notify_all();
    if (text == fail_text) throw std::runtime_error("engine failed");
    for (;;) {
      if (control.cancelled()) {
        record([&] { cancelled_speech.push_back(text); });
        throw tts_host::RunnerCancelled();
      }
      {
        std::lock_guard lock(mutex_);
        if (!block_speech) break;
      }
      std::this_thread::sleep_for(1ms);
    }
    record([&] { spoken.push_back(text); });
  }

  void warm(const tts_host::ConfigDocument &, const std::function<bool()> &cancelled) override {
    record([&] { warm_cancellable.push_back(static_cast<bool>(cancelled)); });
    for (;;) {
      if (cancelled && cancelled()) {
        record([&] { ++cancelled_warms; });
        throw tts_host::RunnerCancelled();
      }
      {
        std::lock_guard lock(mutex_);
        if (!block_warm) return;
      }
      std::this_thread::sleep_for(1ms);
    }
  }

  void unload_if_idle(const tts_host::ConfigDocument &) override {
    record([&] { ++unloads; });
  }

  template <typename Predicate>
  void wait(Predicate predicate, const std::string &message) {
    std::unique_lock lock(mutex_);
    require(changed_.wait_for(lock, 5s, predicate), message);
  }

  template <typename Change>
  void record(Change change) {
    {
      std::lock_guard lock(mutex_);
      change();
    }
    changed_.notify_all();
  }

  // Guarded by mutex_; tests read them inside wait() or record().
  std::vector<std::string> started;
  std::vector<std::string> spoken;
  std::vector<std::string> cancelled_speech;
  std::vector<std::string> models;
  std::vector<bool> warm_cancellable;
  int cancelled_warms = 0;
  int unloads = 0;
  bool block_speech = false;
  bool block_warm = false;
  std::string fail_text;

 private:
  std::mutex mutex_;
  std::condition_variable changed_;
};

// The worker finishes an utterance after the engine returns, so poll.
tts_host::PlaybackStatus wait_for_state(const tts_host::SpeechQueue &queue, tts_host::PlaybackState state) {
  for (int attempt = 0; attempt < 5000 && queue.status().state != state; ++attempt) {
    std::this_thread::sleep_for(1ms);
  }
  return queue.status();
}

bool contains(const std::vector<std::string> &items, const std::string &item) {
  for (const auto &entry : items) {
    if (entry == item) return true;
  }
  return false;
}

void test_interrupt_replaces_the_active_and_pending_reads() {
  FakeEngine engine;
  engine.block_speech = true;
  tts_host::SpeechQueue queue(config(), engine, 10ms);
  queue.submit("first", SpeechQueueMode::Interrupt, "Clipboard");
  engine.wait([&] { return contains(engine.started, "first"); }, "the first read starts");
  queue.submit("second", SpeechQueueMode::Enqueue, "Clipboard");
  queue.submit("third", SpeechQueueMode::Interrupt, "Clipboard");
  engine.record([&] { engine.block_speech = false; });
  engine.wait([&] { return contains(engine.spoken, "third"); }, "the interrupting read is spoken");
  engine.wait([&] { return engine.started == std::vector<std::string>{"first", "third"}; },
              "an interrupting read drops the pending one");
  engine.wait([&] { return engine.cancelled_speech == std::vector<std::string>{"first"}; },
              "an interrupting read stops the active one");
}

void test_enqueue_keeps_order() {
  FakeEngine engine;
  engine.block_speech = true;
  tts_host::SpeechQueue queue(config(), engine, 10ms);
  queue.submit("a", SpeechQueueMode::Interrupt, "Clipboard");
  engine.wait([&] { return contains(engine.started, "a"); }, "the first read starts");
  queue.submit("b", SpeechQueueMode::Enqueue, "Clipboard");
  queue.submit("c", SpeechQueueMode::Enqueue, "Clipboard");
  engine.record([&] { engine.block_speech = false; });
  engine.wait([&] { return engine.spoken == std::vector<std::string>{"a", "b", "c"}; },
              "queued reads follow the active one in order");
}

void test_stop_drops_pending_reads() {
  FakeEngine engine;
  engine.block_speech = true;
  tts_host::SpeechQueue queue(config(), engine, 10ms);
  queue.submit("a", SpeechQueueMode::Interrupt, "Clipboard");
  engine.wait([&] { return contains(engine.started, "a"); }, "the read starts");
  queue.submit("b", SpeechQueueMode::Enqueue, "Clipboard");
  queue.stop();
  engine.wait([&] { return contains(engine.cancelled_speech, "a"); }, "stop cancels the active read");
  engine.record([&] { engine.block_speech = false; });
  queue.submit("c", SpeechQueueMode::Interrupt, "Clipboard");
  engine.wait([&] { return contains(engine.spoken, "c"); }, "the queue speaks again after stop");
  engine.wait([&] { return !contains(engine.started, "b"); }, "stop drops pending reads");
  require(wait_for_state(queue, tts_host::PlaybackState::Idle).state == tts_host::PlaybackState::Idle,
          "a cancelled read is not an error");
}

void test_profile_change_rewarms_and_a_read_cancels_it() {
  FakeEngine engine;
  tts_host::SpeechQueue queue(config(), engine, 10ms);
  engine.wait([&] { return engine.warm_cancellable == std::vector<bool>{false}; },
              "the queue warms the eager model at start, uncancellable");

  auto audio_only = config();
  audio_only.value["audio"]["playbackSkipSeconds"] = 12;
  queue.publish_config(audio_only);
  engine.wait([&] { return engine.unloads >= 3; }, "the idle poll runs");
  engine.wait([&] { return engine.warm_cancellable.size() == 1; }, "a non-profile change does not rewarm");
  require(queue.playback_controller().seek_interval_seconds() == 12, "published playback settings apply");

  engine.record([&] { engine.block_warm = true; });
  queue.publish_config(config("qwen"));
  engine.wait([&] { return engine.warm_cancellable == std::vector<bool>{false, true}; },
              "a profile change rewarms in the background");
  queue.submit("read", SpeechQueueMode::Interrupt, "Clipboard");
  engine.wait([&] { return engine.cancelled_warms == 1; }, "a read cancels the warm-up");
  engine.wait([&] { return contains(engine.spoken, "read"); }, "the read is spoken after the cancelled warm-up");
  engine.wait([&] { return engine.models.back() == "qwen"; }, "the read uses the published config");
}

void test_failed_read_reports_an_error() {
  FakeEngine engine;
  engine.fail_text = "bad";
  tts_host::SpeechQueue queue(config(), engine, 10ms);
  queue.submit("bad", SpeechQueueMode::Interrupt, "Clipboard");
  engine.wait([&] { return contains(engine.started, "bad"); }, "the failing read starts");
  const auto status = wait_for_state(queue, tts_host::PlaybackState::Error);
  require(status.state == tts_host::PlaybackState::Error && status.error == "engine failed",
          "a failed read leaves its message in the status");
  queue.submit("good", SpeechQueueMode::Interrupt, "Clipboard");
  engine.wait([&] { return contains(engine.spoken, "good"); }, "the queue keeps serving after a failure");
}

}  // namespace

int main() {
  try {
    test_interrupt_replaces_the_active_and_pending_reads();
    test_enqueue_keeps_order();
    test_stop_drops_pending_reads();
    test_profile_change_rewarms_and_a_read_cancels_it();
    test_failed_read_reports_an_error();
  } catch (const std::exception &error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
  }
  std::cout << "speech queue tests passed\n";
  return 0;
}
