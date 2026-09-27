#pragma once

#include <filesystem>

namespace tts_host {

// Registers or removes this user's login startup entry. The registration
// always launches the desktop tray with the same config document.
void set_start_at_login(bool enabled, const std::filesystem::path &config_path);

}  // namespace tts_host
