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
    tts_host::SelectionCaptureTarget target{0, 42, "Editor", {}};
    require(tts_host::saved_selection_target_label(target).find("global shortcut") != std::string::npos,
            "Now Playing should ask for the global shortcut before a target is saved");
    const auto unsaved = tts_host::capture_saved_selection(target);
    require(!unsaved.succeeded && unsaved.text.empty(),
            "Read selection without a saved target must fail closed");
    target.focused_element = {42, 7};
    require(tts_host::saved_selection_target_label(target) == "Target: Editor",
            "Now Playing did not label the saved external target");
    target.native_window = 0;
    require(!tts_host::capture_saved_selection(target).succeeded,
            "a vanished saved window must fail closed");
    const tts_host::SelectionCaptureResult failed{false, {}, "UI Automation TextPattern (saved target)",
                                                  "Editor", "the saved control is no longer available"};
    const auto saved_message = tts_host::selection_capture_failure_message(
        failed, SelectionCapturePolicy::Automatic, true);
    require(saved_message.find("Reselect the text and use the global shortcut") != std::string::npos &&
                saved_message.find("Editor") != std::string::npos,
            "a saved-target failure did not ask the user to reselect and use the shortcut");
    const auto shortcut_message = tts_host::selection_capture_failure_message(
        failed, SelectionCapturePolicy::Automatic, false);
    require(shortcut_message.find("Safe Copy fallback is unavailable") != std::string::npos,
            "the shortcut failure lost its automatic-policy explanation");
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
