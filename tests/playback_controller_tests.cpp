#include "tts_host/playback_controller.hpp"

#include <chrono>
#include <cmath>
#include <future>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const std::string &message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void pause_resume_and_stop_publish_the_active_lifecycle() {
  tts_host::PlaybackController controller;
  const auto control = controller.begin();
  require(controller.state() == tts_host::PlaybackState::Preparing,
          "begin did not publish preparing");

  controller.mark_playing(control.get());
  require(controller.state() == tts_host::PlaybackState::Playing,
          "mark_playing did not publish playing");

  controller.pause();
  require(controller.state() == tts_host::PlaybackState::Paused && control->paused(),
          "pause did not publish paused or pause the active token");

  auto waiter = std::async(std::launch::async, [&] { return control->wait_until_resumed(); });
  require(waiter.wait_for(std::chrono::milliseconds(20)) == std::future_status::timeout,
          "paused token did not block playback");
  controller.resume();
  require(waiter.get(), "resume incorrectly cancelled the token");
  require(controller.state() == tts_host::PlaybackState::Playing,
          "resume did not publish playing");

  controller.stop();
  require(controller.state() == tts_host::PlaybackState::Stopping && control->cancelled(),
          "stop did not publish stopping and cancel the active token");
  controller.finish(control);
  require(controller.state() == tts_host::PlaybackState::Idle,
          "finish did not publish idle");
}

void replacement_cancels_stale_work_without_erasing_the_new_state() {
  tts_host::PlaybackController controller;
  const auto first = controller.begin();
  const auto second = controller.begin();
  require(first->cancelled(), "replacement did not cancel the stale utterance");
  controller.finish(first);
  require(controller.state() == tts_host::PlaybackState::Preparing,
          "stale completion erased the replacement state");
  controller.finish(second);
  require(controller.state() == tts_host::PlaybackState::Idle,
          "active completion did not return to idle");
}

void speech_speed_changes_identify_stale_lookahead() {
  tts_host::PlaybackController controller;
  controller.set_default_speech_speed(1.25);
  const auto control = controller.begin();
  require(controller.speech_speed() == 1.25, "begin did not use the persisted default speed");
  const auto first_generation = controller.speech_speed_generation();
  require(controller.prepare_sentence_for_playback(control.get(), first_generation),
          "current lookahead was unexpectedly stale");
  controller.set_active_speech_speed(1.5);
  require(controller.speech_speed() == 1.5 &&
              controller.speech_speed_generation() != first_generation,
          "active speed change did not publish a new generation");
  require(!controller.prepare_sentence_for_playback(control.get(), first_generation),
          "stale lookahead was allowed to start playback");
  require(controller.prepare_sentence_for_playback(control.get(), controller.speech_speed_generation()),
          "regenerated lookahead was not allowed to start playback");
}

void paused_seek_wakes_playback_without_resuming_it() {
  tts_host::PlaybackController controller;
  controller.set_seek_interval_seconds(5);
  const auto control = controller.begin();
  controller.prepare_sentence_for_playback(control.get(), controller.speech_speed_generation());
  controller.set_generated_seconds(control.get(), 20.0);
  control->update_position(10.0);
  controller.pause();

  auto waiter = std::async(std::launch::async, [&] {
    return control->wait_until_resumed_or_seek(control->seek_generation());
  });
  require(waiter.wait_for(std::chrono::milliseconds(20)) == std::future_status::timeout,
          "paused playback should wait before seek");
  require(controller.can_seek(-1) && controller.can_seek(1),
          "seek targets inside generated audio should be enabled");
  require(controller.seek_by_interval(-1), "backward seek should be accepted while paused");
  require(waiter.wait_for(std::chrono::seconds(1)) == std::future_status::ready,
          "seek should wake a paused sink");
  require(!waiter.get(), "seek should wake the sink without reporting resume");
  require(control->paused(), "seek should preserve paused state");
  require(std::abs(control->requested_seek_seconds() - 5.0) < 0.001,
          "seek should request a position on the source timeline");
  controller.finish(control);
}

}  // namespace

int main() {
  try {
    pause_resume_and_stop_publish_the_active_lifecycle();
    replacement_cancels_stale_work_without_erasing_the_new_state();
    speech_speed_changes_identify_stale_lookahead();
    paused_seek_wakes_playback_without_resuming_it();
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
