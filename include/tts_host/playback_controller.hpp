#pragma once

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>

namespace tts_host {

// The token shared by synthesis and the playback sink for one utterance.  It
// is deliberately independent of a particular platform audio API: a sink
// checks cancellation promptly and waits here while an utterance is paused.
class PlaybackControl {
 public:
  void cancel();
  void pause();
  void resume();
  bool cancelled() const;
  bool paused() const;
  bool wait_until_resumed();  // false means cancellation won while waiting
  bool wait_until_resumed_or_seek(std::uint64_t expected_seek_generation);
  std::uint64_t seek_generation() const;
  double requested_seek_seconds() const;
  bool request_seek(double seconds);
  void update_position(double seconds);
  double position_seconds() const;
  void set_generated_seconds(double seconds);
  double generated_seconds() const;

 private:
  std::atomic_bool cancelled_{false};
  std::atomic_uint64_t seek_generation_{0};
  std::atomic<double> seek_seconds_{0.0};
  std::atomic<double> position_seconds_{0.0};
  std::atomic<double> generated_seconds_{0.0};
  mutable std::mutex mutex_;
  std::condition_variable condition_;
  bool paused_ = false;
};

enum class PlaybackState { Idle, Preparing, Playing, Paused, Stopping, Error };

// Owns the observable lifecycle and generated-audio seek position of the one
// active desktop utterance. The scheduler owns runner work; this shared token
// lets runner work and WASAPI playback observe cancellation, pause, and seek.
class PlaybackController {
 public:
  std::shared_ptr<PlaybackControl> begin();
  void mark_playing(const PlaybackControl *control);
  bool prepare_sentence_for_playback(const PlaybackControl *control,
                                    std::uint64_t speech_speed_generation);
  void pause();
  void resume();
  void stop();
  void finish(const std::shared_ptr<PlaybackControl> &control);
  void fail(const std::shared_ptr<PlaybackControl> &control, std::string message);
  std::string error_message() const;
  PlaybackState state() const;
  // A speed change is observed by the host at a sentence boundary. The
  // generation lets it discard already-synthesized, not-yet-played lookahead.
  void set_default_speech_speed(double speed);
  void set_active_speech_speed(double speed);
  double speech_speed() const;
  std::uint64_t speech_speed_generation() const;
  void set_seek_interval_seconds(int seconds);
  int seek_interval_seconds() const;
  bool seek_by_interval(int direction);
  bool can_seek(int direction) const;
  double position_seconds() const;
  double generated_seconds() const;
  void set_generated_seconds(const PlaybackControl *control, double seconds);

 private:
  mutable std::mutex mutex_;
  std::shared_ptr<PlaybackControl> active_;
  PlaybackState state_ = PlaybackState::Idle;
  double default_speech_speed_ = 1.0;
  double speech_speed_ = 1.0;
  std::uint64_t speech_speed_generation_ = 0;
  int seek_interval_seconds_ = 5;
  std::string error_message_;
};

}  // namespace tts_host
