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
};

struct SelectionCaptureResult {
  bool succeeded = false;
  std::string text;
  std::string method;
  std::string target;
  std::string failure;
};

SelectionCapturePolicy parse_selection_capture_policy(std::string_view value);
std::string_view selection_capture_policy_name(SelectionCapturePolicy policy);
SelectionCaptureTarget snapshot_selection_target();
SelectionCaptureResult capture_selection_direct(const SelectionCaptureTarget &target);
void record_selection_capture_diagnostic(const SelectionCaptureResult &result);
std::vector<std::string> selection_capture_diagnostics();

}  // namespace tts_host
