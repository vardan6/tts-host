#include "tts_host/playback_controller.hpp"

#include <stdexcept>

namespace tts_host {

void PlaybackControl::cancel() {
  cancelled_.store(true);
  condition_.notify_all();
}

void PlaybackControl::pause() {
  std::lock_guard lock(mutex_);
  paused_ = true;
}

void PlaybackControl::resume() {
  {
    std::lock_guard lock(mutex_);
    paused_ = false;
  }
  condition_.notify_all();
}

bool PlaybackControl::cancelled() const { return cancelled_.load(); }

bool PlaybackControl::paused() const {
  std::lock_guard lock(mutex_);
  return paused_;
}

bool PlaybackControl::wait_until_resumed() {
  std::unique_lock lock(mutex_);
  condition_.wait(lock, [this] { return cancelled_.load() || !paused_; });
  return !cancelled_.load();
}

bool PlaybackControl::wait_until_resumed_or_seek(std::uint64_t expected_seek_generation) {
  std::unique_lock lock(mutex_);
  condition_.wait(lock, [this, expected_seek_generation] {
    return cancelled_.load() || !paused_ || seek_generation_.load() != expected_seek_generation;
  });
  return !cancelled_.load() && seek_generation_.load() == expected_seek_generation;
}

std::uint64_t PlaybackControl::seek_generation() const { return seek_generation_.load(); }

double PlaybackControl::requested_seek_seconds() const { return seek_seconds_.load(); }

bool PlaybackControl::request_seek(double seconds) {
  if (seconds < 0.0 || seconds > generated_seconds_.load()) return false;
  seek_seconds_.store(seconds);
  seek_generation_.fetch_add(1);
  condition_.notify_all();
  return true;
}

void PlaybackControl::update_position(double seconds) { position_seconds_.store(seconds); }

double PlaybackControl::position_seconds() const { return position_seconds_.load(); }

void PlaybackControl::set_generated_seconds(double seconds) { generated_seconds_.store(seconds); }

double PlaybackControl::generated_seconds() const { return generated_seconds_.load(); }

std::shared_ptr<PlaybackControl> PlaybackController::begin() {
  std::lock_guard lock(mutex_);
  if (active_) {
    active_->cancel();
  }
  active_ = std::make_shared<PlaybackControl>();
  state_ = PlaybackState::Preparing;
  error_message_.clear();
  speech_speed_ = default_speech_speed_;
  ++speech_speed_generation_;
  return active_;
}

void PlaybackController::mark_playing(const PlaybackControl *control) {
  std::lock_guard lock(mutex_);
  if (active_.get() == control && state_ == PlaybackState::Preparing) {
    state_ = PlaybackState::Playing;
  }
}

bool PlaybackController::prepare_sentence_for_playback(
    const PlaybackControl *control, std::uint64_t speech_speed_generation) {
  std::lock_guard lock(mutex_);
  if (active_.get() != control || active_->cancelled() ||
      speech_speed_generation_ != speech_speed_generation) {
    return false;
  }
  if (state_ == PlaybackState::Preparing) {
    state_ = PlaybackState::Playing;
  }
  return state_ == PlaybackState::Playing || state_ == PlaybackState::Paused;
}

void PlaybackController::pause() {
  std::lock_guard lock(mutex_);
  if (active_ && (state_ == PlaybackState::Preparing || state_ == PlaybackState::Playing)) {
    active_->pause();
    state_ = PlaybackState::Paused;
  }
}

void PlaybackController::resume() {
  std::lock_guard lock(mutex_);
  if (active_ && state_ == PlaybackState::Paused) {
    active_->resume();
    state_ = PlaybackState::Playing;
  }
}

void PlaybackController::stop() {
  std::lock_guard lock(mutex_);
  if (active_) {
    active_->cancel();
    state_ = PlaybackState::Stopping;
  }
}

void PlaybackController::finish(const std::shared_ptr<PlaybackControl> &control) {
  std::lock_guard lock(mutex_);
  if (active_ == control) {
    active_.reset();
    if (state_ != PlaybackState::Error) state_ = PlaybackState::Idle;
  }
}

void PlaybackController::fail(const std::shared_ptr<PlaybackControl> &control, std::string message) {
  std::lock_guard lock(mutex_);
  if (active_ == control) {
    active_->cancel();
    active_.reset();
    error_message_ = std::move(message);
    state_ = PlaybackState::Error;
  }
}

std::string PlaybackController::error_message() const {
  std::lock_guard lock(mutex_);
  return error_message_;
}

PlaybackState PlaybackController::state() const {
  std::lock_guard lock(mutex_);
  return state_;
}

namespace {
void require_speech_speed(double speed) {
  if (speed < 0.5 || speed > 2.0) {
    throw std::invalid_argument("speech speed must be between 0.5 and 2.0");
  }
}
}  // namespace

void PlaybackController::set_default_speech_speed(double speed) {
  require_speech_speed(speed);
  std::lock_guard lock(mutex_);
  default_speech_speed_ = speed;
}

void PlaybackController::set_active_speech_speed(double speed) {
  require_speech_speed(speed);
  std::lock_guard lock(mutex_);
  if (active_ && speech_speed_ != speed) {
    speech_speed_ = speed;
    ++speech_speed_generation_;
  }
}

double PlaybackController::speech_speed() const {
  std::lock_guard lock(mutex_);
  return speech_speed_;
}

std::uint64_t PlaybackController::speech_speed_generation() const {
  std::lock_guard lock(mutex_);
  return speech_speed_generation_;
}

void PlaybackController::set_seek_interval_seconds(int seconds) {
  if (seconds < 1 || seconds > 30) {
    throw std::invalid_argument("playback seek interval must be between 1 and 30 seconds");
  }
  std::lock_guard lock(mutex_);
  seek_interval_seconds_ = seconds;
}

int PlaybackController::seek_interval_seconds() const {
  std::lock_guard lock(mutex_);
  return seek_interval_seconds_;
}

bool PlaybackController::seek_by_interval(int direction) {
  if (direction != -1 && direction != 1) return false;
  std::lock_guard lock(mutex_);
  if (!active_ || (state_ != PlaybackState::Playing && state_ != PlaybackState::Paused)) return false;
  const double target = active_->position_seconds() + direction * seek_interval_seconds_;
  return active_->request_seek(target);
}

bool PlaybackController::can_seek(int direction) const {
  if (direction != -1 && direction != 1) return false;
  std::lock_guard lock(mutex_);
  if (!active_ || (state_ != PlaybackState::Playing && state_ != PlaybackState::Paused)) return false;
  const double target = active_->position_seconds() + direction * seek_interval_seconds_;
  return target >= 0.0 && target <= active_->generated_seconds();
}

double PlaybackController::position_seconds() const {
  std::lock_guard lock(mutex_);
  return active_ ? active_->position_seconds() : 0.0;
}

double PlaybackController::generated_seconds() const {
  std::lock_guard lock(mutex_);
  return active_ ? active_->generated_seconds() : 0.0;
}

void PlaybackController::set_generated_seconds(const PlaybackControl *control, double seconds) {
  std::lock_guard lock(mutex_);
  if (active_.get() == control && seconds >= 0.0) active_->set_generated_seconds(seconds);
}

}  // namespace tts_host
