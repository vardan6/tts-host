#include "tts_host/tray_icon.hpp"

#include <stdexcept>

#ifdef _WIN32
#include <windows.h>

#include <shellapi.h>

#include "tts_host/settings_window.hpp"

namespace tts_host {
namespace {

// Shell_NotifyIcon delivers mouse events on the icon through this
// application-defined message, carrying the originating mouse message (e.g.
// WM_RBUTTONUP) in the low word of lParam -- the pre-NOTIFYICON_VERSION_4
// callback shape, which is all this minimal Quit-only menu needs.
constexpr UINT kTrayCallbackMessage = WM_APP + 1;
constexpr UINT kTrayIconId = 1;
constexpr UINT kSettingsMenuItemId = 1;
constexpr UINT kQuitMenuItemId = 2;

void show_context_menu(HWND window) {
  HMENU menu = CreatePopupMenu();
  if (!menu) {
    return;
  }
  AppendMenuW(menu, MF_STRING, kSettingsMenuItemId, L"Settings…");
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
        const auto *document = reinterpret_cast<const ConfigDocument *>(
            GetWindowLongPtrW(window, GWLP_USERDATA));
        run_settings_window(*document);
      }
      return 0;
    case WM_DESTROY:
      PostQuitMessage(0);
      return 0;
    default:
      return DefWindowProcW(window, message, wparam, lparam);
  }
}

}  // namespace

void run_tray_icon(const ConfigDocument &document) {
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
  SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&document));

  NOTIFYICONDATAW icon_data{};
  icon_data.cbSize = sizeof(icon_data);
  icon_data.hWnd = window;
  icon_data.uID = kTrayIconId;
  icon_data.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
  icon_data.uCallbackMessage = kTrayCallbackMessage;
  icon_data.hIcon = LoadIconW(nullptr, MAKEINTRESOURCEW(32512));  // IDI_APPLICATION, forced wide
  wcscpy_s(icon_data.szTip, L"TTS Host");

  if (!Shell_NotifyIconW(NIM_ADD, &icon_data)) {
    throw std::runtime_error("failed to add the tray icon (Shell_NotifyIcon NIM_ADD)");
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

void run_tray_icon(const ConfigDocument & /*document*/) {
  throw std::runtime_error(
      "the tray icon is not implemented on this platform yet (Windows only, see "
      "docs/adr/0007-native-ui-per-platform.md)");
}

}  // namespace tts_host

#endif
