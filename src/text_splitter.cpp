#include "tts_host/text_splitter.hpp"

#include <cctype>

namespace tts_host {

namespace {

std::string trim(const std::string &text) {
  const auto begin = text.find_first_not_of(" \t\r\n");
  if (begin == std::string::npos) {
    return "";
  }
  const auto end = text.find_last_not_of(" \t\r\n");
  return text.substr(begin, end - begin + 1);
}

bool is_sentence_terminator(char character) {
  return character == '.' || character == '!' || character == '?';
}

}  // namespace

std::vector<std::string> split_into_sentences(const std::string &text) {
  std::vector<std::string> chunks;
  std::size_t chunk_start = 0;

  for (std::size_t index = 0; index < text.size(); ++index) {
    if (!is_sentence_terminator(text[index])) {
      continue;
    }
    const bool at_end = index + 1 == text.size();
    const bool followed_by_space = !at_end && std::isspace(static_cast<unsigned char>(text[index + 1]));
    if (!at_end && !followed_by_space) {
      continue;
    }

    auto chunk = trim(text.substr(chunk_start, index + 1 - chunk_start));
    if (!chunk.empty()) {
      chunks.push_back(std::move(chunk));
    }
    chunk_start = index + 1;
  }

  auto remainder = trim(text.substr(chunk_start));
  if (!remainder.empty()) {
    chunks.push_back(std::move(remainder));
  }

  return chunks;
}

}  // namespace tts_host
