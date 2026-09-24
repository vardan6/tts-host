#include "tts_host/tray_icon.hpp"

#include <stdexcept>

#ifdef _WIN32
#include <windows.h>

#include <shellapi.h>

#include "tts_host/clipboard.hpp"
#include "tts_host/global_hotkey.hpp"
#include "tts_host/now_playing_window.hpp"
#include "tts_host/selection_capture.hpp"
#include "tts_host/settings_window.hpp"

#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <thread>

namespace tts_host {
namespace {

// What the tray window carries in GWLP_USERDATA for the Settings… item to
// forward; both members outlive the message loop (see run_tray_icon).
struct TrayContext {
  ConfigDocument *document;
  const std::filesystem::path *runner_directory;
  const SpeakTextFunction *speak_text;
  PlaybackController *playback_controller;
  std::uint64_t capture_generation = 0;
  bool capture_timed_out = false;
  bool selection_hotkey_registered = false;
  bool selection_hotkey_disabled_by_user = false;
};

struct CaptureCompletion {
  std::uint64_t generation;
  SelectionCapturePolicy policy;
  SelectionCaptureResult result;
};

// Shell_NotifyIcon delivers mouse events on the icon through this
// application-defined message, carrying the originating mouse message (e.g.
// WM_RBUTTONUP) in the low word of lParam -- the pre-NOTIFYICON_VERSION_4
// callback shape, which is all this minimal Quit-only menu needs.
constexpr UINT kTrayCallbackMessage = WM_APP + 1;
constexpr UINT kSelectionCaptureCompleteMessage = WM_APP + 2;
constexpr UINT kTrayIconId = 1;
constexpr UINT kSettingsMenuItemId = 1;
constexpr UINT kQuitMenuItemId = 2;
constexpr UINT kReadClipboardMenuItemId = 3;
constexpr UINT kQueueClipboardMenuItemId = 4;
constexpr UINT kNowPlayingMenuItemId = 5;
constexpr UINT kSelectionHotkeyToggleMenuItemId = 6;
constexpr int kReadSelectionHotkeyId = 1;
constexpr UINT_PTR kSelectionCaptureTimerId = 1;
constexpr UINT kSelectionCaptureDeadlineMilliseconds = 1500;
std::atomic_bool selection_capture_worker_active = false;

std::optional<GlobalHotkey> configured_read_selection_hotkey(const ConfigDocument &document,
                                                              std::string &error) {
  const auto hotkeys = document.value.find("hotkeys");
  if (hotkeys == document.value.end()) {
    return std::nullopt;
  }
  if (!hotkeys->is_object()) {
    error = "hotkeys must be an object";
    return std::nullopt;
  }
  const auto read_selection = hotkeys->find("readSelection");
  if (read_selection == hotkeys->end() || read_selection->is_null()) {
    return std::nullopt;
  }
  if (!read_selection->is_string()) {
    error = "hotkeys.readSelection must be a string";
    return std::nullopt;
  }
  if (read_selection->empty()) {
    return std::nullopt;
  }
  return parse_global_hotkey(read_selection->get<std::string>(), error);
}

bool register_read_selection_hotkey(HWND window, const ConfigDocument &document) {
  std::string error;
  const auto hotkey = configured_read_selection_hotkey(document, error);
  if (!hotkey.has_value()) {
    if (!error.empty()) {
      MessageBoxW(window, L"The configured selection hotkey is invalid. Open Settings and choose another one.",
                  L"TTS Host", MB_OK | MB_ICONWARNING);
    }
    return false;
  }
  if (!RegisterHotKey(window, kReadSelectionHotkeyId, hotkey->modifiers | MOD_NOREPEAT,
                      hotkey->virtual_key)) {
    MessageBoxW(window,
                L"The configured selection hotkey is unavailable. Open Settings and choose another one.",
                L"TTS Host", MB_OK | MB_ICONWARNING);
    return false;
  }
  return true;
}

bool has_valid_read_selection_hotkey(const ConfigDocument &document) {
  std::string error;
  return configured_read_selection_hotkey(document, error).has_value();
}

TrayContext *tray_context(HWND window) {
  return reinterpret_cast<TrayContext *>(GetWindowLongPtrW(window, GWLP_USERDATA));
}

void show_error(HWND window, const std::string &message) {
  const int required = MultiByteToWideChar(CP_UTF8, 0, message.c_str(), -1, nullptr, 0);
  std::wstring wide_message(required > 0 ? static_cast<std::size_t>(required) : 0, L'\0');
  if (required > 0) {
    MultiByteToWideChar(CP_UTF8, 0, message.c_str(), -1, wide_message.data(), required);
    wide_message.pop_back();
  }
  MessageBoxW(window, wide_message.c_str(), L"TTS Host", MB_OK | MB_ICONERROR);
}

void speak_clipboard(HWND window, SpeechQueueMode queue_mode = SpeechQueueMode::Interrupt) {
  auto *context = tray_context(window);
  try {
    (*context->speak_text)(read_clipboard_text(), queue_mode);
  } catch (const std::exception &error) {
    show_error(window, error.what());
  }
}

SelectionCapturePolicy configured_selection_policy(const ConfigDocument &document) {
  const auto selection = document.value.value("selection", nlohmann::json::object());
  return parse_selection_capture_policy(selection.value("capturePolicy", "automatic"));
}

void log_capture_diagnostic(const SelectionCaptureResult &result) {
  record_selection_capture_diagnostic(result);
  const std::string message = "TTS Host selection capture: method=" + result.method +
                              ", target=" + result.target + ", result=" +
                              (result.succeeded ? "success" : result.failure) + "\n";
  OutputDebugStringA(message.c_str());
}

void request_selection_capture(HWND window) {
  auto *context = tray_context(window);
  const auto policy = configured_selection_policy(*context->document);
  if (policy == SelectionCapturePolicy::ClipboardOnly) {
    // This policy deliberately reads the existing clipboard without trying to
    // synthesize Copy. The user must copy the intended selection first.
    speak_clipboard(window);
    return;
  }
  bool expected = false;
  if (!selection_capture_worker_active.compare_exchange_strong(expected, true)) {
    show_error(window, "Selection capture is still waiting on the target application. Try again shortly.");
    return;
  }

  const SelectionCaptureTarget target = snapshot_selection_target();
  const std::uint64_t generation = ++context->capture_generation;
  context->capture_timed_out = false;
  SetTimer(window, kSelectionCaptureTimerId, kSelectionCaptureDeadlineMilliseconds, nullptr);
  std::thread([window, generation, policy, target] {
    auto *completion = new CaptureCompletion{generation, policy, capture_selection_direct(target)};
    selection_capture_worker_active.store(false);
    if (!PostMessageW(window, kSelectionCaptureCompleteMessage, 0,
                      reinterpret_cast<LPARAM>(completion))) {
      delete completion;
    }
  }).detach();
}

void show_context_menu(HWND window) {
  const auto *context = tray_context(window);
  HMENU menu = CreatePopupMenu();
  if (!menu) {
    return;
  }
  UINT hotkey_menu_flags = MF_STRING;
  if (context->selection_hotkey_registered) {
    hotkey_menu_flags |= MF_CHECKED;
  }
  if (!has_valid_read_selection_hotkey(*context->document)) {
    hotkey_menu_flags |= MF_GRAYED;
  }
  AppendMenuW(menu, hotkey_menu_flags, kSelectionHotkeyToggleMenuItemId,
              L"Global selection shortcut");
  AppendMenuW(menu, MF_STRING, kSettingsMenuItemId, L"Settings…");
  AppendMenuW(menu, MF_STRING, kReadClipboardMenuItemId, L"Read clipboard");
  AppendMenuW(menu, MF_STRING, kQueueClipboardMenuItemId, L"Queue clipboard");
  AppendMenuW(menu, MF_STRING, kNowPlayingMenuItemId, L"Now Playing…");
  AppendMenuW(menu, MF_STRING, kQuitMenuItemId, L"Quit");

  POINT cursor{};
  GetCursorPos(&cursor);
  // Required for the popup menu to close correctly when the user clicks
  // away from it (a documented Win32 quirk: the window that owns the menu
  // must be the foreground window, and a WM_NULL must follow TrackPopupMenu).
  SetForegroundWindow(window);
  TrackPopupMenu(menu, TPM_RIGHTBUTTON, cursor.x, cursor.y, 0, window, nullptr);
  PostMessageW(window, WM_NULL, 0, 0);
  DestroyMenu(menu);
}

LRESULT CALLBACK tray_window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
  switch (message) {
    case kTrayCallbackMessage:
      if (LOWORD(lparam) == WM_RBUTTONUP || LOWORD(lparam) == WM_CONTEXTMENU) {
        show_context_menu(window);
      }
      return 0;
    case kSelectionCaptureCompleteMessage: {
      std::unique_ptr<CaptureCompletion> completion(
          reinterpret_cast<CaptureCompletion *>(lparam));
      auto *context = tray_context(window);
      if (completion->generation != context->capture_generation || context->capture_timed_out) {
        return 0;
      }
      KillTimer(window, kSelectionCaptureTimerId);
      log_capture_diagnostic(completion->result);
      if (completion->result.succeeded) {
        try {
          (*context->speak_text)(completion->result.text, SpeechQueueMode::Interrupt);
        } catch (const std::exception &error) {
          show_error(window, error.what());
        }
      } else {
        std::string explanation = completion->result.method + " failed for " +
                                  completion->result.target + ": " + completion->result.failure + ". ";
        if (completion->policy == SelectionCapturePolicy::Automatic) {
          explanation += "Safe Copy fallback is unavailable for this control. ";
        }
        explanation += "Copy manually, then use Read clipboard.";
        show_error(window, explanation);
      }
      return 0;
    }
    case WM_COMMAND:
      if (LOWORD(wparam) == kQuitMenuItemId) {
        DestroyWindow(window);
      } else if (LOWORD(wparam) == kSettingsMenuItemId) {
        // Blocks the tray's own message loop until the settings window
        // closes -- both windows are modal-by-blocking in this slice, so
        // the tray icon simply stops responding to clicks while settings is
        // open rather than needing a second thread.
        auto *context = tray_context(window);
        const bool reenable_selection_hotkey = !context->selection_hotkey_disabled_by_user;
        // Release our own hotkey while Settings probes candidates. Otherwise
        // testing the active value would always report a conflict with TTS Host.
        UnregisterHotKey(window, kReadSelectionHotkeyId);
        context->selection_hotkey_registered = false;
        run_settings_window(*context->document, *context->runner_directory);
        // Settings edits the shared document. Apply its persisted default to
        // the next utterance without changing the active utterance's requested
        // speed; the latter belongs to the Now Playing control.
        context->playback_controller->set_default_speech_speed(
            context->document->value.at("audio").value("speechSpeed", 1.0));
        if (reenable_selection_hotkey) {
          context->selection_hotkey_registered =
              register_read_selection_hotkey(window, *context->document);
        }
      } else if (LOWORD(wparam) == kSelectionHotkeyToggleMenuItemId) {
        auto *context = tray_context(window);
        if (context->selection_hotkey_registered) {
          UnregisterHotKey(window, kReadSelectionHotkeyId);
          context->selection_hotkey_registered = false;
          context->selection_hotkey_disabled_by_user = true;
        } else {
          context->selection_hotkey_disabled_by_user = false;
          context->selection_hotkey_registered =
              register_read_selection_hotkey(window, *context->document);
        }
      } else if (LOWORD(wparam) == kReadClipboardMenuItemId) {
        speak_clipboard(window);
      } else if (LOWORD(wparam) == kQueueClipboardMenuItemId) {
        speak_clipboard(window, SpeechQueueMode::Enqueue);
      } else if (LOWORD(wparam) == kNowPlayingMenuItemId) {
        show_now_playing_window(*tray_context(window)->playback_controller);
      }
      return 0;
    case WM_HOTKEY:
      if (wparam == kReadSelectionHotkeyId && tray_context(window)->selection_hotkey_registered) {
        request_selection_capture(window);
      }
      return 0;
    case WM_TIMER:
      if (wparam == kSelectionCaptureTimerId) {
        auto *context = tray_context(window);
        KillTimer(window, kSelectionCaptureTimerId);
        context->capture_timed_out = true;
        log_capture_diagnostic({false, {}, "UI Automation TextPattern", "saved foreground target",
                                "capture timed out; late result discarded"});
        show_error(window,
                   "UI Automation selection capture timed out. The late result will be ignored; "
                   "copy manually, then use Read clipboard.");
      }
      return 0;
    case WM_DESTROY: {
      UnregisterHotKey(window, kReadSelectionHotkeyId);
      PostQuitMessage(0);
      return 0;
    }
    default:
      return DefWindowProcW(window, message, wparam, lparam);
  }
}

}  // namespace

void run_tray_icon(ConfigDocument &document,
                   const std::filesystem::path &runner_directory,
                   const SpeakTextFunction &speak_text, PlaybackController &playback_controller) {
  const wchar_t *kClassName = L"TtsHostTrayWindow";
  const HINSTANCE instance = GetModuleHandleW(nullptr);

  WNDCLASSW window_class{};
  window_class.lpfnWndProc = tray_window_proc;
  window_class.hInstance = instance;
  window_class.lpszClassName = kClassName;
  if (!RegisterClassW(&window_class)) {
    throw std::runtime_error("failed to register the tray window class");
  }

  // HWND_MESSAGE: a message-only window. It never needs to be visible --
  // Shell_NotifyIcon only needs a valid HWND to deliver callbacks to.
  const HWND window = CreateWindowExW(0, kClassName, L"TTS Host", 0, 0, 0, 0, 0, HWND_MESSAGE,
                                      nullptr, instance, nullptr);
  if (!window) {
    throw std::runtime_error("failed to create the tray message window");
  }
  const TrayContext context{&document, &runner_directory, &speak_text, &playback_controller};
  auto mutable_context = context;
  SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&mutable_context));

  NOTIFYICONDATAW icon_data{};
  icon_data.cbSize = sizeof(icon_data);
  icon_data.hWnd = window;
  icon_data.uID = kTrayIconId;
  icon_data.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
  icon_data.uCallbackMessage = kTrayCallbackMessage;
  icon_data.hIcon = LoadIconW(nullptr, MAKEINTRESOURCEW(32512));  // IDI_APPLICATION, forced wide
  wcscpy_s(icon_data.szTip, L"TTS Host — global selection shortcut in menu");

  if (!Shell_NotifyIconW(NIM_ADD, &icon_data)) {
    throw std::runtime_error("failed to add the tray icon (Shell_NotifyIcon NIM_ADD)");
  }

  mutable_context.selection_hotkey_registered = register_read_selection_hotkey(window, document);

  MSG message;
  while (GetMessageW(&message, nullptr, 0, 0) > 0) {
    TranslateMessage(&message);
    DispatchMessageW(&message);
  }

  Shell_NotifyIconW(NIM_DELETE, &icon_data);
}

}  // namespace tts_host

#else

namespace tts_host {

void run_tray_icon(ConfigDocument & /*document*/,
                   const std::filesystem::path & /*runner_directory*/,
                   const SpeakTextFunction & /*speak_text*/, PlaybackController & /*playback_controller*/) {
  throw std::runtime_error(
      "the tray icon is not implemented on this platform yet (Windows only, see "
      "docs/adr/0007-native-ui-per-platform.md)");
}

}  // namespace tts_host

#endif
