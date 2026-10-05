#include "tts_host/config_snapshot.hpp"

#include <utility>

namespace tts_host {

ConfigSnapshot::ConfigSnapshot(ConfigDocument initial)
    : current_(std::make_shared<const ConfigDocument>(std::move(initial))) {}

std::shared_ptr<const ConfigDocument> ConfigSnapshot::current() const {
  std::lock_guard lock(mutex_);
  return current_;
}

void ConfigSnapshot::publish(ConfigDocument document) {
  // Build the copy outside the lock; only the pointer swap is serialized.
  auto next = std::make_shared<const ConfigDocument>(std::move(document));
  std::lock_guard lock(mutex_);
  current_ = std::move(next);
}

void apply_playback_settings(const ConfigDocument &document, PlaybackController &controller) {
  const auto &audio = document.value.at("audio");
  // Older valid config documents did not carry these optional schema
  // properties; absence deliberately means the documented defaults.
  controller.set_default_speech_speed(audio.value("speechSpeed", 1.0));
  controller.set_seek_interval_seconds(audio.value("playbackSkipSeconds", 5));
}

}  // namespace tts_host
