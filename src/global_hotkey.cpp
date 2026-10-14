#include "tts_host/global_hotkey.hpp"

#include <algorithm>
#include <cctype>
#include <exception>
#include <string>
#include <vector>

namespace tts_host {
namespace {

std::string trim(std::string value) {
  const auto first = std::find_if_not(value.begin(), value.end(), [](unsigned char c) { return std::isspace(c); });
  const auto last = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char c) { return std::isspace(c); }).base();
  return first < last ? std::string(first, last) : std::string();
}

std::string uppercase(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
    return static_cast<char>(std::toupper(c));
  });
  return value;
}

}  // namespace

std::optional<GlobalHotkey> parse_global_hotkey(std::string_view value, std::string &error) {
  std::vector<std::string> parts;
  std::string remaining(value);
  std::size_t start = 0;
  while (start <= remaining.size()) {
    const std::size_t separator = remaining.find('+', start);
    const std::string part = trim(remaining.substr(start, separator - start));
    if (part.empty()) {
      error = "use modifier keys and one key";
      return std::nullopt;
    }
    parts.push_back(uppercase(part));
    if (separator == std::string::npos) {
      break;
    }
    start = separator + 1;
  }

  GlobalHotkey hotkey;
  for (const auto &part : parts) {
    unsigned int modifier = 0;
    if (part == "ALT") {
      modifier = kHotkeyAlt;
    } else if (part == "CTRL" || part == "CONTROL") {
      modifier = kHotkeyControl;
    } else if (part == "SHIFT") {
      modifier = kHotkeyShift;
    } else if (part == "WIN" || part == "WINDOWS") {
      modifier = kHotkeyWindows;
    }
    if (modifier != 0) {
      if ((hotkey.modifiers & modifier) != 0) {
        error = "a modifier appears more than once";
        return std::nullopt;
      }
      hotkey.modifiers |= modifier;
      continue;
    }

    if (hotkey.virtual_key != 0) {
      error = "choose exactly one non-modifier key";
      return std::nullopt;
    }
    if (part.size() == 1 && ((part[0] >= 'A' && part[0] <= 'Z') || (part[0] >= '0' && part[0] <= '9'))) {
      hotkey.virtual_key = static_cast<unsigned int>(part[0]);
      continue;
    }
    if (part.size() >= 2 && part[0] == 'F' &&
        std::all_of(part.begin() + 1, part.end(), [](unsigned char c) { return std::isdigit(c); })) {
      try {
        const int number = std::stoi(part.substr(1));
        if (number >= 1 && number <= 24) {
          hotkey.virtual_key = 0x70U + static_cast<unsigned int>(number - 1);
          continue;
        }
      } catch (const std::exception &) {
      }
    }
    error = "use a letter, digit, or F1 through F24 as the final key";
    return std::nullopt;
  }

  if (hotkey.modifiers == 0 || hotkey.virtual_key == 0) {
    error = "use at least one modifier and one key";
    return std::nullopt;
  }
  return hotkey;
}

}  // namespace tts_host
