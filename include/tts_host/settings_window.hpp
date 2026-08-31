#pragma once

#include "tts_host/config_loader.hpp"

namespace tts_host {

// Shows the settings window and blocks until the user closes it
// (docs/design/architecture.md#desktop-integration). Opens independently of
// the tray icon (`tts-host --settings`) so a platform with no usable tray
// still has a path to configuration. Windows only in this slice -- see
// docs/adr/0007-native-ui-per-platform.md; other platforms throw a clear
// not-implemented error instead of doing nothing. document supplies the
// current config and the path to write changes back to; this slice edits
// audio.outputDevice and server.host/server.port
// (docs/requirements/product.md#configuration-and-controls) -- server.host/port
// require a restart to take effect (docs/design/architecture.md#live-reload),
// which the window says but does not enforce. The model manager and hotkeys
// controls remain.
void run_settings_window(const ConfigDocument &document);

}  // namespace tts_host
