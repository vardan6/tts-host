#include "tts_host/selection_capture.hpp"
#include "tts_host/activity_log.hpp"

#include <stdexcept>
#include <cwchar>

#ifdef _WIN32
#include <windows.h>
#include <UIAutomation.h>
#include <oleauto.h>
#endif

namespace tts_host {

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

std::string saved_selection_target_label(const SelectionCaptureTarget &target) {
  if (target.focused_element.empty()) return "No target yet: use the global shortcut once";
  return "Target: " + (target.display_name.empty() ? std::string("unnamed window") : target.display_name);
}

std::string selection_capture_failure_message(const SelectionCaptureResult &result,
                                              SelectionCapturePolicy policy, bool saved_target) {
  std::string message = result.method + " failed for " +
                        (result.target.empty() ? std::string("unknown") : result.target) + ": " +
                        result.failure + ". ";
  if (saved_target) return message + "Reselect the text and use the global shortcut, or copy it and use Read clipboard.";
  if (policy == SelectionCapturePolicy::Automatic)
    message += "Safe Copy fallback is unavailable for this control. ";
  return message + "Copy manually, then use Read clipboard.";
}

void record_selection_capture_diagnostic(const SelectionCaptureResult &result) {
  std::string line = "Selection capture — method: " + result.method + ", target: " +
                     (result.target.empty() ? "unknown" : result.target) + ", result: " +
                     (result.succeeded ? "success" : result.failure);
  record_activity(std::move(line));
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

constexpr char kDirectMethod[] = "UI Automation TextPattern";
constexpr char kSavedMethod[] = "UI Automation TextPattern (saved target)";

SelectionCaptureResult failure(const SelectionCaptureTarget &target, std::string reason,
                               const char *method = kDirectMethod) {
  return {false, {}, method, target.display_name, std::move(reason)};
}

class ComApartment {
 public:
  ComApartment() : result_(CoInitializeEx(nullptr, COINIT_MULTITHREADED)) {}
  ~ComApartment() { if (SUCCEEDED(result_)) CoUninitialize(); }
  bool usable() const { return SUCCEEDED(result_) || result_ == RPC_E_CHANGED_MODE; }

 private:
  HRESULT result_;
};

std::vector<int> runtime_id(IUIAutomationElement *element) {
  std::vector<int> id;
  SAFEARRAY *array = nullptr;
  if (FAILED(element->GetRuntimeId(&array)) || array == nullptr) return id;
  LONG lower = 0, upper = -1;
  SafeArrayGetLBound(array, 1, &lower);
  SafeArrayGetUBound(array, 1, &upper);
  for (LONG index = lower; index <= upper; ++index) {
    int value = 0;
    if (SUCCEEDED(SafeArrayGetElement(array, &index, &value))) id.push_back(value);
  }
  SafeArrayDestroy(array);
  return id;
}

// Shared by both capture paths once the control is found: ownership,
// protected-field, and TextPattern selection checks.
SelectionCaptureResult read_element_selection(IUIAutomationElement *element,
                                              const SelectionCaptureTarget &target,
                                              const char *method) {
  int element_process_id = 0;
  HRESULT hr = element->get_CurrentProcessId(&element_process_id);
  // UI Automation providers for console and terminal text controls may
  // report a child/proxy native HWND rather than the foreground top-level
  // HWND. The provider's process id is the stable ownership check for the
  // saved target.
  if (FAILED(hr)) return failure(target, "Windows UI Automation could not identify the control", method);
  if (static_cast<unsigned long>(element_process_id) != target.process_id)
    return failure(target, "the accessibility focus no longer belongs to the saved target", method);
  auto with_element = [&](SelectionCaptureResult result) {
    result.focused_element = runtime_id(element);
    return result;
  };
  BOOL is_password = FALSE;
  hr = element->get_CurrentIsPassword(&is_password);
  if (SUCCEEDED(hr) && is_password)
    return with_element(failure(target, "protected fields cannot be read", method));

  IUIAutomationTextPattern *pattern = nullptr;
  IUIAutomationTextRangeArray *ranges = nullptr;
  SelectionCaptureResult result;
  if (SUCCEEDED(hr)) {
    hr = element->GetCurrentPatternAs(UIA_TextPatternId, IID_PPV_ARGS(&pattern));
    if (FAILED(hr) || pattern == nullptr) {
      result = failure(target, "the focused control does not expose a TextPattern selection", method);
      hr = E_NOINTERFACE;
    }
  }
  if (SUCCEEDED(hr)) hr = pattern->GetSelection(&ranges);
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
    result = failure(target, "the focused control reported an empty selection", method);
  else if (SUCCEEDED(hr))
    result = {true, wide_to_utf8(selected.c_str()), method, target.display_name, {}};
  else if (result.failure.empty())
    result = failure(target, "Windows UI Automation could not read the focused selection", method);
  release(ranges); release(pattern);
  return with_element(std::move(result));
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

  ComApartment apartment;
  if (!apartment.usable()) return failure(target, "could not initialize Windows accessibility services");
  IUIAutomation *automation = nullptr;
  IUIAutomationElement *focused = nullptr;
  SelectionCaptureResult result;
  HRESULT hr = CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&automation));
  if (SUCCEEDED(hr)) hr = automation->GetFocusedElement(&focused);
  if (SUCCEEDED(hr) && GetForegroundWindow() != target_window)
    result = failure(target, "focus changed while reading the selection");
  else if (SUCCEEDED(hr) && focused != nullptr)
    result = read_element_selection(focused, target, kDirectMethod);
  else
    result = failure(target, "Windows UI Automation could not read the focused selection");
  release(focused); release(automation);
  return result;
}

SelectionCaptureResult capture_saved_selection(const SelectionCaptureTarget &target) {
  if (target.focused_element.empty())
    return failure(target, "no target saved yet", kSavedMethod);
  const HWND target_window = reinterpret_cast<HWND>(target.native_window);
  if (target_window == nullptr || !IsWindow(target_window))
    return failure(target, "the saved window is no longer available", kSavedMethod);
  DWORD window_process_id = 0;
  GetWindowThreadProcessId(target_window, &window_process_id);
  if (window_process_id != target.process_id)
    return failure(target, "the saved window now belongs to another process", kSavedMethod);

  ComApartment apartment;
  if (!apartment.usable())
    return failure(target, "could not initialize Windows accessibility services", kSavedMethod);
  IUIAutomation *automation = nullptr;
  IUIAutomationElement *root = nullptr;
  IUIAutomationCondition *condition = nullptr;
  IUIAutomationElement *element = nullptr;
  SAFEARRAY *id = SafeArrayCreateVector(VT_I4, 0, static_cast<ULONG>(target.focused_element.size()));
  if (id == nullptr) return failure(target, "out of memory", kSavedMethod);
  for (LONG index = 0; index < static_cast<LONG>(target.focused_element.size()); ++index) {
    int value = target.focused_element[static_cast<std::size_t>(index)];
    SafeArrayPutElement(id, &index, &value);
  }
  VARIANT id_value;
  VariantInit(&id_value);
  id_value.vt = VT_ARRAY | VT_I4;
  id_value.parray = id;

  HRESULT hr = CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&automation));
  if (SUCCEEDED(hr)) hr = automation->ElementFromHandle(target_window, &root);
  if (SUCCEEDED(hr)) hr = automation->CreatePropertyCondition(UIA_RuntimeIdPropertyId, id_value, &condition);
  if (SUCCEEDED(hr)) hr = root->FindFirst(TreeScope_Subtree, condition, &element);
  SelectionCaptureResult result;
  if (SUCCEEDED(hr) && element != nullptr)
    result = read_element_selection(element, target, kSavedMethod);
  else if (SUCCEEDED(hr))
    result = failure(target, "the saved control is no longer available", kSavedMethod);
  else
    result = failure(target, "Windows UI Automation could not find the saved control", kSavedMethod);
  release(element); release(condition); release(root); release(automation);
  VariantClear(&id_value);
  return result;
}

#else
SelectionCaptureTarget snapshot_selection_target() { return {}; }
SelectionCaptureResult capture_selection_direct(const SelectionCaptureTarget &) {
  return {false, {}, "direct selection", {}, "direct selection capture is not implemented on this platform"};
}
SelectionCaptureResult capture_saved_selection(const SelectionCaptureTarget &) {
  return {false, {}, "direct selection", {}, "direct selection capture is not implemented on this platform"};
}
#endif

}  // namespace tts_host
