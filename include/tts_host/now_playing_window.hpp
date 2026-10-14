#pragma once

#include <functional>

namespace tts_host {
class PlaybackController;

// Opens (or restores) the singleton modeless Now Playing window. Closing it
// hides it; it never cancels the active utterance.
void show_now_playing_window(PlaybackController &controller,
                             std::function<void()> stop_speech = {});
// Destroy the modeless window before its owning controller is destroyed.
void destroy_now_playing_window();
}  // namespace tts_host
