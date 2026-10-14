#include "tts_host/kokoro_phoneme_mapping.hpp"

#include <cstdint>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void require(bool condition, const std::string &message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

std::string describe(const std::vector<std::int64_t> &ids) {
  std::ostringstream stream;
  stream << "[";
  for (std::size_t index = 0; index < ids.size(); ++index) {
    if (index != 0) {
      stream << ", ";
    }
    stream << ids[index];
  }
  stream << "]";
  return stream.str();
}

void expect(const std::string &ipa, const std::vector<std::int64_t> &expected, const std::string &message) {
  const auto actual = tts_host::translate_ipa_to_kokoro_token_ids(ipa);
  if (actual != expected) {
    throw std::runtime_error(message + "\n  expected: " + describe(expected) +
                             "\n  actual:   " + describe(actual));
  }
}

}  // namespace

int main() {
  try {
    // Real `espeak-ng -q --ipa=3 -v en-us` output for "hello": a ZWJ-tied
    // diphthong "o‍ʊ" folds to the single Kokoro symbol "O".
    expect("həlˈo‍ʊ", {50, 83, 54, 156, 31}, "a ZWJ diphthong did not fold to 'O'");

    // "joy": ZWJ-tied affricate "d‍ʒ" folds to 'ʤ' and diphthong
    // "ɔ‍ɪ" folds to 'Y'.
    expect("d‍ʒˈɔ‍ɪ", {82, 156, 41}, "affricate/diphthong ties were not folded");

    // "butter": the flap "ɾ" becomes 'T' and "ɚ" splits into 'ə' + 'ɹ'.
    expect("bˈʌɾɚ", {44, 156, 138, 36, 83, 123}, "flap or r-colored vowel was mapped incorrectly");

    // "bottle": a syllabic "l" (marked by the combining U+0329) folds to the
    // prefixed 'ᵊl' pair rather than being dropped.
    expect("bˈɑːɾə‍l", {44, 156, 69, 36, 42, 54},
           "a syllabic consonant was not folded onto its preceding vowel");

    // Multi-clause text: espeak-ng's per-clause newline becomes a plain word
    // separator (vocab id 16), not dropped or left as an unmapped symbol.
    expect("wʌlld\nənʌlld", {65, 138, 54, 54, 46, 16, 83, 56, 138, 54, 54, 46},
           "a clause-boundary newline was not treated as a word separator");

    // Distinct input text must produce a distinct, non-empty token sequence
    // (the property this slice exists to guarantee, replacing a fixed
    // hardcoded "hello" sequence for every request).
    const auto hello = tts_host::translate_ipa_to_kokoro_token_ids("həlˈo‍ʊ");
    const auto world = tts_host::translate_ipa_to_kokoro_token_ids("wˈɜːld");
    require(!hello.empty() && !world.empty() && hello != world,
            "distinct input texts produced the same (or an empty) token sequence");

    // A symbol with no Kokoro vocabulary slot (an unrecognized codepoint) is
    // silently dropped rather than erroring, matching the reference
    // tokenizer's behavior (ADR 0006).
    expect("h\U0001F600i", {50, 51}, "an out-of-vocabulary symbol was not silently dropped");

    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
