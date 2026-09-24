#include "tts_host/selection_capture.hpp"

#include <stdexcept>
#include <cwchar>
#include <mutex>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <UIAutomation.h>
#include <oleauto.h>
#endif

namespace tts_host {
namespace {

std::mutex diagnostics_mutex;
std::vector<std::string> diagnostics;
constexpr std::size_t kMaximumDiagnostics = 32;

}  // namespace

SelectionCapturePolicy parse_selection_capture_policy(std::string_view value) {
  if (value.empty() || value == "automatic") return SelectionCapturePolicy::Automatic;
  if (value == "uiAutomationOnly") return SelectionCapturePolicy::UiAutomationOnly;
  if (value == "clipboardOnly") return SelectionCapturePolicy::ClipboardOnly;
  throw std::invalid_argument("unknown selection capture policy: " + std::string(value));
}

std::string_view selection_capture_policy_name(SelectionCapturePolicy policy) {
  switch (policy) {
    case SelectionCapturePolicy::Automatic: return "automatic";
    case SelectionCapturePolicy::UiAutomationOnly: return "uiAutomationOnly";
    case SelectionCapturePolicy::ClipboardOnly: return "clipboardOnly";
  }
  return "automatic";
}

void record_selection_capture_diagnostic(const SelectionCaptureResult &result) {
  std::string line = "Selection capture — method: " + result.method + ", target: " +
                     (result.target.empty() ? "unknown" : result.target) + ", result: " +
                     (result.succeeded ? "success" : result.failure);
  std::lock_guard lock(diagnostics_mutex);
  if (diagnostics.size() == kMaximumDiagnostics) diagnostics.erase(diagnostics.begin());
  diagnostics.push_back(std::move(line));
}

std::vector<std::string> selection_capture_diagnostics() {
  std::lock_guard lock(diagnostics_mutex);
  return diagnostics;
}

#ifdef _WIN32
namespace {

std::string wide_to_utf8(const wchar_t *value) {
  if (value == nullptr || *value == L'\0') return {};
  const int length = static_cast<int>(std::wcslen(value));
  const int needed = WideCharToMultiByte(CP_UTF8, 0, value, length, nullptr, 0, nullptr, nullptr);
  std::string result(needed > 0 ? static_cast<std::size_t>(needed) : 0, '\0');
  if (needed > 0) WideCharToMultiByte(CP_UTF8, 0, value, length, result.data(), needed, nullptr, nullptr);
  return result;
}

template <typename T> void release(T *value) { if (value != nullptr) value->Release(); }

SelectionCaptureResult failure(const SelectionCaptureTarget &target, std::string reason) {
  return {false, {}, "UI Automation TextPattern", target.display_name, std::move(reason)};
}

}  // namespace

SelectionCaptureTarget snapshot_selection_target() {
  SelectionCaptureTarget target;
  const HWND window = GetForegroundWindow();
  target.native_window = reinterpret_cast<std::uintptr_t>(window);
  if (window == nullptr) {
    target.display_name = "no foreground window";
    return target;
  }
  GetWindowThreadProcessId(window, &target.process_id);
  wchar_t title[256]{};
  GetWindowTextW(window, title, static_cast<int>(std::size(title)));
  target.display_name = wide_to_utf8(title);
  if (target.display_name.empty()) target.display_name = "window pid " + std::to_string(target.process_id);
  return target;
}

SelectionCaptureResult capture_selection_direct(const SelectionCaptureTarget &target) {
  const HWND target_window = reinterpret_cast<HWND>(target.native_window);
  if (target_window == nullptr || !IsWindow(target_window))
    return failure(target, "the previously focused window is no longer available");
  if (GetForegroundWindow() != target_window)
    return failure(target, "focus changed before selection capture began");

  const HRESULT initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  if (FAILED(initialized) && initialized != RPC_E_CHANGED_MODE)
    return failure(target, "could not initialize Windows accessibility services");
  const bool uninitialize = SUCCEEDED(initialized);

  IUIAutomation *automation = nullptr;
  IUIAutomationElement *focused = nullptr;
  IUIAutomationTextPattern *pattern = nullptr;
  IUIAutomationTextRangeArray *ranges = nullptr;
  SelectionCaptureResult result;
  HRESULT hr = CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&automation));
  if (SUCCEEDED(hr)) hr = automation->GetFocusedElement(&focused);
  if (SUCCEEDED(hr) && GetForegroundWindow() != target_window) {
    result = failure(target, "focus changed while reading the selection");
    hr = E_ABORT;
  }
  if (SUCCEEDED(hr)) {
    int focused_process_id = 0;
    hr = focused->get_CurrentProcessId(&focused_process_id);
    // UI Automation providers for console and terminal text controls may
    // report a child/proxy native HWND rather than the foreground top-level
    // HWND. Foreground identity was checked above; the provider's process id
    // is the stable ownership check for the saved target.
    if (SUCCEEDED(hr) && static_cast<unsigned long>(focused_process_id) != target.process_id) {
      result = failure(target, "the accessibility focus no longer belongs to the saved target");
      hr = E_ABORT;
    }
  }
  if (SUCCEEDED(hr)) {
    BOOL is_password = FALSE;
    hr = focused->get_CurrentIsPassword(&is_password);
    if (SUCCEEDED(hr) && is_password) {
      result = failure(target, "protected fields cannot be read");
      hr = E_ACCESSDENIED;
    }
  }
  if (SUCCEEDED(hr)) {
    hr = focused->GetCurrentPatternAs(UIA_TextPatternId, IID_PPV_ARGS(&pattern));
    if (FAILED(hr) || pattern == nullptr)
      result = failure(target, "the focused control does not expose a TextPattern selection");
  }
  if (SUCCEEDED(hr) && pattern != nullptr) hr = pattern->GetSelection(&ranges);
  int range_count = 0;
  if (SUCCEEDED(hr) && ranges != nullptr) hr = ranges->get_Length(&range_count);
  std::wstring selected;
  for (int index = 0; SUCCEEDED(hr) && index < range_count; ++index) {
    IUIAutomationTextRange *range = nullptr;
    BSTR text = nullptr;
    hr = ranges->GetElement(index, &range);
    if (SUCCEEDED(hr)) hr = range->GetText(-1, &text);
    if (SUCCEEDED(hr) && text != nullptr) selected.append(text, SysStringLen(text));
    SysFreeString(text);
    release(range);
  }
  if (SUCCEEDED(hr) && selected.empty())
    result = failure(target, "the focused control reported an empty selection");
  else if (SUCCEEDED(hr))
    result = {true, wide_to_utf8(selected.c_str()), "UI Automation TextPattern", target.display_name, {}};
  else if (result.failure.empty())
    result = failure(target, "Windows UI Automation could not read the focused selection");

  release(ranges); release(pattern); release(focused); release(automation);
  if (uninitialize) CoUninitialize();
  return result;
}

#else
SelectionCaptureTarget snapshot_selection_target() { return {}; }
SelectionCaptureResult capture_selection_direct(const SelectionCaptureTarget &) {
  return {false, {}, "direct selection", {}, "direct selection capture is not implemented on this platform"};
}
#endif

}  // namespace tts_host
