#include "tts_host/tray_icon.hpp"

#include <stdexcept>

#ifdef _WIN32
#include <windows.h>

#include <shellapi.h>

#include "tts_host/clipboard.hpp"
#include "tts_host/settings_window.hpp"

#include <array>
#include <string>

namespace tts_host {
namespace {

// What the tray window carries in GWLP_USERDATA for the Settings… item to
// forward; both members outlive the message loop (see run_tray_icon).
struct TrayContext {
  const ConfigDocument *document;
  const std::filesystem::path *runner_directory;
  const SpeakTextFunction *speak_text;
  UINT clipboard_sequence_before_copy = 0;
  unsigned int clipboard_poll_count = 0;
};

// Shell_NotifyIcon delivers mouse events on the icon through this
// application-defined message, carrying the originating mouse message (e.g.
// WM_RBUTTONUP) in the low word of lParam -- the pre-NOTIFYICON_VERSION_4
// callback shape, which is all this minimal Quit-only menu needs.
constexpr UINT kTrayCallbackMessage = WM_APP + 1;
constexpr UINT kTrayIconId = 1;
constexpr UINT kSettingsMenuItemId = 1;
constexpr UINT kQuitMenuItemId = 2;
constexpr UINT kReadClipboardMenuItemId = 3;
constexpr int kReadSelectionHotkeyId = 1;
constexpr UINT_PTR kSelectionCopyTimerId = 1;
constexpr UINT kSelectionCopyPollMilliseconds = 50;
constexpr unsigned int kMaximumSelectionCopyPolls = 10;

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

void speak_clipboard(HWND window) {
  auto *context = tray_context(window);
  try {
    (*context->speak_text)(read_clipboard_text());
  } catch (const std::exception &error) {
    show_error(window, error.what());
  }
}

void request_selection_copy(HWND window) {
  auto *context = tray_context(window);
  context->clipboard_sequence_before_copy = GetClipboardSequenceNumber();
  context->clipboard_poll_count = 0;

  std::array<INPUT, 4> inputs{};
  inputs[0].type = INPUT_KEYBOARD;
  inputs[0].ki.wVk = VK_CONTROL;
  inputs[1].type = INPUT_KEYBOARD;
  inputs[1].ki.wVk = 'C';
  inputs[2].type = INPUT_KEYBOARD;
  inputs[2].ki.wVk = 'C';
  inputs[2].ki.dwFlags = KEYEVENTF_KEYUP;
  inputs[3].type = INPUT_KEYBOARD;
  inputs[3].ki.wVk = VK_CONTROL;
  inputs[3].ki.dwFlags = KEYEVENTF_KEYUP;
  if (SendInput(static_cast<UINT>(inputs.size()), inputs.data(), sizeof(INPUT)) != inputs.size()) {
    show_error(window, "Could not copy the current selection.");
    return;
  }
  SetTimer(window, kSelectionCopyTimerId, kSelectionCopyPollMilliseconds, nullptr);
}

void show_context_menu(HWND window) {
  HMENU menu = CreatePopupMenu();
  if (!menu) {
    return;
  }
  AppendMenuW(menu, MF_STRING, kSettingsMenuItemId, L"Settings…");
  AppendMenuW(menu, MF_STRING, kReadClipboardMenuItemId, L"Read clipboard");
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
    case WM_COMMAND:
      if (LOWORD(wparam) == kQuitMenuItemId) {
        DestroyWindow(window);
      } else if (LOWORD(wparam) == kSettingsMenuItemId) {
        // Blocks the tray's own message loop until the settings window
        // closes -- both windows are modal-by-blocking in this slice, so
        // the tray icon simply stops responding to clicks while settings is
        // open rather than needing a second thread.
        const auto *context = tray_context(window);
        run_settings_window(*context->document, *context->runner_directory);
      } else if (LOWORD(wparam) == kReadClipboardMenuItemId) {
        speak_clipboard(window);
      }
      return 0;
    case WM_HOTKEY:
      if (wparam == kReadSelectionHotkeyId) {
        request_selection_copy(window);
      }
      return 0;
    case WM_TIMER:
      if (wparam == kSelectionCopyTimerId) {
        auto *context = tray_context(window);
        if (GetClipboardSequenceNumber() != context->clipboard_sequence_before_copy) {
          KillTimer(window, kSelectionCopyTimerId);
          speak_clipboard(window);
        } else if (++context->clipboard_poll_count >= kMaximumSelectionCopyPolls) {
          KillTimer(window, kSelectionCopyTimerId);
          show_error(window, "No text was copied from the current selection. Copy the text manually, then use Read clipboard.");
        }
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

void run_tray_icon(const ConfigDocument &document,
                   const std::filesystem::path &runner_directory,
                   const SpeakTextFunction &speak_text) {
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
  const TrayContext context{&document, &runner_directory, &speak_text};
  auto mutable_context = context;
  SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&mutable_context));

  NOTIFYICONDATAW icon_data{};
  icon_data.cbSize = sizeof(icon_data);
  icon_data.hWnd = window;
  icon_data.uID = kTrayIconId;
  icon_data.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
  icon_data.uCallbackMessage = kTrayCallbackMessage;
  icon_data.hIcon = LoadIconW(nullptr, MAKEINTRESOURCEW(32512));  // IDI_APPLICATION, forced wide
  wcscpy_s(icon_data.szTip, L"TTS Host — Ctrl+Alt+R reads selection");

  if (!Shell_NotifyIconW(NIM_ADD, &icon_data)) {
    throw std::runtime_error("failed to add the tray icon (Shell_NotifyIcon NIM_ADD)");
  }

  if (!RegisterHotKey(window, kReadSelectionHotkeyId, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, 'R')) {
    MessageBoxW(window,
                L"Ctrl+Alt+R is already used by another application. You can still use the tray menu's Read clipboard command.",
                L"TTS Host", MB_OK | MB_ICONWARNING);
  }

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

void run_tray_icon(const ConfigDocument & /*document*/,
                   const std::filesystem::path & /*runner_directory*/,
                   const SpeakTextFunction & /*speak_text*/) {
  throw std::runtime_error(
      "the tray icon is not implemented on this platform yet (Windows only, see "
      "docs/adr/0007-native-ui-per-platform.md)");
}

}  // namespace tts_host

#endif
