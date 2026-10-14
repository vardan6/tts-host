#pragma once

#include <functional>
#include <string>

namespace tts_host {
class PlaybackController;

// What Now Playing delegates to its owner (the tray). All run on the thread
// that owns the window.
struct NowPlayingActions {
  std::function<void()> stop_speech;
  // Reads the saved external target's selection; never captures Host
  // controls or takes focus (docs/design/architecture.md#desktop-integration).
  std::function<void()> read_selection;
  std::function<void()> read_clipboard;
  // Polled on refresh: the saved target and the last capture result.
  std::function<std::string()> target_label;
  std::function<std::string()> capture_status;
};

// Opens (or restores) the singleton modeless Now Playing window. Closing it
// hides it; it never cancels the active utterance.
void show_now_playing_window(PlaybackController &controller, NowPlayingActions actions = {});
// Destroy the modeless window before its owning controller is destroyed.
void destroy_now_playing_window();
}  // namespace tts_host
