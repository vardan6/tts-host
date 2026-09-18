#include "tts_host/language_selection.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

namespace tts_host {
namespace {

// Decodes the UTF-8 sequence starting at `index`, advancing `index` past it.
// Malformed bytes advance by one and decode as U+FFFD, which counts as no
// script -- detection degrades to the configured default rather than throwing
// on text the user can still hear.
std::uint32_t next_code_point(const std::string &text, std::size_t &index) {
  const auto byte = static_cast<unsigned char>(text[index]);
  std::size_t length = 1;
  std::uint32_t code_point = byte;
  if ((byte & 0xE0U) == 0xC0U) {
    length = 2;
    code_point = byte & 0x1FU;
  } else if ((byte & 0xF0U) == 0xE0U) {
    length = 3;
    code_point = byte & 0x0FU;
  } else if ((byte & 0xF8U) == 0xF0U) {
    length = 4;
    code_point = byte & 0x07U;
  } else if ((byte & 0x80U) != 0U) {
    ++index;
    return 0xFFFDU;
  }

  if (index + length > text.size()) {
    ++index;
    return 0xFFFDU;
  }
  for (std::size_t offset = 1; offset < length; ++offset) {
    const auto continuation = static_cast<unsigned char>(text[index + offset]);
    if ((continuation & 0xC0U) != 0x80U) {
      ++index;
      return 0xFFFDU;
    }
    code_point = (code_point << 6U) | (continuation & 0x3FU);
  }
  index += length;
  return code_point;
}

bool is_latin_letter(std::uint32_t code_point) {
  return (code_point >= U'A' && code_point <= U'Z') || (code_point >= U'a' && code_point <= U'z') ||
         // Latin-1 Supplement, Extended-A and Extended-B letters, minus the two
         // multiplication/division signs that sit inside the letter block.
         (code_point >= 0x00C0U && code_point <= 0x024FU && code_point != 0x00D7U &&
          code_point != 0x00F7U);
}

bool is_cyrillic_letter(std::uint32_t code_point) {
  return code_point >= 0x0400U && code_point <= 0x052FU;
}

bool is_armenian_letter(std::uint32_t code_point) {
  return (code_point >= 0x0531U && code_point <= 0x0556U) ||
         (code_point >= 0x0561U && code_point <= 0x0588U) || code_point == 0x0587U;
}

}  // namespace

std::optional<std::string> detect_language_from_script(const std::string &text) {
  struct ScriptCount {
    const char *tag;
    std::size_t letters;
  };
  std::array<ScriptCount, 3> counts{
      ScriptCount{"en", 0},
      ScriptCount{"ru", 0},
      ScriptCount{"hy", 0},
  };

  for (std::size_t index = 0; index < text.size();) {
    const auto code_point = next_code_point(text, index);
    if (is_latin_letter(code_point)) {
      ++counts[0].letters;
    } else if (is_cyrillic_letter(code_point)) {
      ++counts[1].letters;
    } else if (is_armenian_letter(code_point)) {
      ++counts[2].letters;
    }
  }

  const auto best = std::max_element(counts.begin(), counts.end(),
                                     [](const ScriptCount &left, const ScriptCount &right) {
                                       return left.letters < right.letters;
                                     });
  if (best->letters == 0) {
    return std::nullopt;
  }
  // A tie is not a signal: mixed-script text (a Russian sentence quoting an
  // English product name, say) should fall back rather than guess.
  const auto ties = std::count_if(counts.begin(), counts.end(), [best](const ScriptCount &count) {
    return count.letters == best->letters;
  });
  if (ties > 1) {
    return std::nullopt;
  }
  return std::string(best->tag);
}

SelectedLanguage select_request_language(const std::optional<std::string> &explicit_language,
                                         const std::string &text,
                                         const std::vector<std::string> &configured_languages) {
  if (explicit_language.has_value()) {
    return {*explicit_language, LanguageSource::Explicit};
  }

  const auto detected = detect_language_from_script(text);
  if (detected.has_value() &&
      std::find(configured_languages.begin(), configured_languages.end(), *detected) !=
          configured_languages.end()) {
    return {*detected, LanguageSource::Script};
  }

  return {std::string(kFallbackLanguage), LanguageSource::Default};
}

std::string describe_language_source(LanguageSource source) {
  switch (source) {
    case LanguageSource::Explicit:
      return "explicit";
    case LanguageSource::Script:
      return "detected from text script";
    case LanguageSource::Default:
      break;
  }
  return "configured default";
}

}  // namespace tts_host
