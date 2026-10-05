#include "tts_host/config_snapshot.hpp"

#include <atomic>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

void require(bool condition, const std::string &message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

tts_host::ConfigDocument document_with(int skip_seconds, double speed) {
  tts_host::ConfigDocument document;
  document.value = {{"audio",
                     {{"outputDevice", "system-default"},
                      {"playbackSkipSeconds", skip_seconds},
                      {"speechSpeed", speed}}},
                    {"languageDefaults", {{"en", "fast"}}}};
  return document;
}

void in_flight_snapshot_is_unchanged_by_publish() {
  tts_host::ConfigSnapshot snapshot(document_with(5, 1.0));
  const auto in_flight = snapshot.current();

  auto edited = document_with(12, 1.5);
  edited.value["languageDefaults"]["en"] = "quality";
  snapshot.publish(edited);

  require(in_flight->value.at("audio").at("playbackSkipSeconds") == 5 &&
              in_flight->value.at("languageDefaults").at("en") == "fast",
          "publish changed a snapshot an in-flight request already holds");
  require(snapshot.current()->value.at("audio").at("playbackSkipSeconds") == 12 &&
              snapshot.current()->value.at("languageDefaults").at("en") == "quality",
          "the next request did not observe the published document");
}

// A Settings thread repeatedly publishes documents whose fields all encode the
// same generation while readers walk their snapshots. Any mixed snapshot means
// a request could observe half of one Settings edit.
void concurrent_publish_never_yields_a_mixed_snapshot() {
  const auto document_for = [](int skip) {
    auto document = document_with(skip, 0.5 + skip * 0.05);
    document.value["languageDefaults"]["en"] = "p" + std::to_string(skip);
    return document;
  };
  tts_host::ConfigSnapshot snapshot(document_for(1));
  std::atomic_bool done = false;
  std::atomic_bool mixed = false;

  std::vector<std::thread> readers;
  for (int reader = 0; reader < 4; ++reader) {
    readers.emplace_back([&] {
      while (!done.load()) {
        const auto config = snapshot.current();
        const int skip = config->value.at("audio").at("playbackSkipSeconds").get<int>();
        const double speed = config->value.at("audio").at("speechSpeed").get<double>();
        const auto profile = config->value.at("languageDefaults").at("en").get<std::string>();
        if (speed != 0.5 + skip * 0.05 || profile != "p" + std::to_string(skip)) {
          mixed.store(true);
        }
      }
    });
  }

  for (int round = 0; round < 2000; ++round) {
    snapshot.publish(document_for(1 + round % 30));
  }
  done.store(true);
  for (auto &reader : readers) reader.join();

  require(!mixed.load(), "a reader observed a snapshot mixing two published documents");
}

void applying_settings_updates_the_next_seek_press() {
  tts_host::PlaybackController controller;
  controller.set_seek_interval_seconds(5);
  const auto control = controller.begin();
  controller.mark_playing(control.get());
  controller.set_generated_seconds(control.get(), 60.0);
  control->update_position(20.0);

  tts_host::apply_playback_settings(document_with(12, 1.25), controller);
  require(controller.seek_interval_seconds() == 12, "seek interval was not applied live");
  require(controller.speech_speed() == 1.0,
          "a published default changed the active utterance's speed");

  require(controller.seek_by_interval(-1), "seek back was rejected");
  require(control->requested_seek_seconds() == 8.0,
          "the next seek press did not use the newly applied interval");
}

void missing_optional_audio_settings_apply_documented_defaults() {
  tts_host::PlaybackController controller;
  tts_host::apply_playback_settings(document_with(17, 2.0), controller);
  tts_host::ConfigDocument document;
  document.value = {{"audio", {{"outputDevice", "system-default"}}}};

  tts_host::apply_playback_settings(document, controller);
  require(controller.seek_interval_seconds() == 5, "missing interval did not mean the default");
  controller.begin();
  require(controller.speech_speed() == 1.0, "missing speed did not mean the default");
}

}  // namespace

int main() {
  try {
    in_flight_snapshot_is_unchanged_by_publish();
    concurrent_publish_never_yields_a_mixed_snapshot();
    applying_settings_updates_the_next_seek_press();
    missing_optional_audio_settings_apply_documented_defaults();
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
  std::cout << "config snapshot tests passed\n";
  return 0;
}
