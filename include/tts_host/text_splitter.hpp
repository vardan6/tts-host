#pragma once

#include <string>
#include <vector>

namespace tts_host {

// Splits normalized, speakable text into sentence-scale chunks, the "split"
// stage of the speech pipeline (docs/design/architecture.md#speech-pipeline).
// Splitting lives in the host, not clients, so synthesis can start on the
// first chunk quickly and cancellation can stop after the current chunk.
//
// Splits after '.', '!', or '?' followed by whitespace or end of input.
// Text with no sentence-ending punctuation is returned as a single chunk.
// Leading/trailing whitespace is trimmed from each chunk; empty chunks are
// dropped.
std::vector<std::string> split_into_sentences(const std::string &text);

}  // namespace tts_host
