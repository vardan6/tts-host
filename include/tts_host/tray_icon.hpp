#pragma once

#include <filesystem>
#include <functional>
#include <string>

#include "tts_host/config_loader.hpp"
#include "tts_host/playback_controller.hpp"

namespace tts_host {
class ModelSessionManager;

// Shows the tray icon and blocks until the user chooses Quit from its
// context menu (docs/design/architecture.md#desktop-integration). The
// context menu also has a Settings... item that opens the settings window
// (one Settings window at a time; its nested loop keeps the tray responsive
// and safely propagates Quit). Windows only in
// this slice -- see docs/adr/0007-native-ui-per-platform.md; other platforms
// throw a clear not-implemented error instead of doing nothing. document and
// runner_directory are forwarded unchanged to the settings window, which
// needs the latter to find a model's engine runner when the user loads it.
// Tray startup attempts to register the configured selection shortcut; the
// tray menu can suspend and resume it for the current process without
// changing its saved binding.
// A new tray request interrupts the current utterance by default. Queueing is
// explicit, matching docs/design/architecture.md#speech-pipeline.
enum class SpeechQueueMode { Interrupt, Enqueue };

// speak_text runs the owning Host's synthesis path for text captured by a
// tray command; it keeps the tray and playback in the same Host process.
using SpeakTextFunction = std::function<void(const std::string &text, SpeechQueueMode)>;

// publish_config receives the tray's document after Settings closes. The tray
// thread alone touches `document`; the speech worker reads only what is
// published (docs/design/architecture.md#live-reload).
using PublishConfigFunction = std::function<void(const ConfigDocument &document)>;

void run_tray_icon(ConfigDocument &document,
                   const std::filesystem::path &runner_directory,
                   const SpeakTextFunction &speak_text, PlaybackController &playback_controller,
                   const PublishConfigFunction &publish_config,
                   ModelSessionManager *sessions = nullptr,
                   const std::function<void()> &stop_speech = {});

}  // namespace tts_host
