#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace tts_host {

// Runs the GPL-licensed espeak-ng executable as a separate process and
// returns its IPA output with one trailing line ending removed. It is never
// linked into tts-host or a runner; see ADR 0005.
std::string phonemize_with_espeak_ng(const std::filesystem::path &executable,
                                     std::string_view voice, std::string_view text);

// Linux development uses the system executable on PATH. Windows packaging
// places the vendored executable next to the Kokoro runner (ADR 0006).
std::filesystem::path default_espeak_ng_executable();

}  // namespace tts_host
