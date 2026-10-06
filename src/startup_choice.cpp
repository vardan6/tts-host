#include "tts_host/startup_choice.hpp"

#include "tts_host/path_encoding.hpp"

#include <fstream>
#include <stdexcept>
#include <string>
#include <system_error>

#ifdef _WIN32
#include <windows.h>
#endif

namespace tts_host {

bool ask_startup_choice_once(const std::filesystem::path &data_dir, bool default_answer,
                             const std::function<bool(bool)> &ask,
                             const std::function<void()> &register_startup) {
  const auto marker = data_dir / kStartupChoiceMarkerName;
  if (std::filesystem::exists(marker)) {
    return false;
  }
  const bool answer = ask(default_answer);

  std::error_code error;
  std::filesystem::create_directories(data_dir, error);
  std::ofstream output(marker);
  output << (answer ? "launchOnLogin\n" : "declined\n");
  if (!output) {
    throw std::runtime_error("could not record the login startup choice in " +
                             path_to_utf8(data_dir));
  }
  output.close();

  if (answer) {
    register_startup();
  }
  return true;
}

#ifdef _WIN32

namespace {

constexpr int kStartupChoiceCheckboxId = 101;

struct StartupChoiceState {
  HWND checkbox = nullptr;
  bool answer = false;
};

LRESULT CALLBACK startup_choice_window_proc(HWND window, UINT message, WPARAM wparam,
                                            LPARAM lparam) {
  auto *state = reinterpret_cast<StartupChoiceState *>(GetWindowLongPtrW(window, GWLP_USERDATA));
  switch (message) {
    case WM_CTLCOLORSTATIC: {
      SetBkMode(reinterpret_cast<HDC>(wparam), TRANSPARENT);
      return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_BTNFACE));
    }
    case WM_COMMAND:
      if (LOWORD(wparam) == IDOK && state != nullptr) {
        state->answer = SendMessageW(state->checkbox, BM_GETCHECK, 0, 0) == BST_CHECKED;
        DestroyWindow(window);
      }
      return 0;
    case WM_CLOSE:
      // Closing without OK is not consent.
      DestroyWindow(window);
      return 0;
    default:
      return DefWindowProcW(window, message, wparam, lparam);
  }
}

BOOL CALLBACK apply_font(HWND child, LPARAM font) {
  SendMessageW(child, WM_SETFONT, static_cast<WPARAM>(font), MAKELPARAM(TRUE, 0));
  return TRUE;
}

}  // namespace

bool show_startup_choice_dialog(bool default_answer) {
  const wchar_t *kClassName = L"TtsHostStartupChoiceWindow";
  const HINSTANCE instance = GetModuleHandleW(nullptr);
  WNDCLASSW window_class{};
  window_class.lpfnWndProc = startup_choice_window_proc;
  window_class.hInstance = instance;
  window_class.lpszClassName = kClassName;
  window_class.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
  window_class.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
  if (!RegisterClassW(&window_class) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
    throw std::runtime_error("failed to register the startup question window class");
  }

  StartupChoiceState state;
  const int width = 440;
  const int height = 200;
  const HWND window = CreateWindowExW(
      WS_EX_DLGMODALFRAME | WS_EX_TOPMOST, kClassName, L"TTS Host", WS_CAPTION | WS_SYSMENU,
      (GetSystemMetrics(SM_CXSCREEN) - width) / 2, (GetSystemMetrics(SM_CYSCREEN) - height) / 2,
      width, height, nullptr, nullptr, instance, nullptr);
  if (window == nullptr) {
    throw std::runtime_error("failed to create the startup question window");
  }
  SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&state));

  CreateWindowExW(0, L"STATIC",
                  L"TTS Host runs in the notification area and reads text aloud. You can change "
                  L"this later in Settings or in Windows startup settings.",
                  WS_CHILD | WS_VISIBLE, 16, 16, 392, 48, window, nullptr, instance, nullptr);
  state.checkbox = CreateWindowExW(
      0, L"BUTTON", L"Start TTS Host when I sign in",
      WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX, 16, 72, 392, 24, window,
      reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kStartupChoiceCheckboxId)), instance, nullptr);
  const HWND ok_button = CreateWindowExW(
      0, L"BUTTON", L"OK", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, 318, 112, 90, 28,
      window, reinterpret_cast<HMENU>(static_cast<UINT_PTR>(IDOK)), instance, nullptr);
  if (state.checkbox == nullptr || ok_button == nullptr) {
    DestroyWindow(window);
    throw std::runtime_error("failed to create the startup question controls");
  }
  SendMessageW(state.checkbox, BM_SETCHECK, default_answer ? BST_CHECKED : BST_UNCHECKED, 0);

  NONCLIENTMETRICSW metrics{};
  metrics.cbSize = sizeof(metrics);
  HFONT font = nullptr;
  if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0) != 0) {
    font = CreateFontIndirectW(&metrics.lfMessageFont);
  }
  EnumChildWindows(window, apply_font,
                   reinterpret_cast<LPARAM>(font ? font : GetStockObject(DEFAULT_GUI_FONT)));

  ShowWindow(window, SW_SHOWNORMAL);
  SetForegroundWindow(window);
  SetFocus(ok_button);

  MSG message{};
  while (IsWindow(window)) {
    const auto result = GetMessageW(&message, nullptr, 0, 0);
    if (result <= 0) {
      DestroyWindow(window);
      if (result == 0) PostQuitMessage(static_cast<int>(message.wParam));
      break;
    }
    if (!IsDialogMessageW(window, &message)) {
      TranslateMessage(&message);
      DispatchMessageW(&message);
    }
  }
  if (font != nullptr) {
    DeleteObject(font);
  }
  return state.answer;
}

#else

bool show_startup_choice_dialog(bool) {
  throw std::runtime_error(
      "the startup question is not implemented on this platform yet (Windows only, see "
      "docs/design/architecture.md#login-startup)");
}

#endif

}  // namespace tts_host
