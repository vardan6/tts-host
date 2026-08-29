#include "tts_host/clipboard.hpp"

#include <stdexcept>

#ifdef _WIN32
#include <windows.h>
#else
#include <array>
#include <cstdio>
#include <memory>
#endif

namespace tts_host {

#ifdef _WIN32

std::string read_clipboard_text() {
  if (!OpenClipboard(nullptr)) {
    throw std::runtime_error("could not open the clipboard");
  }

  const HANDLE handle = GetClipboardData(CF_UNICODETEXT);
  if (handle == nullptr) {
    CloseClipboard();
    throw std::runtime_error("the clipboard does not contain text");
  }

  const wchar_t *wide_text = static_cast<const wchar_t *>(GlobalLock(handle));
  if (wide_text == nullptr) {
    CloseClipboard();
    throw std::runtime_error("failed to lock clipboard memory");
  }

  const int required = WideCharToMultiByte(CP_UTF8, 0, wide_text, -1, nullptr, 0, nullptr, nullptr);
  std::string utf8_text;
  if (required > 1) {
    utf8_text.resize(static_cast<std::size_t>(required) - 1);
    WideCharToMultiByte(CP_UTF8, 0, wide_text, -1, utf8_text.data(), required, nullptr, nullptr);
  }

  GlobalUnlock(handle);
  CloseClipboard();
  return utf8_text;
}

#else

namespace {

// Vendors no clipboard library for Linux; shells out to whichever clipboard
// tool the desktop session provides instead (docs/requirements/product.md's
// "Command-line client accepting ... clipboard text" does not mandate a
// specific backend). Both commands are fixed literals, not built from input.
struct PipeCloser {
  void operator()(FILE *pipe) const {
    if (pipe != nullptr) {
      pclose(pipe);
    }
  }
};

std::string run_command(const char *command) {
  std::array<char, 4096> buffer{};
  std::string output;
  std::unique_ptr<FILE, PipeCloser> pipe(popen(command, "r"));
  if (!pipe) {
    return {};
  }
  std::size_t read_count = 0;
  while ((read_count = fread(buffer.data(), 1, buffer.size(), pipe.get())) > 0) {
    output.append(buffer.data(), read_count);
  }
  return output;
}

}  // namespace

std::string read_clipboard_text() {
  std::string text = run_command("wl-paste --no-newline 2>/dev/null");
  if (text.empty()) {
    text = run_command("xclip -selection clipboard -o 2>/dev/null");
  }
  if (text.empty()) {
    throw std::runtime_error(
        "could not read the clipboard: install wl-clipboard (wl-paste) or xclip");
  }
  return text;
}

#endif

}  // namespace tts_host
