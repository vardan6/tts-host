#include "tts_host/speech_queue.hpp"

#include <exception>
#include <iostream>
#include <utility>

#include "tts_host/activity_log.hpp"
#include "tts_host/runner_launcher.hpp"

namespace tts_host {

namespace {

void report_warm_failure(const std::exception &error) {
  record_activity(std::string("Eager model load failed: ") + error.what());
  std::cerr << "Eager model load failed: " << error.what() << '\n';
}

}  // namespace

SpeechQueue::SpeechQueue(ConfigDocument document, SpeechEngine &engine, std::chrono::milliseconds idle_poll)
    : config_(std::move(document)), engine_(engine), idle_poll_(idle_poll) {
  apply_playback_settings(*config_.current(), playback_controller_);
  try {
    engine_.warm(*config_.current(), {});
  } catch (const std::exception &error) {
    report_warm_failure(error);
  }
  worker_ = std::thread(&SpeechQueue::run, this);
}

SpeechQueue::~SpeechQueue() {
  {
    std::lock_guard lock(mutex_);
    stopping_ = true;
    playback_controller_.stop();
    pending_.clear();
  }
  condition_.notify_one();
  worker_.join();
}

void SpeechQueue::submit(std::string text, SpeechQueueMode queue_mode, std::string source) {
  {
    std::lock_guard lock(mutex_);
    if (queue_mode == SpeechQueueMode::Interrupt) {
      pending_.clear();
      playback_controller_.stop();
    }
    pending_.push_back({std::move(text), std::move(source)});
  }
  condition_.notify_one();
}

void SpeechQueue::stop() {
  std::lock_guard lock(mutex_);
  pending_.clear();
  playback_controller_.stop();
}

void SpeechQueue::publish_config(const ConfigDocument &document) {
  apply_playback_settings(document, playback_controller_);
  const auto previous = config_.current();
  const bool profiles_changed = previous->value.at("profiles") != document.value.at("profiles") ||
                                previous->value.at("languageDefaults") != document.value.at("languageDefaults");
  config_.publish(document);
  if (profiles_changed) {
    {
      std::lock_guard lock(mutex_);
      eager_pending_ = true;
    }
    condition_.notify_one();
  }
}

PlaybackStatus SpeechQueue::status() const { return playback_controller_.status(); }

void SpeechQueue::run() {
  while (true) {
    Request request;
    std::shared_ptr<PlaybackControl> control;
    std::shared_ptr<const ConfigDocument> config;
    {
      std::unique_lock lock(mutex_);
      condition_.wait_for(lock, idle_poll_, [this] { return stopping_ || !pending_.empty() || eager_pending_; });
      if (stopping_) {
        return;
      }
      if (pending_.empty()) {
        config = config_.current();
        const bool eager = std::exchange(eager_pending_, false);
        lock.unlock();
        if (eager) {
          try {
            engine_.warm(*config, [this] {
              std::lock_guard lock(mutex_);
              return stopping_ || !pending_.empty();
            });
          } catch (const RunnerCancelled &) {
            // A new read or Quit cancels background warming immediately.
          } catch (const std::exception &error) {
            report_warm_failure(error);
          }
        }
        engine_.unload_if_idle(*config);
        continue;
      }
      request = std::move(pending_.front());
      pending_.pop_front();
      control = playback_controller_.begin(std::move(request.source));
      config = config_.current();
    }

    try {
      engine_.speak(request.text, *config, *control, playback_controller_);
    } catch (const RunnerCancelled &) {
      // Stop/replacement terminates runner I/O without retry or an error state.
    } catch (const std::exception &error) {
      playback_controller_.fail(control, error.what());
      record_activity(std::string("Speech failed: ") + error.what());
      std::cerr << "Tray speech request failed: " << error.what() << '\n';
    }

    playback_controller_.finish(control);
  }
}

}  // namespace tts_host
