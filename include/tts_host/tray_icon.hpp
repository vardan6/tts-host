#pragma once

#include <filesystem>
#include <functional>
#include <string>

#include "tts_host/config_loader.hpp"

namespace tts_host {

// Shows the tray icon and blocks until the user chooses Quit from its
// context menu (docs/design/architecture.md#desktop-integration). The
// context menu also has a Settings... item that opens the settings window
// (blocking the tray's own message loop until it closes, since both windows
// are modal-by-blocking in this slice -- no threading yet). Windows only in
// this slice -- see docs/adr/0007-native-ui-per-platform.md; other platforms
// throw a clear not-implemented error instead of doing nothing. document and
// runner_directory are forwarded unchanged to the settings window, which
// needs the latter to find a model's engine runner when the user loads it.
// speak_text runs the owning Host's synthesis path for text captured by a
// tray command; it keeps the tray and playback in the same Host process.
using SpeakTextFunction = std::function<void(const std::string &text)>;

void run_tray_icon(const ConfigDocument &document,
                   const std::filesystem::path &runner_directory,
                   const SpeakTextFunction &speak_text);

}  // namespace tts_host
