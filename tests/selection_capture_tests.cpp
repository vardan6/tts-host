#include "tts_host/selection_capture.hpp"

#include <iostream>
#include <stdexcept>

void require(bool condition, const char *message) {
  if (!condition) throw std::runtime_error(message);
}

int main() {
  try {
    using tts_host::SelectionCapturePolicy;
    require(tts_host::parse_selection_capture_policy("") == SelectionCapturePolicy::Automatic,
            "missing policy must preserve the automatic default");
    require(tts_host::parse_selection_capture_policy("automatic") == SelectionCapturePolicy::Automatic,
            "automatic policy did not parse");
    require(tts_host::parse_selection_capture_policy("uiAutomationOnly") == SelectionCapturePolicy::UiAutomationOnly,
            "UI Automation policy did not parse");
    require(tts_host::parse_selection_capture_policy("clipboardOnly") == SelectionCapturePolicy::ClipboardOnly,
            "clipboard-only policy did not parse");
    require(tts_host::selection_capture_policy_name(SelectionCapturePolicy::ClipboardOnly) == "clipboardOnly",
            "policy did not serialize canonically");
    bool rejected = false;
    try { (void)tts_host::parse_selection_capture_policy("copyAnything"); }
    catch (const std::invalid_argument &) { rejected = true; }
    require(rejected, "unknown policy was accepted");
    tts_host::record_selection_capture_diagnostic(
        {false, {}, "UI Automation TextPattern", "Editor", "empty selection"});
    const auto diagnostics = tts_host::selection_capture_diagnostics();
    require(diagnostics.size() == 1 && diagnostics.front().find("empty selection") != std::string::npos,
            "capture diagnostic was not retained without selected text");
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
  return 0;
}
