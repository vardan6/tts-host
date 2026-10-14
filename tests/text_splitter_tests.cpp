#include "tts_host/text_splitter.hpp"

#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void expect(const std::string &text, const std::vector<std::string> &expected,
           const std::string &message) {
  const auto actual = tts_host::split_into_sentences(text);
  if (actual != expected) {
    std::string actual_joined;
    for (const auto &chunk : actual) {
      actual_joined += "[" + chunk + "]";
    }
    std::string expected_joined;
    for (const auto &chunk : expected) {
      expected_joined += "[" + chunk + "]";
    }
    throw std::runtime_error(message + "\n  expected: " + expected_joined +
                             "\n  actual:   " + actual_joined);
  }
}

void text_with_no_terminal_punctuation_is_one_chunk() {
  expect("Hello world", {"Hello world"}, "text without sentence punctuation was split");
}

void terminal_punctuation_followed_by_whitespace_splits() {
  expect("Hello world. Second sentence!", {"Hello world.", "Second sentence!"},
         "sentences were not split on '.' and '!' followed by whitespace");
  expect("Is this working? Yes.", {"Is this working?", "Yes."},
         "sentences were not split on '?' followed by whitespace");
}

void terminal_punctuation_not_followed_by_whitespace_does_not_split() {
  expect("Version 1.5 released", {"Version 1.5 released"},
         "a decimal point without trailing whitespace was treated as a sentence end");
}

void trailing_punctuation_with_no_following_text_is_kept_as_final_chunk() {
  expect("Only one sentence.", {"Only one sentence."},
         "a single trailing sentence was not kept whole");
}

void whitespace_between_sentences_is_trimmed() {
  expect("First.   Second.", {"First.", "Second."},
         "whitespace between sentences leaked into a chunk");
}

void empty_text_produces_no_chunks() {
  expect("", {}, "empty input produced a chunk");
  expect("   ", {}, "whitespace-only input produced a chunk");
}

}  // namespace

int main() {
  try {
    text_with_no_terminal_punctuation_is_one_chunk();
    terminal_punctuation_followed_by_whitespace_splits();
    terminal_punctuation_not_followed_by_whitespace_does_not_split();
    trailing_punctuation_with_no_following_text_is_kept_as_final_chunk();
    whitespace_between_sentences_is_trimmed();
    empty_text_produces_no_chunks();
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
