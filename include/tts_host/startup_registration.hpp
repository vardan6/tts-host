#pragma once

#include <filesystem>

namespace tts_host {

// Login startup as the operating system records it; never mirrored in config
// (docs/adr/0008-os-owned-login-startup.md).
enum class StartAtLoginState {
  off,
  on,
  // The entry exists but the user turned it off in the system startup page.
  disabledBySystem,
  // The entry exists but launches a different executable or config document.
  otherCopy,
};

// Reads this user's registration and compares it with this copy's command.
StartAtLoginState read_start_at_login_state(const std::filesystem::path &config_path);

// Registers or removes this user's login startup entry. The registration
// always launches the desktop tray with the same config document. Never
// writes the system's own enable/disable record.
void set_start_at_login(bool enabled, const std::filesystem::path &config_path);

// Opens the operating system's startup-apps page.
void open_system_startup_settings();

}  // namespace tts_host
