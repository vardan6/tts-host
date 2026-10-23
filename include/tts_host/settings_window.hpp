#pragma once

#include <filesystem>
#include <functional>

#include "tts_host/config_loader.hpp"

namespace tts_host {
class ModelSessionManager;

// Shows the settings window and blocks until the user closes it
// (docs/design/architecture.md#desktop-integration). Opens independently of
// the tray icon (`tts-host --settings`) so a platform with no usable tray
// still has a path to configuration. Windows only in this slice -- see
// docs/adr/0007-native-ui-per-platform.md; other platforms throw a clear
// not-implemented error instead of doing nothing. document supplies the
// current config and the path to write changes back to; this slice edits
// audio.outputDevice, hotkeys.readSelection, server.host/server.port, and languageDefaults.en
// (docs/requirements/product.md#configuration-and-controls) -- server.host/port
// require a restart to take effect (docs/design/architecture.md#live-reload).
// Model Load saves a default for the package's languages after success;
// publish_config applies model/default-profile choices to tray speech immediately.
//
// Tray Settings uses the Host's resident ModelSessionManager; independent
// --settings owns a temporary manager for that window. Load/Unload reports
// busy during an active utterance rather than blocking the window. A timer
// polls modelRegistry.idleUnloadSeconds against actual speech activity so a
// forgotten load does not keep weights resident indefinitely. A package can also be installed through
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
// views. Download, Update, and Import run on a worker thread so the window
// keeps responding; Cancel stops a download and keeps its partial file. A
// More ▾ menu holds the selected model's Revert, Delete previous, Remove
// superseded, Remove… (with a confirmation), Verify and repair… (hashing on
// the worker, then a confirmation before re-downloading damaged files), Open
// folder, and Open models folder, enabling only the actions that apply
// (docs/design/architecture.md#model-removal-repair-and-maintenance-actions).
void run_settings_window(ConfigDocument &document,
                         const std::filesystem::path &runner_directory,
                         ModelSessionManager *sessions = nullptr,
                         const std::function<void(const ConfigDocument &)> &publish_config = {},
                         const std::function<void(bool)> &probe_hotkey = {});

}  // namespace tts_host
