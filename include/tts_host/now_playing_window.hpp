#pragma once

namespace tts_host {
class PlaybackController;

// Opens (or restores) the singleton modeless Now Playing window. Closing it
// hides it; it never cancels the active utterance.
void show_now_playing_window(PlaybackController &controller);
}  // namespace tts_host
