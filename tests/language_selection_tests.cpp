#include "tts_host/language_selection.hpp"

#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void expect_detected(const std::string &text, const std::optional<std::string> &expected,
                     const std::string &message) {
  const auto actual = tts_host::detect_language_from_script(text);
  if (actual != expected) {
    throw std::runtime_error(message + "\n  expected: " + expected.value_or("<none>") +
                             "\n  actual:   " + actual.value_or("<none>"));
  }
}

void expect_selected(const std::optional<std::string> &explicit_language, const std::string &text,
                     const std::vector<std::string> &configured, const std::string &expected_tag,
                     tts_host::LanguageSource expected_source, const std::string &message) {
  const auto actual = tts_host::select_request_language(explicit_language, text, configured);
  if (actual.tag != expected_tag || actual.source != expected_source) {
    throw std::runtime_error(message + "\n  expected: " + expected_tag + " (" +
                             tts_host::describe_language_source(expected_source) +
                             ")\n  actual:   " + actual.tag + " (" +
                             tts_host::describe_language_source(actual.source) + ")");
  }
}

void latin_text_is_english() {
  expect_detected("Hello world", "en", "Latin text was not detected as English");
  expect_detected("Café Münchner", "en", "accented Latin text was not detected as English");
}

void cyrillic_text_is_russian() {
  expect_detected("Привет, мир", "ru", "Cyrillic text was not detected as Russian");
}

void armenian_text_is_armenian() {
  expect_detected("Բարև աշխարհ", "hy", "Armenian text was not detected as Armenian");
}

void text_without_supported_letters_detects_nothing() {
  expect_detected("", std::nullopt, "empty text produced a language");
  expect_detected("123 -- 45.6 %", std::nullopt, "digits and punctuation produced a language");
  expect_detected("こんにちは", std::nullopt, "unsupported script produced a language");
}

void the_dominant_script_wins_and_an_even_mix_detects_nothing() {
  expect_detected("Привет, мир! TTS", "ru",
                  "a Latin product name outvoted the surrounding Cyrillic sentence");
  expect_detected("abc где", std::nullopt, "an even script mix produced a language");
}

void malformed_utf8_does_not_throw() {
  expect_detected(std::string("Hello \xC3"), "en", "a truncated UTF-8 sequence changed the result");
  expect_detected(std::string("\xFF\xFE"), std::nullopt, "invalid bytes produced a language");
}

void an_explicit_language_wins_over_the_script() {
  expect_selected(std::string("ru"), "Hello world", {"en", "ru"}, "ru",
                  tts_host::LanguageSource::Explicit, "--language did not override the script");
  expect_selected(std::string("hy"), "Hello world", {"en"}, "hy",
                  tts_host::LanguageSource::Explicit,
                  "an unconfigured explicit language was not returned for the caller to report");
}

void a_detected_language_is_used_when_configured() {
  expect_selected(std::nullopt, "Привет, мир", {"en", "ru"}, "ru",
                  tts_host::LanguageSource::Script, "a configured detected language was not used");
}

void an_unconfigured_detected_language_falls_back() {
  expect_selected(std::nullopt, "Привет, мир", {"en"}, "en", tts_host::LanguageSource::Default,
                  "text routed to a language the user has not configured");
}

void undetectable_text_falls_back_to_the_default() {
  expect_selected(std::nullopt, "12345", {"en", "ru"}, "en", tts_host::LanguageSource::Default,
                  "undetectable text did not fall back to the configured default");
}

}  // namespace

int main() {
  try {
    latin_text_is_english();
    cyrillic_text_is_russian();
    armenian_text_is_armenian();
    text_without_supported_letters_detects_nothing();
    the_dominant_script_wins_and_an_even_mix_detects_nothing();
    malformed_utf8_does_not_throw();
    an_explicit_language_wins_over_the_script();
    a_detected_language_is_used_when_configured();
    an_unconfigured_detected_language_falls_back();
    undetectable_text_falls_back_to_the_default();
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
