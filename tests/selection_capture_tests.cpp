#include "tts_host/activity_log.hpp"
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
    const auto diagnostics = tts_host::activity_since(0);
    require(diagnostics.size() == 1 && diagnostics.front().text.find("empty selection") != std::string::npos,
            "capture diagnostic was not retained without selected text");
    tts_host::record_activity("Speech failed: runner exited");
    const auto newer = tts_host::activity_since(diagnostics.front().sequence);
    require(newer.size() == 1 && newer.front().text == "Speech failed: runner exited" &&
                newer.front().time.size() == 8,
            "activity log did not return only entries after the given sequence");
    for (int i = 0; i < 205; ++i) tts_host::record_activity("bounded entry " + std::to_string(i));
    const auto retained = tts_host::activity_since(0);
    require(retained.size() == 200 && retained.front().text == "bounded entry 5" &&
                retained.back().text == "bounded entry 204",
            "activity log did not evict its oldest entries");
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
  return 0;
}
