#pragma once

#include "tts_host/config_loader.hpp"

namespace tts_host {

// Shows the tray icon and blocks until the user chooses Quit from its
// context menu (docs/design/architecture.md#desktop-integration). The
// context menu also has a Settings... item that opens the settings window
// (blocking the tray's own message loop until it closes, since both windows
// are modal-by-blocking in this slice -- no threading yet). Windows only in
// this slice -- see docs/adr/0007-native-ui-per-platform.md; other platforms
// throw a clear not-implemented error instead of doing nothing. document is
// forwarded unchanged to the settings window.
void run_tray_icon(const ConfigDocument &document);

}  // namespace tts_host
