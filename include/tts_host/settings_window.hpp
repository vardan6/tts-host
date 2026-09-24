#pragma once

#include <filesystem>

#include "tts_host/config_loader.hpp"

namespace tts_host {

// Shows the settings window and blocks until the user closes it
// (docs/design/architecture.md#desktop-integration). Opens independently of
// the tray icon (`tts-host --settings`) so a platform with no usable tray
// still has a path to configuration. Windows only in this slice -- see
// docs/adr/0007-native-ui-per-platform.md; other platforms throw a clear
// not-implemented error instead of doing nothing. document supplies the
// current config and the path to write changes back to; this slice edits
// audio.outputDevice, hotkeys.readSelection, server.host/server.port, and languageDefaults.en
// (docs/requirements/product.md#configuration-and-controls) -- server.host/port
// and languageDefaults.en require a restart to take effect
// (docs/design/architecture.md#live-reload), which the window says but does
// not enforce.
//
// It also loads and unloads a model through a ModelSessionManager
// (tts_host/model_session.hpp), which keeps the runner process resident for as
// long as the window is open; runner_directory is where that manager looks for
// the model's engine runner. A timer polls modelRegistry.idleUnloadSeconds
// against it (ModelSessionManager::unload_if_idle) so a forgotten load does not
// keep weights resident indefinitely. A package can also be installed through
// the Import… button (tts_host/model_import.hpp), and, when
// modelRegistry.watchForChanges is true, a second timer polls
// modelRegistry.directories and refreshes the model combo/licence display when
// a registry scan finds something different -- so an install dropped in by
// something other than Import… still shows up without restarting. A Catalogue
// combo box lists every compiled-in entry (tts_host/model_catalogue.hpp) with
// its licence, size, and GET/HAVE status; its Download button fetches,
// verifies, and installs the entry through the same
// tts_host/catalogue_download.hpp path `--download-model` uses, into the
// first configured modelRegistry.directories entry, then refreshes the model
// views.
void run_settings_window(ConfigDocument &document,
                         const std::filesystem::path &runner_directory);

}  // namespace tts_host
