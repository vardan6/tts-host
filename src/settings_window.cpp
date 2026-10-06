#include "tts_host/settings_window.hpp"

#include <stdexcept>

#ifdef _WIN32
#include <windows.h>
#include <shobjidl.h>
#include <commctrl.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cwchar>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

#include "tts_host/catalogue_download.hpp"
#include "tts_host/config_persistence.hpp"
#include "tts_host/global_hotkey.hpp"
#include "tts_host/model_catalogue.hpp"
#include "tts_host/model_import.hpp"
#include "tts_host/model_registry.hpp"
#include "tts_host/selection_capture.hpp"
#include "tts_host/startup_registration.hpp"
#include "tts_host/model_session.hpp"
#include "tts_host/playback_sink.hpp"

namespace tts_host {
namespace {

constexpr int kOutputDeviceComboId = 101;
constexpr int kServerHostEditId = 102;
constexpr int kServerPortEditId = 103;
constexpr int kDefaultProfileComboId = 104;
constexpr int kModelComboId = 105;
constexpr int kLoadModelButtonId = 106;
constexpr int kUnloadModelButtonId = 107;
constexpr int kImportModelButtonId = 108;
constexpr int kCatalogueComboId = 109;
constexpr int kDownloadModelButtonId = 110;
constexpr int kReadSelectionHotkeyEditId = 111;
constexpr int kCheckHotkeyButtonId = 112;
constexpr int kSelectionPolicyComboId = 113;
constexpr int kSpeechSpeedComboId = 114;
constexpr int kPlaybackSkipComboId = 115;
constexpr int kStartAtLoginCheckboxId = 116;
constexpr int kSettingsTabsId = 117;
constexpr int kSettingsHotkeyProbeId = 2;

// Tab order of the Settings pages; docs/design/architecture.md#desktop-integration.
constexpr int kGeneralPage = 0;
constexpr int kPlaybackPage = 1;
constexpr int kModelsPage = 2;
constexpr int kServerPage = 3;

// Poll actual speech activity when sharing the tray manager; busy sessions
// are never unloaded. Standalone Settings uses the same five-second poll.
constexpr UINT_PTR kIdleTimerId = 1;
constexpr UINT kIdleTimerIntervalMs = 5000;

// Polls modelRegistry.directories for changes when modelRegistry.watchForChanges
// is true (docs/design/architecture.md#desktop-integration), so an install
// dropped into a registry directory by something other than Import… (a synced
// folder, a manual copy) still shows up without restarting. Same interval and
// mechanism as the idle timer: the settings window already owns a live message
// loop, so a second WM_TIMER poll avoids a second OS-specific watch API
// (ReadDirectoryChangesW) for one more low-frequency check.
constexpr UINT_PTR kDirectoryWatchTimerId = 2;
constexpr UINT kDirectoryWatchTimerIntervalMs = 5000;

// Edits the tray-owned document and retains the last successfully saved value.
// Tray sessions outlive this window; standalone Settings owns its own manager.
struct SettingsState {
  SettingsState(ConfigDocument &config, const std::filesystem::path &runner_directory,
                ModelSessionManager *shared_sessions)
      : document(config), saved_value(config.value),
        owned_sessions(shared_sessions ? nullptr : std::make_unique<ModelSessionManager>(runner_directory)),
        sessions(shared_sessions ? *shared_sessions : *owned_sessions) {}

  ConfigDocument &document;
  nlohmann::json saved_value;
  std::unique_ptr<ModelSessionManager> owned_sessions;
  ModelSessionManager &sessions;
  // Model ids in combo-box order; the combo itself shows display names.
  std::vector<std::string> model_ids;
  // Catalogue entry ids in combo-box order, mirroring model_ids above.
  std::vector<std::string> catalogue_ids;
  // The lower pane is the one durable place for user-visible settings-window
  // output: model information, progress, successes, and actionable failures.
  HWND activity_log = nullptr;
  HWND tabs = nullptr;
  // Every page control with its tab index; switching tabs shows one page.
  std::vector<std::pair<HWND, int>> page_controls;
  // The scan the model views currently reflect, so the directory-watch timer
  // can tell a real change from scan_model_registry re-validating the same
  // packages on every poll.
  ModelRegistryScan last_scan;
};

void refresh_model_views(SettingsState &state, HWND window);

void show_settings_page(SettingsState &state, int page) {
  for (const auto &[control, control_page] : state.page_controls) {
    ShowWindow(control, control_page == page ? SW_SHOW : SW_HIDE);
  }
}
void append_activity_log(SettingsState &state, const std::wstring &text);
WNDPROC original_hotkey_edit_proc = nullptr;

std::wstring utf8_to_wide(const std::string &text) {
  if (text.empty()) {
    return std::wstring();
  }
  const int required = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
  std::wstring wide;
  wide.resize(static_cast<std::size_t>(required) - 1);
  MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, wide.data(), required);
  return wide;
}

std::string wide_to_utf8(const wchar_t *wide_text) {
  const int required = WideCharToMultiByte(CP_UTF8, 0, wide_text, -1, nullptr, 0, nullptr, nullptr);
  std::string text;
  if (required > 1) {
    text.resize(static_cast<std::size_t>(required) - 1);
    WideCharToMultiByte(CP_UTF8, 0, wide_text, -1, text.data(), required, nullptr, nullptr);
  }
  return text;
}

void save_config(SettingsState &state) {
  try {
    save_config_document(state.document);
    state.saved_value = state.document.value;
  } catch (...) {
    state.document.value = state.saved_value;
    throw;
  }
}

// Fills the combo box with "System Default" plus every enumerated output
// device, keeping the currently configured device selected even if it is no
// longer attached (so the control reflects the real config value rather than
// silently changing it out from under the user).
void populate_output_device_combo(HWND combo, const std::string &current_value) {
  SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"System Default"));

  std::vector<std::string> devices;
  try {
    devices = list_output_devices();
  } catch (const std::exception &) {
    // Enumeration can fail (no audio subsystem, no devices); the user can
    // still pick "System Default" or keep whatever value was already pinned.
  }

  int current_index = current_value == kSystemDefaultOutputDevice ? 0 : -1;
  int index = 1;
  for (const auto &device : devices) {
    SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(utf8_to_wide(device).c_str()));
    if (device == current_value) {
      current_index = index;
    }
    ++index;
  }

  if (current_index < 0) {
    SendMessageW(combo, CB_ADDSTRING, 0,
                reinterpret_cast<LPARAM>(utf8_to_wide(current_value).c_str()));
    current_index = index;
  }

  SendMessageW(combo, CB_SETCURSEL, current_index, 0);
}

void on_output_device_selected(SettingsState &state, HWND combo) {
  const int index = static_cast<int>(SendMessageW(combo, CB_GETCURSEL, 0, 0));
  if (index < 0) {
    return;
  }
  wchar_t buffer[256];
  SendMessageW(combo, CB_GETLBTEXT, index, reinterpret_cast<LPARAM>(buffer));
  const std::string selected = wide_to_utf8(buffer);
  state.document.value["audio"]["outputDevice"] =
      selected == "System Default" ? kSystemDefaultOutputDevice : selected;
  save_config(state);
}

void populate_speech_speed_combo(HWND combo, double current_speed) {
  const wchar_t *labels[] = {L"0.5×", L"0.75×", L"1.0×", L"1.25×", L"1.5×", L"1.75×", L"2.0×"};
  for (int index = 0; index < static_cast<int>(std::size(labels)); ++index) {
    SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(labels[index]));
    if (std::abs(std::wcstod(labels[index], nullptr) - current_speed) < 0.001) {
      SendMessageW(combo, CB_SETCURSEL, index, 0);
    }
  }
}

void on_speech_speed_selected(SettingsState &state, HWND combo) {
  wchar_t value[16];
  const auto index = SendMessageW(combo, CB_GETCURSEL, 0, 0);
  if (index == CB_ERR) return;
  SendMessageW(combo, CB_GETLBTEXT, index, reinterpret_cast<LPARAM>(value));
  state.document.value["audio"]["speechSpeed"] = std::wcstod(value, nullptr);
  save_config(state);
}

void populate_playback_skip_combo(HWND combo, int current_seconds) {
  for (int seconds = 1; seconds <= 30; ++seconds) {
    const auto label = std::to_wstring(seconds) + L" seconds";
    const auto index = SendMessageW(combo, CB_ADDSTRING, 0,
                                    reinterpret_cast<LPARAM>(label.c_str()));
    if (seconds == current_seconds) SendMessageW(combo, CB_SETCURSEL, index, 0);
  }
}

void on_playback_skip_selected(SettingsState &state, HWND combo) {
  const auto index = SendMessageW(combo, CB_GETCURSEL, 0, 0);
  if (index == CB_ERR) return;
  wchar_t value[32];
  SendMessageW(combo, CB_GETLBTEXT, index, reinterpret_cast<LPARAM>(value));
  state.document.value["audio"]["playbackSkipSeconds"] = std::wcstol(value, nullptr, 10);
  save_config(state);
}

void populate_selection_policy_combo(HWND combo, const ConfigDocument &document) {
  const auto selection = document.value.value("selection", nlohmann::json::object());
  const auto current = parse_selection_capture_policy(selection.value("capturePolicy", "automatic"));
  const SelectionCapturePolicy policies[] = {
      SelectionCapturePolicy::Automatic,
      SelectionCapturePolicy::UiAutomationOnly,
      SelectionCapturePolicy::ClipboardOnly,
  };
  for (int index = 0; index < 3; ++index) {
    const auto name = selection_capture_policy_name(policies[index]);
    const std::wstring label = policies[index] == SelectionCapturePolicy::Automatic
                                   ? L"Automatic (direct, then safe Copy if supported)"
                               : policies[index] == SelectionCapturePolicy::UiAutomationOnly
                                   ? L"UI Automation only"
                                   : L"Clipboard only (read existing clipboard)";
    SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
    SendMessageW(combo, CB_SETITEMDATA, index, static_cast<LPARAM>(policies[index]));
    if (policies[index] == current) SendMessageW(combo, CB_SETCURSEL, index, 0);
    (void)name;
  }
}

void on_selection_policy_selected(SettingsState &state, HWND combo) {
  const int index = static_cast<int>(SendMessageW(combo, CB_GETCURSEL, 0, 0));
  if (index < 0) return;
  const auto policy = static_cast<SelectionCapturePolicy>(SendMessageW(combo, CB_GETITEMDATA, index, 0));
  state.document.value["selection"]["capturePolicy"] = selection_capture_policy_name(policy);
  save_config(state);
  append_activity_log(state,
                      policy == SelectionCapturePolicy::UiAutomationOnly
                          ? L"Selection policy saved: UI Automation only; the clipboard is never changed."
                      : policy == SelectionCapturePolicy::ClipboardOnly
                          ? L"Selection policy saved: Ctrl+F8 reads the existing clipboard; copy the selection first."
                          : L"Selection policy saved: automatic direct capture; safe Copy is used only when a compatible adapter is available.");
}

// Controls created with CreateWindowExW inherit no font, so Windows draws them
// with the ancient bitmap SYSTEM_FONT: bold, oversized, and wide enough to
// overflow the fixed pixel layout below (it clipped the Import… caption and the
// default-profile label). Use the shell's own UI font — Segoe UI on Windows
// 10/11 — which is the metric the layout coordinates assume. The font outlives
// every control that selects it, hence the function-local static.
HFONT shell_ui_font() {
  static const HFONT font = []() -> HFONT {
    NONCLIENTMETRICSW metrics{};
    metrics.cbSize = sizeof(metrics);
    if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0) != 0) {
      if (const HFONT created = CreateFontIndirectW(&metrics.lfMessageFont)) {
        return created;
      }
    }
    return static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
  }();
  return font;
}

BOOL CALLBACK apply_shell_ui_font(HWND child, LPARAM font) {
  SendMessageW(child, WM_SETFONT, static_cast<WPARAM>(font), MAKELPARAM(TRUE, 0));
  return TRUE;
}

// Fills the combo box with every profile name from config's "profiles" map,
// preselecting whichever one languageDefaults.en currently names. English is
// the only language surfaced end to end so far (see roadmap and
// resolve_default_runner_selection in src/main.cpp).
void populate_default_profile_combo(HWND combo, const ConfigDocument &document) {
  const auto &profiles = document.value.at("profiles");
  const std::string current_value = document.value.at("languageDefaults").contains("en")
                                        ? document.value.at("languageDefaults").at("en").get<std::string>()
                                        : std::string();

  int index = 0;
  int current_index = -1;
  for (auto it = profiles.begin(); it != profiles.end(); ++it, ++index) {
    SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(utf8_to_wide(it.key()).c_str()));
    if (it.key() == current_value) {
      current_index = index;
    }
  }

  if (current_index < 0 && !current_value.empty()) {
    SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(utf8_to_wide(current_value).c_str()));
    current_index = index;
  }

  SendMessageW(combo, CB_SETCURSEL, current_index, 0);
}

void on_default_profile_selected(SettingsState &state, HWND combo) {
  const int index = static_cast<int>(SendMessageW(combo, CB_GETCURSEL, 0, 0));
  if (index < 0) {
    return;
  }
  wchar_t buffer[256];
  SendMessageW(combo, CB_GETLBTEXT, index, reinterpret_cast<LPARAM>(buffer));
  state.document.value["languageDefaults"]["en"] = wide_to_utf8(buffer);
  save_config(state);
}

std::string edit_text(HWND edit) {
  const int length = GetWindowTextLengthW(edit);
  std::wstring text(static_cast<std::size_t>(length) + 1, L'\0');
  GetWindowTextW(edit, text.data(), length + 1);
  text.resize(static_cast<std::size_t>(length));
  return wide_to_utf8(text.c_str());
}

void append_activity_log(SettingsState &state, const std::wstring &text) {
  if (state.activity_log == nullptr) {
    return;
  }
  const int length = GetWindowTextLengthW(state.activity_log);
  SendMessageW(state.activity_log, EM_SETSEL, length, length);
  if (length != 0) {
    SendMessageW(state.activity_log, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(L"\r\n"));
  }
  SendMessageW(state.activity_log, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(text.c_str()));
  SendMessageW(state.activity_log, EM_SCROLLCARET, 0, 0);
  // Loading and download setup block this UI thread, so repaint the message
  // announcing that work before entering either operation.
  UpdateWindow(state.activity_log);
}

std::wstring captured_hotkey_text(WPARAM virtual_key) {
  std::wstring text;
  const bool control_down = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
  const bool alt_down = (GetKeyState(VK_MENU) & 0x8000) != 0;
  const bool shift_down = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
  const bool windows_down = (GetKeyState(VK_LWIN) & 0x8000) != 0 ||
                            (GetKeyState(VK_RWIN) & 0x8000) != 0;
  if (control_down) {
    text += L"Ctrl + ";
  }
  if (alt_down) {
    text += L"Alt + ";
  }
  if (shift_down) {
    text += L"Shift + ";
  }
  if (windows_down) {
    text += L"Win + ";
  }

  if ((virtual_key >= 'A' && virtual_key <= 'Z') || (virtual_key >= '0' && virtual_key <= '9')) {
    text += static_cast<wchar_t>(virtual_key);
  } else if (virtual_key >= VK_F1 && virtual_key <= VK_F24) {
    text += L"F" + std::to_wstring(virtual_key - VK_F1 + 1);
  }
  return control_down || alt_down || shift_down || windows_down ? text : std::wstring();
}

// Restore controls after a rejected save so the form matches the live document.
void restore_saved_controls(SettingsState &state, HWND window) {
  auto reset = [&](int id) {
    const auto control = GetDlgItem(window, id);
    SendMessageW(control, CB_RESETCONTENT, 0, 0);
    return control;
  };
  const auto &value = state.document.value;
  populate_output_device_combo(reset(kOutputDeviceComboId), value["audio"]["outputDevice"].get<std::string>());
  populate_speech_speed_combo(reset(kSpeechSpeedComboId), value["audio"].value("speechSpeed", 1.0));
  populate_playback_skip_combo(reset(kPlaybackSkipComboId), value["audio"].value("playbackSkipSeconds", 5));
  populate_selection_policy_combo(reset(kSelectionPolicyComboId), state.document);
  populate_default_profile_combo(reset(kDefaultProfileComboId), state.document);
  SetDlgItemTextW(window, kServerHostEditId, utf8_to_wide(value["server"]["host"].get<std::string>()).c_str());
  SetDlgItemTextW(window, kServerPortEditId, std::to_wstring(value["server"]["port"].get<int>()).c_str());
  SetDlgItemTextW(window, kReadSelectionHotkeyEditId,
                  utf8_to_wide(value["hotkeys"].value("readSelection", "")).c_str());
}

LRESULT CALLBACK hotkey_edit_proc(HWND edit, UINT message, WPARAM wparam, LPARAM lparam) {
  if (message == WM_KEYDOWN || message == WM_SYSKEYDOWN) {
    const auto captured = captured_hotkey_text(wparam);
    if (!captured.empty() && captured.back() != L' ') {
      SetWindowTextW(edit, captured.c_str());
      SendMessageW(edit, EM_SETSEL, 0, -1);
      auto *state = reinterpret_cast<SettingsState *>(
          GetWindowLongPtrW(GetParent(edit), GWLP_USERDATA));
      if (state != nullptr) {
        append_activity_log(*state, L"Captured shortcut: " + captured + L". Check and save it to apply.");
      }
      return 0;
    }
  }
  // Suppress the character message after a captured chord, otherwise the edit
  // control inserts the final key after the canonical chord above.
  if (message == WM_CHAR || message == WM_SYSCHAR) {
    return 0;
  }
  return CallWindowProcW(original_hotkey_edit_proc, edit, message, wparam, lparam);
}

void on_check_hotkey_clicked(SettingsState &state, HWND window, HWND edit) {
  const std::string configured = edit_text(edit);
  if (configured.empty()) {
    state.document.value["hotkeys"]["readSelection"] = "";
    save_config(state);
    append_activity_log(state, L"Selection hotkey disabled and saved. Close Settings to apply.");
    return;
  }

  std::string error;
  const auto hotkey = parse_global_hotkey(configured, error);
  if (!hotkey.has_value()) {
    append_activity_log(state, utf8_to_wide("Invalid shortcut: " + error));
    return;
  }

  // Registering then immediately unregistering is Windows' only reliable
  // availability check: there is no API that enumerates every application's
  // global hotkeys. The tray releases its own registration before opening this
  // window, so changing an already-active TTS Host binding does not self-report
  // as a conflict.
  if (!RegisterHotKey(window, kSettingsHotkeyProbeId,
                      hotkey->modifiers | MOD_NOREPEAT, hotkey->virtual_key)) {
    append_activity_log(state, L"Shortcut unavailable: Windows or another application is using it.");
    return;
  }
  UnregisterHotKey(window, kSettingsHotkeyProbeId);
  state.document.value["hotkeys"]["readSelection"] = configured;
  save_config(state);
  append_activity_log(state, L"Shortcut available and saved. Close Settings to apply.");
}

// server.host/server.port cannot be applied live (docs/design/architecture.md#live-reload),
// so these are only written on focus loss, once the user has finished editing, rather than
// on every keystroke like the output-device combo's immediate selection.

void on_server_host_changed(SettingsState &state, HWND edit) {
  wchar_t buffer[256];
  GetWindowTextW(edit, buffer, static_cast<int>(std::size(buffer)));
  const std::string host = wide_to_utf8(buffer);
  if (host.empty()) {
    return;  // schema requires minLength 1; keep the last valid host instead of writing an empty one.
  }
  state.document.value["server"]["host"] = host;
  save_config(state);
}

void on_server_port_changed(SettingsState &state, HWND edit) {
  wchar_t buffer[16];
  GetWindowTextW(edit, buffer, static_cast<int>(std::size(buffer)));
  const std::string text = wide_to_utf8(buffer);
  if (text.empty()) {
    return;
  }
  const int port = _wtoi(buffer);
  if (port < 1 || port > 65535) {
    return;  // out of the schema's range; keep the last valid port instead of writing an invalid one.
  }
  state.document.value["server"]["port"] = port;
  save_config(state);
}

void on_start_at_login_clicked(SettingsState &state, HWND checkbox) {
  const bool enabled = SendMessageW(checkbox, BM_GETCHECK, 0, 0) == BST_CHECKED;
  const auto previous_document = state.document.value;
  const bool previous = state.document.value
                            .value("startup", nlohmann::json::object())
                            .value("launchOnLogin", true);
  try {
    set_start_at_login(enabled, state.document.paths.config_path);
    state.document.value["startup"]["launchOnLogin"] = enabled;
    save_config(state);
    append_activity_log(state, enabled ? L"Start at login enabled."
                                       : L"Start at login disabled.");
  } catch (const std::exception &error) {
    state.document.value = previous_document;
    SendMessageW(checkbox, BM_SETCHECK, previous ? BST_CHECKED : BST_UNCHECKED, 0);
    try {
      set_start_at_login(previous, state.document.paths.config_path);
    } catch (const std::exception &rollback_error) {
      append_activity_log(state, L"Could not restore the previous startup registration: " +
                                     utf8_to_wide(rollback_error.what()));
    }
    append_activity_log(state, L"Could not update start at login: " +
                                   utf8_to_wide(error.what()));
  }
}

// Fills the combo box with every discovered package and records the matching
// model ids, which is what load() actually takes. Selection defaults to the
// first package so the Load button is meaningful without a pick first.
void populate_model_combo(HWND combo, SettingsState &state, const ModelRegistryScan &scan) {
  for (const auto &package : scan.discovered_packages) {
    const auto label = package.display_name + " (" + package.id + ")";
    SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(utf8_to_wide(label).c_str()));
    state.model_ids.push_back(package.id);
  }
  if (!state.model_ids.empty()) {
    SendMessageW(combo, CB_SETCURSEL, 0, 0);
  }
}

void report_session_status(SettingsState &state) {
  const auto &status = state.sessions.status();
  if (!status.loaded) {
    append_activity_log(state, L"No model loaded.");
    return;
  }
  append_activity_log(state, L"Loaded: " + utf8_to_wide(status.display_name) + L" (" +
                                 utf8_to_wide(status.model_id) + L", " +
                                 utf8_to_wide(status.engine) + L")");
}

void on_load_model_clicked(SettingsState &state, HWND combo) {
  const int index = static_cast<int>(SendMessageW(combo, CB_GETCURSEL, 0, 0));
  if (index < 0 || static_cast<std::size_t>(index) >= state.model_ids.size()) {
    append_activity_log(state, L"Select a model to load.");
    return;
  }

  const auto &model_id = state.model_ids[static_cast<std::size_t>(index)];
  append_activity_log(state, L"Loading " + utf8_to_wide(model_id) + L"…");
  try {
    state.sessions.load(model_id, state.document);
  } catch (const std::exception &error) {
    // A model that will not load is ordinary (missing runner, bad weights);
    // report it in the window instead of taking the process down.
    append_activity_log(state, L"Load failed: " + utf8_to_wide(error.what()));
    return;
  }
  report_session_status(state);
}

// Fills the combo box with every compiled-in catalogue entry
// (docs/design/architecture.md#download-catalogue), labelled the same way
// `--list-catalogue` prints them: GET/HAVE, size, and licence, so the choice
// before downloading is visible without a separate details view.
void populate_catalogue_combo(HWND combo, SettingsState &state, const ModelRegistryScan &scan) {
  for (const auto &entry : model_catalogue()) {
    const bool installed = catalogue_entry_installed(entry, scan);
    const auto label = std::string(installed ? "HAVE " : "GET  ") + entry.display_name + " (" +
                        describe_download_size(catalogue_entry_size_bytes(entry)) + ", " +
                        describe_catalogue_license(entry) + ")";
    SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(utf8_to_wide(label).c_str()));
    state.catalogue_ids.push_back(entry.id);
  }
  if (!state.catalogue_ids.empty()) {
    SendMessageW(combo, CB_SETCURSEL, 0, 0);
  }
}

void on_download_model_clicked(SettingsState &state, HWND combo, HWND window) {
  const int index = static_cast<int>(SendMessageW(combo, CB_GETCURSEL, 0, 0));
  if (index < 0 || static_cast<std::size_t>(index) >= state.catalogue_ids.size()) {
    append_activity_log(state, L"Select a catalogue entry to download.");
    return;
  }

  const auto &entries = model_catalogue();
  const auto &entry_id = state.catalogue_ids[static_cast<std::size_t>(index)];
  const auto found = std::find_if(entries.begin(), entries.end(),
                                  [&](const auto &entry) { return entry.id == entry_id; });
  if (found == entries.end()) {
    return;
  }
  if (catalogue_entry_installed(*found, state.last_scan)) {
    append_activity_log(state, utf8_to_wide(found->id) + L" is already installed.");
    return;
  }

  const auto &directories = state.document.value.at("modelRegistry").at("directories");
  if (directories.empty()) {
    append_activity_log(state, L"No model directory is configured to download into.");
    return;
  }
  const auto destination_root = resolve_registry_directory(
      state.document.paths.config_path, directories.front().get<std::string>());

  try {
    append_activity_log(state, L"Downloading " + utf8_to_wide(found->display_name) + L"…");
    download_catalogue_entry(
        *found, destination_root, state.document, fetch_url_to_file,
        [&state](const DownloadProgress &progress) {
          append_activity_log(state, utf8_to_wide(progress.relative_path) + L": " +
                                         std::to_wstring(progress.bytes_downloaded) + L" / " +
                                         std::to_wstring(progress.bytes_total) + L" bytes");
        });
    refresh_model_views(state, window);
    append_activity_log(state, L"Downloaded " + utf8_to_wide(found->display_name));
  } catch (const std::exception &error) {
    // Transport failures and checksum mismatches are ordinary (flaky network,
    // corrupted partial file); report them in the window instead of taking
    // the process down.
    append_activity_log(state, L"Download failed: " + utf8_to_wide(error.what()));
  }
}

void on_idle_timer(SettingsState &state) {
  const auto idle_unload_seconds =
      state.document.value["modelRegistry"]["idleUnloadSeconds"].get<int>();
  const auto before = state.sessions.status();
  state.sessions.unload_if_idle(std::chrono::seconds(idle_unload_seconds));
  const auto after = state.sessions.status();
  if (before.loaded != after.loaded || before.model_id != after.model_id)
    report_session_status(state);
}

void on_unload_model_clicked(SettingsState &state) {
  try {
    state.sessions.unload();
  } catch (const std::exception &error) {
    append_activity_log(state, L"Unload failed: " + utf8_to_wide(error.what()));
    return;
  }
  report_session_status(state);
}

// Model manifests are validated by scan_model_registry before their details
// reach the read-only activity log; the combo box above it is what the user
// acts on.
std::wstring format_installed_models(const ModelRegistryScan &scan) {
  std::wstring text;
  if (scan.discovered_packages.empty()) {
    text = L"No compatible installed models were found.";
  }
  for (const auto &package : scan.discovered_packages) {
    if (!text.empty()) {
      text += L"\r\n\r\n";
    }
    const auto &license = package.manifest.at("license");
    text += utf8_to_wide(package.display_name);
    text += L" (" + utf8_to_wide(package.id) + L")\r\nLicence: ";
    text += utf8_to_wide(license.at("name").get<std::string>());
    text += L"\r\n";
    text += utf8_to_wide(license.at("url").get<std::string>());
  }

  if (!scan.unsupported_entries.empty()) {
    if (!text.empty()) {
      text += L"\r\n\r\n";
    }
    text += L"Unsupported packages:";
    for (const auto &entry : scan.unsupported_entries) {
      text += L"\r\n";
      text += entry.path.wstring();
      text += L"\r\n  Reason: ";
      text += utf8_to_wide(entry.reason);
    }
  }
  return text;
}

// Folder picker for Import…: the user chooses a package directory (the folder
// holding model.json), not a file, because a model package is a directory
// (docs/design/architecture.md#model-packages-and-discovery). Returns an empty
// string when the dialog is cancelled or unavailable.
std::wstring pick_model_package_folder(HWND owner) {
  std::wstring chosen;
  const HRESULT initialize_result =
      CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
  // RPC_E_CHANGED_MODE means COM is already initialized on this thread in
  // another mode; the dialog still works, but this call must not uninitialize
  // what it did not initialize.
  const bool initialized_here = SUCCEEDED(initialize_result);

  IFileOpenDialog *dialog = nullptr;
  if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                 IID_PPV_ARGS(&dialog)))) {
    DWORD options = 0;
    if (SUCCEEDED(dialog->GetOptions(&options))) {
      dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
    }
    dialog->SetTitle(L"Select a model package folder");
    if (SUCCEEDED(dialog->Show(owner))) {
      IShellItem *item = nullptr;
      if (SUCCEEDED(dialog->GetResult(&item))) {
        PWSTR path = nullptr;
        if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
          chosen = path;
          CoTaskMemFree(path);
        }
        item->Release();
      }
    }
    dialog->Release();
  }

  if (initialized_here) {
    CoUninitialize();
  }
  return chosen;
}

// Re-reads the registry into both model views and records the scan they now
// reflect. Importing and directory watching are what changes what is
// installed while the window is open; the resident session is left alone, so
// a model loaded before either stays loaded.
void refresh_model_views(SettingsState &state, HWND window) {
  state.last_scan = scan_model_registry(state.document);
  const HWND combo = GetDlgItem(window, kModelComboId);
  if (combo != nullptr) {
    SendMessageW(combo, CB_RESETCONTENT, 0, 0);
    state.model_ids.clear();
    populate_model_combo(combo, state, state.last_scan);
  }
  append_activity_log(state, L"Installed models and licences updated:\r\n" +
                                 format_installed_models(state.last_scan));
  const HWND catalogue_combo = GetDlgItem(window, kCatalogueComboId);
  if (catalogue_combo != nullptr) {
    SendMessageW(catalogue_combo, CB_RESETCONTENT, 0, 0);
    state.catalogue_ids.clear();
    populate_catalogue_combo(catalogue_combo, state, state.last_scan);
  }
}

// Polls modelRegistry.directories when modelRegistry.watchForChanges is true;
// a no-op scan (nothing added, removed, or changed reason) skips the refresh
// so the combo selection and display scroll position are not disturbed on
// every 5 s tick.
void on_directory_watch_timer(SettingsState &state, HWND window) {
  if (!state.document.value["modelRegistry"]["watchForChanges"].get<bool>()) {
    return;
  }
  const auto scan = scan_model_registry(state.document);
  if (registry_scan_changed(state.last_scan, scan)) {
    refresh_model_views(state, window);
  }
}

void on_import_model_clicked(SettingsState &state, HWND window) {
  const auto folder = pick_model_package_folder(window);
  if (folder.empty()) {
    return;  // Cancelled; leave the status line saying whatever it said before.
  }

  append_activity_log(state, L"Importing " + folder + L"…");
  try {
    const auto imported = import_model_package(std::filesystem::path(folder), state.document);
    refresh_model_views(state, window);
    append_activity_log(state, L"Imported " + utf8_to_wide(imported.display_name) + L" (" +
                                   utf8_to_wide(imported.id) + L")");
  } catch (const std::exception &error) {
    // Invalid, incomplete, and duplicate packages are ordinary user mistakes:
    // report the registry's actionable reason in the window.
    append_activity_log(state, L"Import failed: " + utf8_to_wide(error.what()));
  }
}

LRESULT CALLBACK settings_window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
  switch (message) {
    // STATIC labels and the read-only licence display send this to ask what
    // brush to paint their background with; without a handler DefWindowProc
    // answers with the white COLOR_WINDOW brush, which visibly mismatches the
    // COLOR_BTNFACE-grey the window itself is painted with below.
    case WM_CTLCOLORSTATIC: {
      const HDC dc = reinterpret_cast<HDC>(wparam);
      SetBkMode(dc, TRANSPARENT);
      return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_BTNFACE));
    }
    case WM_NOTIFY: {
      auto *state = reinterpret_cast<SettingsState *>(GetWindowLongPtrW(window, GWLP_USERDATA));
      const auto *header = reinterpret_cast<const NMHDR *>(lparam);
      if (state != nullptr && header->hwndFrom == state->tabs && header->code == TCN_SELCHANGE) {
        show_settings_page(*state,
                           static_cast<int>(SendMessageW(state->tabs, TCM_GETCURSEL, 0, 0)));
      }
      return 0;
    }
    case WM_DESTROY:
      KillTimer(window, kIdleTimerId);
      KillTimer(window, kDirectoryWatchTimerId);
      return 0;
    case WM_TIMER: {
      auto *state = reinterpret_cast<SettingsState *>(GetWindowLongPtrW(window, GWLP_USERDATA));
      if (state == nullptr) {
        return 0;
      }
      if (wparam == kIdleTimerId) {
        on_idle_timer(*state);
      } else if (wparam == kDirectoryWatchTimerId) {
        on_directory_watch_timer(*state, window);
      }
      return 0;
    }
    case WM_COMMAND: {
      auto *state = reinterpret_cast<SettingsState *>(GetWindowLongPtrW(window, GWLP_USERDATA));
      if (state == nullptr) {
        return 0;
      }
      try {
        if (LOWORD(wparam) == kOutputDeviceComboId && HIWORD(wparam) == CBN_SELCHANGE) {
          on_output_device_selected(*state, reinterpret_cast<HWND>(lparam));
        } else if (LOWORD(wparam) == kSpeechSpeedComboId && HIWORD(wparam) == CBN_SELCHANGE) {
          on_speech_speed_selected(*state, reinterpret_cast<HWND>(lparam));
        } else if (LOWORD(wparam) == kPlaybackSkipComboId && HIWORD(wparam) == CBN_SELCHANGE) {
          on_playback_skip_selected(*state, reinterpret_cast<HWND>(lparam));
        } else if (LOWORD(wparam) == kSelectionPolicyComboId && HIWORD(wparam) == CBN_SELCHANGE) {
          on_selection_policy_selected(*state, reinterpret_cast<HWND>(lparam));
        } else if (LOWORD(wparam) == kServerHostEditId && HIWORD(wparam) == EN_KILLFOCUS) {
          on_server_host_changed(*state, reinterpret_cast<HWND>(lparam));
        } else if (LOWORD(wparam) == kServerPortEditId && HIWORD(wparam) == EN_KILLFOCUS) {
          on_server_port_changed(*state, reinterpret_cast<HWND>(lparam));
        } else if (LOWORD(wparam) == kDefaultProfileComboId && HIWORD(wparam) == CBN_SELCHANGE) {
          on_default_profile_selected(*state, reinterpret_cast<HWND>(lparam));
        } else if (LOWORD(wparam) == kLoadModelButtonId && HIWORD(wparam) == BN_CLICKED) {
          on_load_model_clicked(*state, GetDlgItem(window, kModelComboId));
        } else if (LOWORD(wparam) == kUnloadModelButtonId && HIWORD(wparam) == BN_CLICKED) {
          on_unload_model_clicked(*state);
        } else if (LOWORD(wparam) == kImportModelButtonId && HIWORD(wparam) == BN_CLICKED) {
          on_import_model_clicked(*state, window);
        } else if (LOWORD(wparam) == kDownloadModelButtonId && HIWORD(wparam) == BN_CLICKED) {
          on_download_model_clicked(*state, GetDlgItem(window, kCatalogueComboId), window);
        } else if (LOWORD(wparam) == kCheckHotkeyButtonId && HIWORD(wparam) == BN_CLICKED) {
          on_check_hotkey_clicked(*state, window, GetDlgItem(window, kReadSelectionHotkeyEditId));
        } else if (LOWORD(wparam) == kStartAtLoginCheckboxId && HIWORD(wparam) == BN_CLICKED) {
          on_start_at_login_clicked(*state, reinterpret_cast<HWND>(lparam));
        }
      } catch (const std::exception &error) {
        restore_saved_controls(*state, window);
        append_activity_log(*state, L"Settings change failed; previous settings retained: " +
                                   utf8_to_wide(error.what()));
      }
      return 0;
    }
    default:
      return DefWindowProcW(window, message, wparam, lparam);
  }
}

}  // namespace

void run_settings_window(ConfigDocument &document,
                         const std::filesystem::path &runner_directory, ModelSessionManager *sessions) {
  const wchar_t *kClassName = L"TtsHostSettingsWindow";
  const HINSTANCE instance = GetModuleHandleW(nullptr);

  WNDCLASSW window_class{};
  window_class.lpfnWndProc = settings_window_proc;
  window_class.hInstance = instance;
  window_class.lpszClassName = kClassName;
  // Match the COLOR_BTNFACE grey returned for STATIC controls in
  // WM_CTLCOLORSTATIC above; otherwise the window paints white while labels
  // paint grey, producing a visible checkerboard of background colors.
  window_class.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
  // IDC_ARROW expands through the ANSI-generic MAKEINTRESOURCE macro when
  // UNICODE is not globally defined. This window intentionally calls the
  // explicit wide API, so use the wide resource form as well (32512 is the
  // documented resource id behind IDC_ARROW).
  window_class.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
  // Tolerate re-registration: the tray's Settings… item can invoke this
  // function repeatedly within one process (see tray_icon.cpp), and Windows
  // rejects registering the same class name twice.
  if (!RegisterClassW(&window_class) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
    throw std::runtime_error("failed to register the settings window class");
  }

  SettingsState state(document, runner_directory, sessions);

  // A tabbed top-level window: General, Playback, Models, and Server pages
  // above the always-visible activity log
  // (docs/design/architecture.md#desktop-integration).
  const HWND window =
      CreateWindowExW(0, kClassName, L"TTS Host Settings", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT,
                      CW_USEDEFAULT, 512, 640, nullptr, nullptr, instance, nullptr);
  if (!window) {
    throw std::runtime_error("failed to create the settings window");
  }
  struct WindowLifetime {
    HWND handle;
    ~WindowLifetime() { if (IsWindow(handle)) DestroyWindow(handle); }
  } lifetime{window};
  SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&state));

  // Page controls are siblings of the tab control rather than its children, so
  // their WM_COMMAND notifications and GetDlgItem lookups stay on this window.
  // WS_CLIPSIBLINGS plus bottom Z-order keep the tab frame behind them.
  const INITCOMMONCONTROLSEX common_controls{sizeof(INITCOMMONCONTROLSEX), ICC_TAB_CLASSES};
  InitCommonControlsEx(&common_controls);
  state.tabs = CreateWindowExW(
      0, WC_TABCONTROLW, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS, 8, 8, 480, 316, window,
      reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kSettingsTabsId)), instance, nullptr);
  if (state.tabs == nullptr) {
    throw std::runtime_error("failed to create the settings tabs");
  }
  SetWindowPos(state.tabs, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
  const wchar_t *const page_names[] = {L"General", L"Playback", L"Models", L"Server"};
  for (int index = 0; index < static_cast<int>(std::size(page_names)); ++index) {
    TCITEMW item{};
    item.mask = TCIF_TEXT;
    item.pszText = const_cast<wchar_t *>(page_names[index]);
    SendMessageW(state.tabs, TCM_INSERTITEMW, index, reinterpret_cast<LPARAM>(&item));
  }
  auto on_page = [&](int page, HWND control) {
    if (control != nullptr) {
      state.page_controls.push_back({control, page});
    }
    return control;
  };
  auto label = [&](int page, const wchar_t *text, int x, int y, int width, int height) {
    on_page(page, CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE, x, y, width, height,
                                  window, nullptr, instance, nullptr));
  };

  // Layout: a 148 px label column at x=24 (wide enough for the longest caption,
  // "Default English profile:", in the shell UI font) and a control column at
  // x=176, both inside the tab control's page area below its 28 px header.

  // General page.
  label(kGeneralPage, L"Default English profile:", 24, 48, 148, 24);
  const HWND default_profile_combo = on_page(kGeneralPage, CreateWindowExW(
      0, L"COMBOBOX", nullptr, WS_CHILD | WS_VISIBLE | WS_VSCROLL | CBS_DROPDOWNLIST, 176, 44, 300,
      200, window, reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kDefaultProfileComboId)), instance,
      nullptr));
  if (!default_profile_combo) {
    throw std::runtime_error("failed to create the default-profile control");
  }
  populate_default_profile_combo(default_profile_combo, document);
  label(kGeneralPage, L"Restart required to take effect.", 176, 72, 260, 20);

  label(kGeneralPage, L"Selection hotkey:", 24, 104, 148, 24);
  const std::string configured_hotkey =
      state.document.value.value("hotkeys", nlohmann::json::object()).value("readSelection", "");
  const HWND hotkey_edit = on_page(kGeneralPage, CreateWindowExW(
      WS_EX_CLIENTEDGE, L"EDIT", utf8_to_wide(configured_hotkey).c_str(),
      WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 176, 100, 190, 24, window,
      reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kReadSelectionHotkeyEditId)), instance, nullptr));
  if (!hotkey_edit) {
    throw std::runtime_error("failed to create the selection-hotkey controls");
  }
  original_hotkey_edit_proc = reinterpret_cast<WNDPROC>(
      SetWindowLongPtrW(hotkey_edit, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(hotkey_edit_proc)));
  const HWND check_hotkey_button = on_page(kGeneralPage, CreateWindowExW(
      0, L"BUTTON", L"Check and save", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 376, 100, 100, 24, window,
      reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kCheckHotkeyButtonId)), instance, nullptr));
  if (!check_hotkey_button || original_hotkey_edit_proc == nullptr) {
    throw std::runtime_error("failed to create the selection-hotkey controls");
  }

  label(kGeneralPage, L"Capture policy:", 24, 138, 148, 24);
  const HWND capture_policy_combo = on_page(kGeneralPage, CreateWindowExW(
      0, L"COMBOBOX", nullptr, WS_CHILD | WS_VISIBLE | WS_VSCROLL | CBS_DROPDOWNLIST,
      176, 134, 300, 120, window,
      reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kSelectionPolicyComboId)), instance, nullptr));
  if (!capture_policy_combo) {
    throw std::runtime_error("failed to create the selection-capture policy control");
  }
  populate_selection_policy_combo(capture_policy_combo, document);
  label(kGeneralPage,
        L"Clipboard-only reads existing text; copy first. Automatic Copy may change it.", 24, 164,
        452, 20);

  const HWND start_at_login_checkbox = on_page(kGeneralPage, CreateWindowExW(
      0, L"BUTTON", L"Start TTS Host when I sign in", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
      24, 196, 452, 24, window,
      reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kStartAtLoginCheckboxId)), instance, nullptr));
  if (start_at_login_checkbox == nullptr) {
    throw std::runtime_error("failed to create the start-at-login control");
  }
  const bool launch_on_login = state.document.value
                                   .value("startup", nlohmann::json::object())
                                   .value("launchOnLogin", true);
  SendMessageW(start_at_login_checkbox, BM_SETCHECK,
               launch_on_login ? BST_CHECKED : BST_UNCHECKED, 0);

  // Playback page.
  label(kPlaybackPage, L"Output device:", 24, 48, 148, 24);
  const HWND device_combo = on_page(kPlaybackPage, CreateWindowExW(
      0, L"COMBOBOX", nullptr, WS_CHILD | WS_VISIBLE | WS_VSCROLL | CBS_DROPDOWNLIST, 176, 44, 300,
      200, window, reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kOutputDeviceComboId)), instance,
      nullptr));
  if (!device_combo) {
    throw std::runtime_error("failed to create the output-device control");
  }
  populate_output_device_combo(
      device_combo, state.document.value["audio"]["outputDevice"].get<std::string>());

  label(kPlaybackPage, L"Default speech speed:", 24, 84, 148, 24);
  const HWND speech_speed_combo = on_page(kPlaybackPage, CreateWindowExW(
      0, L"COMBOBOX", nullptr, WS_CHILD | WS_VISIBLE | WS_VSCROLL | CBS_DROPDOWNLIST, 176, 80, 120,
      160, window, reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kSpeechSpeedComboId)), instance,
      nullptr));
  if (speech_speed_combo == nullptr) {
    throw std::runtime_error("failed to create the speech-speed control");
  }
  populate_speech_speed_combo(speech_speed_combo, state.document.value["audio"].value("speechSpeed", 1.0));

  label(kPlaybackPage, L"Playback seek interval:", 24, 120, 148, 24);
  const HWND playback_skip_combo = on_page(kPlaybackPage, CreateWindowExW(
      0, L"COMBOBOX", nullptr, WS_CHILD | WS_VISIBLE | WS_VSCROLL | CBS_DROPDOWNLIST, 176, 116,
      150, 240, window,
      reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kPlaybackSkipComboId)), instance, nullptr));
  if (playback_skip_combo == nullptr) {
    throw std::runtime_error("failed to create the playback seek interval control");
  }
  populate_playback_skip_combo(playback_skip_combo,
                               state.document.value["audio"].value("playbackSkipSeconds", 5));

  // Models page.
  label(kModelsPage, L"Model:", 24, 48, 148, 24);
  const HWND model_combo = on_page(kModelsPage, CreateWindowExW(
      0, L"COMBOBOX", nullptr, WS_CHILD | WS_VISIBLE | WS_VSCROLL | CBS_DROPDOWNLIST, 176, 44, 300,
      200, window, reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kModelComboId)), instance, nullptr));
  if (!model_combo) {
    throw std::runtime_error("failed to create the model-selection control");
  }
  state.last_scan = scan_model_registry(document);
  populate_model_combo(model_combo, state, state.last_scan);

  const HWND load_button = on_page(kModelsPage, CreateWindowExW(
      0, L"BUTTON", L"Load", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 176, 76, 90, 26, window,
      reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kLoadModelButtonId)), instance, nullptr));
  const HWND unload_button = on_page(kModelsPage, CreateWindowExW(
      0, L"BUTTON", L"Unload", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 276, 76, 90, 26, window,
      reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kUnloadModelButtonId)), instance, nullptr));
  const HWND import_button = on_page(kModelsPage, CreateWindowExW(
      0, L"BUTTON", L"Import…", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 376, 76, 100, 26, window,
      reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kImportModelButtonId)), instance, nullptr));
  if (!load_button || !unload_button || !import_button) {
    throw std::runtime_error("failed to create the model load/unload/import controls");
  }

  label(kModelsPage, L"Catalogue:", 24, 128, 148, 24);
  const HWND catalogue_combo = on_page(kModelsPage, CreateWindowExW(
      0, L"COMBOBOX", nullptr, WS_CHILD | WS_VISIBLE | WS_VSCROLL | CBS_DROPDOWNLIST, 176, 124, 300,
      200, window, reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kCatalogueComboId)), instance,
      nullptr));
  if (!catalogue_combo) {
    throw std::runtime_error("failed to create the catalogue-selection control");
  }
  populate_catalogue_combo(catalogue_combo, state, state.last_scan);

  const HWND download_button = on_page(kModelsPage, CreateWindowExW(
      0, L"BUTTON", L"Download", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 176, 156, 100, 26, window,
      reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kDownloadModelButtonId)), instance, nullptr));
  if (!download_button) {
    throw std::runtime_error("failed to create the download-model control");
  }

  // Server page.
  label(kServerPage, L"Server host:", 24, 48, 148, 24);
  const HWND host_edit = on_page(kServerPage, CreateWindowExW(
      WS_EX_CLIENTEDGE, L"EDIT",
      utf8_to_wide(state.document.value["server"]["host"].get<std::string>()).c_str(),
      WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 176, 44, 180, 24, window,
      reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kServerHostEditId)), instance, nullptr));
  if (!host_edit) {
    throw std::runtime_error("failed to create the server-host control");
  }

  label(kServerPage, L"Server port:", 24, 80, 148, 24);
  const HWND port_edit = on_page(kServerPage, CreateWindowExW(
      WS_EX_CLIENTEDGE, L"EDIT",
      std::to_wstring(state.document.value["server"]["port"].get<int>()).c_str(),
      WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_NUMBER, 176, 76, 80, 24, window,
      reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kServerPortEditId)), instance, nullptr));
  if (!port_edit) {
    throw std::runtime_error("failed to create the server-port control");
  }
  label(kServerPage, L"Restart required to take effect.", 176, 108, 260, 20);

  show_settings_page(state, kGeneralPage);

  // Activity log, below the tabs and visible on every page.
  CreateWindowExW(0, L"STATIC", L"Activity and installed models:", WS_CHILD | WS_VISIBLE, 16, 336,
                  240, 24, window, nullptr, instance, nullptr);
  state.activity_log = CreateWindowExW(
      WS_EX_CLIENTEDGE, L"EDIT", nullptr,
      WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_AUTOVSCROLL | ES_MULTILINE | ES_READONLY, 16, 360, 472,
      220, window, nullptr, instance, nullptr);
  if (state.activity_log == nullptr) {
    throw std::runtime_error("failed to create the settings activity log");
  }
  append_activity_log(state, L"Installed models and licences:\r\n" + format_installed_models(state.last_scan));
  for (const auto &diagnostic : selection_capture_diagnostics()) {
    append_activity_log(state, utf8_to_wide(diagnostic));
  }
  // Tray Settings shows the existing resident model and does not unload on close.
  report_session_status(state);

  // Every control above is created without a font, so give them all the shell
  // UI font in one pass now that they exist.
  EnumChildWindows(window, apply_shell_ui_font, reinterpret_cast<LPARAM>(shell_ui_font()));

  SetTimer(window, kIdleTimerId, kIdleTimerIntervalMs, nullptr);
  if (state.document.value["modelRegistry"]["watchForChanges"].get<bool>()) {
    SetTimer(window, kDirectoryWatchTimerId, kDirectoryWatchTimerIntervalMs, nullptr);
  }

  ShowWindow(window, SW_SHOWNORMAL);
  UpdateWindow(window);

  MSG message{};
  while (IsWindow(window)) {
    const auto result = GetMessageW(&message, nullptr, 0, 0);
    if (result <= 0) {
      // The tray may have quit inside this nested loop. Tear down Settings
      // before its stack state dies, then return WM_QUIT to the owning loop.
      DestroyWindow(window);
      if (result == 0) PostQuitMessage(static_cast<int>(message.wParam));
      else throw std::runtime_error("failed to read Settings window messages");
      break;
    }
    TranslateMessage(&message);
    DispatchMessageW(&message);
  }
}

}  // namespace tts_host

#else

namespace tts_host {

void run_settings_window(ConfigDocument &, const std::filesystem::path &, ModelSessionManager *) {
  throw std::runtime_error(
      "the settings window is not implemented on this platform yet (Windows only, see "
      "docs/adr/0007-native-ui-per-platform.md)");
}

}  // namespace tts_host

#endif
