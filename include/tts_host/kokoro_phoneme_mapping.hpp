#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

namespace tts_host {

// Translates espeak-ng `--ipa=3` output (English, en-us) into Kokoro-82M's
// sparse ONNX phoneme vocabulary. Kokoro was trained on a custom vocabulary,
// not raw IPA, so symbols the vocabulary has no slot for are dropped rather
// than erroring — matching the reference kokoro-onnx tokenizer. Ported (as
// static data, not vendored code) from hexgrad/misaki's (Apache-2.0)
// espeak.py `EspeakFallback`, the non-British, non-"2.0" branch — see
// ADR 0006. Returns bare phoneme token ids with no start/end padding; callers
// add the model's padding token themselves.
std::vector<std::int64_t> translate_ipa_to_kokoro_token_ids(std::string_view ipa);

}  // namespace tts_host
