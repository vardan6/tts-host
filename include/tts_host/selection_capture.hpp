#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace tts_host {

enum class SelectionCapturePolicy { Automatic, UiAutomationOnly, ClipboardOnly };

struct SelectionCaptureTarget {
  std::uintptr_t native_window = 0;
  unsigned long process_id = 0;
  std::string display_name;
  // UI Automation RuntimeId of the control that had focus when a global
  // shortcut captured. Lets Now Playing read that control's selection later
  // without taking focus. Empty until a capture identified the control.
  std::vector<int> focused_element;
};

struct SelectionCaptureResult {
  bool succeeded = false;
  std::string text;
  std::string method;
  std::string target;
  std::string failure;
  // The focused control's RuntimeId when the capture identified it, even if
  // reading its selection failed.
  std::vector<int> focused_element = {};
};

SelectionCapturePolicy parse_selection_capture_policy(std::string_view value);
std::string_view selection_capture_policy_name(SelectionCapturePolicy policy);
SelectionCaptureTarget snapshot_selection_target();
SelectionCaptureResult capture_selection_direct(const SelectionCaptureTarget &target);
// Reads the selection of target.focused_element while another window (Now
// Playing) has focus. Never changes focus or sends input; a missing window or
// control, another process, or an empty selection fails closed.
SelectionCaptureResult capture_saved_selection(const SelectionCaptureTarget &target);
// Now Playing's label for the saved Read selection target.
std::string saved_selection_target_label(const SelectionCaptureTarget &target);
// User-facing explanation of a failed capture. A saved-target failure asks
// the user to reselect and use the global shortcut.
std::string selection_capture_failure_message(const SelectionCaptureResult &result,
                                              SelectionCapturePolicy policy, bool saved_target);
// Records method, target, and failure reason (never the selected text) in the
// activity log (tts_host/activity_log.hpp).
void record_selection_capture_diagnostic(const SelectionCaptureResult &result);

}  // namespace tts_host
