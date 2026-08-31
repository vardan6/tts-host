#include "tts_host/settings_window.hpp"

#include <stdexcept>

#ifdef _WIN32
#include <windows.h>

#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

#include "tts_host/model_registry.hpp"
#include "tts_host/playback_sink.hpp"

namespace tts_host {
namespace {

constexpr int kOutputDeviceComboId = 101;
constexpr int kServerHostEditId = 102;
constexpr int kServerPortEditId = 103;

// Owns the mutable working copy of the config the window edits; the ConfigDocument
// passed to run_settings_window is const, so control handlers write here instead.
struct SettingsState {
  ConfigDocument document;
};

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

// Writes the settings window's edits back to the config file
// (docs/design/architecture.md#desktop-integration's "Live reload" -- the
// host-side file watcher that applies these live is a separate, not-yet-built
// piece).
void save_config(const SettingsState &state) {
  std::ofstream output(state.document.paths.config_path);
  if (!output) {
    throw std::runtime_error("failed to write " + state.document.paths.config_path.string());
  }
  output << state.document.value.dump(2) << '\n';
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

// Model manifests are validated by scan_model_registry before reaching this
// display. Keep this read-only: selecting, loading, and downloading models
// belong to the later model-manager slice, while licence information should be
// visible as soon as an installed package is discovered.
std::wstring format_installed_models(const ConfigDocument &document) {
  const auto scan = scan_model_registry(document);

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

LRESULT CALLBACK settings_window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
  switch (message) {
    case WM_DESTROY:
      PostQuitMessage(0);
      return 0;
    case WM_COMMAND: {
      auto *state = reinterpret_cast<SettingsState *>(GetWindowLongPtrW(window, GWLP_USERDATA));
      if (state == nullptr) {
        return 0;
      }
      if (LOWORD(wparam) == kOutputDeviceComboId && HIWORD(wparam) == CBN_SELCHANGE) {
        on_output_device_selected(*state, reinterpret_cast<HWND>(lparam));
      } else if (LOWORD(wparam) == kServerHostEditId && HIWORD(wparam) == EN_KILLFOCUS) {
        on_server_host_changed(*state, reinterpret_cast<HWND>(lparam));
      } else if (LOWORD(wparam) == kServerPortEditId && HIWORD(wparam) == EN_KILLFOCUS) {
        on_server_port_changed(*state, reinterpret_cast<HWND>(lparam));
      }
      return 0;
    }
    default:
      return DefWindowProcW(window, message, wparam, lparam);
  }
}

}  // namespace

void run_settings_window(const ConfigDocument &document) {
  const wchar_t *kClassName = L"TtsHostSettingsWindow";
  const HINSTANCE instance = GetModuleHandleW(nullptr);

  WNDCLASSW window_class{};
  window_class.lpfnWndProc = settings_window_proc;
  window_class.hInstance = instance;
  window_class.lpszClassName = kClassName;
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

  SettingsState state{document};

  // A plain visible top-level window with output-device, server host/port, and
  // installed-model licence display controls. Model management and hotkeys
  // arrive in later slices as additional controls added to this window.
  const HWND window =
      CreateWindowExW(0, kClassName, L"TTS Host Settings", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT,
                      CW_USEDEFAULT, 480, 440, nullptr, nullptr, instance, nullptr);
  if (!window) {
    throw std::runtime_error("failed to create the settings window");
  }
  SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&state));

  CreateWindowExW(0, L"STATIC", L"Output device:", WS_CHILD | WS_VISIBLE, 16, 16, 120, 24, window,
                  nullptr, instance, nullptr);
  const HWND device_combo = CreateWindowExW(
      0, L"COMBOBOX", nullptr, WS_CHILD | WS_VISIBLE | WS_VSCROLL | CBS_DROPDOWNLIST, 144, 12, 300,
      200, window, reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kOutputDeviceComboId)), instance,
      nullptr);
  if (!device_combo) {
    throw std::runtime_error("failed to create the output-device control");
  }
  populate_output_device_combo(
      device_combo, state.document.value["audio"]["outputDevice"].get<std::string>());

  CreateWindowExW(0, L"STATIC", L"Server host:", WS_CHILD | WS_VISIBLE, 16, 56, 120, 24, window,
                  nullptr, instance, nullptr);
  const HWND host_edit = CreateWindowExW(
      WS_EX_CLIENTEDGE, L"EDIT",
      utf8_to_wide(state.document.value["server"]["host"].get<std::string>()).c_str(),
      WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 144, 52, 180, 24, window,
      reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kServerHostEditId)), instance, nullptr);
  if (!host_edit) {
    throw std::runtime_error("failed to create the server-host control");
  }

  CreateWindowExW(0, L"STATIC", L"Server port:", WS_CHILD | WS_VISIBLE, 16, 88, 120, 24, window,
                  nullptr, instance, nullptr);
  const HWND port_edit = CreateWindowExW(
      WS_EX_CLIENTEDGE, L"EDIT",
      std::to_wstring(state.document.value["server"]["port"].get<int>()).c_str(),
      WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_NUMBER, 144, 84, 80, 24, window,
      reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kServerPortEditId)), instance, nullptr);
  if (!port_edit) {
    throw std::runtime_error("failed to create the server-port control");
  }

  CreateWindowExW(0, L"STATIC", L"Restart required to take effect.", WS_CHILD | WS_VISIBLE, 144,
                  116, 260, 20, window, nullptr, instance, nullptr);

  CreateWindowExW(0, L"STATIC", L"Installed models and licences:", WS_CHILD | WS_VISIBLE, 16, 156,
                  240, 24, window, nullptr, instance, nullptr);
  const auto model_text = format_installed_models(document);
  const HWND model_details = CreateWindowExW(
      WS_EX_CLIENTEDGE, L"EDIT", model_text.c_str(),
      WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_AUTOVSCROLL | ES_MULTILINE | ES_READONLY, 16, 180, 428,
      190, window, nullptr, instance, nullptr);
  if (!model_details) {
    throw std::runtime_error("failed to create the installed-model licence display");
  }

  ShowWindow(window, SW_SHOWNORMAL);
  UpdateWindow(window);

  MSG message;
  while (GetMessageW(&message, nullptr, 0, 0) > 0) {
    TranslateMessage(&message);
    DispatchMessageW(&message);
  }
}

}  // namespace tts_host

#else

namespace tts_host {

void run_settings_window(const ConfigDocument &) {
  throw std::runtime_error(
      "the settings window is not implemented on this platform yet (Windows only, see "
      "docs/adr/0007-native-ui-per-platform.md)");
}

}  // namespace tts_host

#endif
