#pragma once

#include <optional>
#include <string>
#include <vector>

namespace tts_host {

// Language selection for a synthesis request
// (docs/design/architecture.md#speech-pipeline,
// docs/requirements/product.md "Speech behaviour"): an explicit request
// parameter wins, else the text's script decides, else the configured default.
//
// Script detection only has to separate the supported languages, which use
// mutually exclusive scripts: Latin -> English, Cyrillic -> Russian, Armenian
// -> Armenian. Anything else (digits, punctuation, emoji, CJK) is not a
// language signal and is ignored.

// The language used when nothing else identifies one. The schema has no
// top-level default-language key, so English -- the product's primary language
// and the tag every shipped config carries -- is that default.
inline constexpr const char *kFallbackLanguage = "en";

enum class LanguageSource {
  Explicit,  // named by the request (--language)
  Script,    // detected from the text
  Default,   // kFallbackLanguage, because nothing else identified one
};

struct SelectedLanguage {
  std::string tag;
  LanguageSource source = LanguageSource::Default;
};

// Returns the language tag of the dominant script in `text`, or nullopt when
// the text carries no letters of a supported script, or when two scripts tie.
std::optional<std::string> detect_language_from_script(const std::string &text);

// `configured_languages` are the keys of config's `languageDefaults`. A
// detected language is only honoured when it is configured -- otherwise
// Cyrillic text would route to a Russian profile the user has not set up.
// An explicit tag is returned as given, configured or not, so the caller can
// report the missing entry by name instead of silently speaking English.
SelectedLanguage select_request_language(const std::optional<std::string> &explicit_language,
                                         const std::string &text,
                                         const std::vector<std::string> &configured_languages);

// "explicit", "detected from text script", or "configured default", for CLI
// and status output.
std::string describe_language_source(LanguageSource source);

}  // namespace tts_host
