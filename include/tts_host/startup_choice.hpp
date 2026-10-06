#pragma once

#include <filesystem>
#include <functional>

namespace tts_host {

// Host state in the data directory, not configuration; its presence means the
// first-run login-startup question was answered
// (docs/design/architecture.md#login-startup).
inline constexpr char kStartupChoiceMarkerName[] = "startup-choice.marker";

// Asks once per data directory. Without a marker, calls ask with the
// pre-ticked default, writes the marker whatever the answer, then calls
// register_startup only on yes. Returns whether the question was asked.
bool ask_startup_choice_once(const std::filesystem::path &data_dir, bool default_answer,
                             const std::function<bool(bool)> &ask,
                             const std::function<void()> &register_startup);

// The native first-run question: a checkbox pre-ticked with default_answer.
// Closing the window without OK answers no. Windows only for now.
bool show_startup_choice_dialog(bool default_answer);

}  // namespace tts_host
