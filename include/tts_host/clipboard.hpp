#pragma once

#include <string>

namespace tts_host {

// Reads text from the system clipboard. Throws std::runtime_error if the
// clipboard is empty, holds no text, or (on Linux) no clipboard tool is
// available.
std::string read_clipboard_text();

}  // namespace tts_host
