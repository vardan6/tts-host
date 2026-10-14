#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace tts_host {

// Win32 RegisterHotKey-compatible modifier bits. Kept platform-neutral so the
// configuration syntax can be parsed and tested on every supported platform.
constexpr unsigned int kHotkeyAlt = 0x0001;
constexpr unsigned int kHotkeyControl = 0x0002;
constexpr unsigned int kHotkeyShift = 0x0004;
constexpr unsigned int kHotkeyWindows = 0x0008;

struct GlobalHotkey {
  unsigned int modifiers = 0;
  unsigned int virtual_key = 0;
};

// Parses a user-entered chord. An empty value is deliberately not a hotkey: it
// disables the optional global selection command. The parser accepts letters,
// digits, and F1 through F24.
std::optional<GlobalHotkey> parse_global_hotkey(std::string_view value, std::string &error);

}  // namespace tts_host
