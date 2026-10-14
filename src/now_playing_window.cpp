#include "tts_host/now_playing_window.hpp"

#include "tts_host/playback_controller.hpp"

#include <cmath>
#include <cwchar>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>

#ifdef _WIN32
#include <windows.h>

namespace tts_host {
namespace {
constexpr int kPauseResumeId = 1;
constexpr int kStopId = 2;
constexpr int kSpeedComboId = 4;
constexpr int kSeekBackId = 5;
constexpr int kSeekForwardId = 6;
constexpr int kSourceId = 8;
constexpr int kReadSelectionId = 9;
constexpr int kReadClipboardId = 10;
constexpr int kTargetId = 11;
constexpr int kCaptureStatusId = 12;
constexpr UINT_PTR kStateTimerId = 1;
constexpr wchar_t kWindowClass[] = L"TtsHostNowPlayingWindow";

struct WindowState {
  PlaybackController *controller;
  NowPlayingActions actions;
};

HWND current_window() {
  HWND window = nullptr;
  while ((window = FindWindowExW(nullptr, window, kWindowClass, nullptr))) {
    DWORD process = 0;
    GetWindowThreadProcessId(window, &process);
    if (process == GetCurrentProcessId()) return window;
  }
  return nullptr;
}

std::wstring utf8_to_wide(const std::string &text) {
  if (text.empty()) return {};
  const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                                       static_cast<int>(text.size()), nullptr, 0);
  if (count <= 0) return L"Playback failed";
  std::wstring result(static_cast<std::size_t>(count), L'\0');
  MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()),
                      result.data(), count);
  return result;
}

const wchar_t *state_label(PlaybackState state) {
  switch (state) {
    case PlaybackState::Idle: return L"Idle";
    case PlaybackState::Preparing: return L"Reading…";
    case PlaybackState::Playing: return L"Playing";
    case PlaybackState::Paused: return L"Paused";
    case PlaybackState::Stopping: return L"Stopping…";
    case PlaybackState::Error: return L"Error";
  }
  return L"Unknown";
}

void refresh(HWND window) {
  auto *data = reinterpret_cast<WindowState *>(GetWindowLongPtrW(window, GWLP_USERDATA));
  if (data == nullptr) return;
  auto *controller = data->controller;
  const auto state = controller->state();
  SetWindowTextW(GetDlgItem(window, 3), state_label(state));
  if (state == PlaybackState::Error) {
    const auto message = controller->error_message();
    SetWindowTextW(GetDlgItem(window, 7), utf8_to_wide(message).c_str());
  } else {
    wchar_t position[128];
    std::swprintf(position, std::size(position), L"%.1f s elapsed  /  %.1f s generated",
                  controller->position_seconds(), controller->generated_seconds());
    SetWindowTextW(GetDlgItem(window, 7), position);
  }
  const auto source = controller->source();
  SetWindowTextW(GetDlgItem(window, kSourceId),
                 (L"Source: " + (source.empty() ? std::wstring(L"none") : utf8_to_wide(source))).c_str());
  if (data->actions.target_label)
    SetWindowTextW(GetDlgItem(window, kTargetId), utf8_to_wide(data->actions.target_label()).c_str());
  if (data->actions.capture_status)
    SetWindowTextW(GetDlgItem(window, kCaptureStatusId), utf8_to_wide(data->actions.capture_status()).c_str());
  const auto interval = controller->seek_interval_seconds();
  const auto back = L"-" + std::to_wstring(interval) + L" s";
  const auto forward = L"+" + std::to_wstring(interval) + L" s";
  SetWindowTextW(GetDlgItem(window, kSeekBackId), back.c_str());
  SetWindowTextW(GetDlgItem(window, kSeekForwardId), forward.c_str());
  EnableWindow(GetDlgItem(window, kSeekBackId), controller->can_seek(-1));
  EnableWindow(GetDlgItem(window, kSeekForwardId), controller->can_seek(1));
  SetWindowTextW(GetDlgItem(window, kPauseResumeId),
                 state == PlaybackState::Paused ? L"Resume" : L"Pause");
  EnableWindow(GetDlgItem(window, kPauseResumeId), state == PlaybackState::Preparing ||
                                                     state == PlaybackState::Playing ||
                                                     state == PlaybackState::Paused);
  EnableWindow(GetDlgItem(window, kStopId), state != PlaybackState::Idle && state != PlaybackState::Error);
  const HWND speed_combo = GetDlgItem(window, kSpeedComboId);
  EnableWindow(speed_combo, state != PlaybackState::Idle && state != PlaybackState::Error);
  for (int index = 0; index < static_cast<int>(SendMessageW(speed_combo, CB_GETCOUNT, 0, 0)); ++index) {
    wchar_t value[16];
    SendMessageW(speed_combo, CB_GETLBTEXT, index, reinterpret_cast<LPARAM>(value));
    if (std::abs(std::wcstod(value, nullptr) - controller->speech_speed()) < 0.001) {
      SendMessageW(speed_combo, CB_SETCURSEL, index, 0);
      break;
    }
  }
}

LRESULT CALLBACK proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
  switch (message) {
    case WM_CLOSE: ShowWindow(window, SW_HIDE); return 0;
    case WM_TIMER: refresh(window); return 0;
    case WM_NCDESTROY:
      delete reinterpret_cast<WindowState *>(GetWindowLongPtrW(window, GWLP_USERDATA));
      SetWindowLongPtrW(window, GWLP_USERDATA, 0);
      return DefWindowProcW(window, message, wparam, lparam);
    case WM_COMMAND: {
      auto *data = reinterpret_cast<WindowState *>(GetWindowLongPtrW(window, GWLP_USERDATA));
      if (data == nullptr) return 0;
      auto *controller = data->controller;
      if (LOWORD(wparam) == kPauseResumeId && HIWORD(wparam) == BN_CLICKED) {
        if (controller->state() == PlaybackState::Paused) controller->resume(); else controller->pause();
      } else if (LOWORD(wparam) == kStopId && HIWORD(wparam) == BN_CLICKED) {
        if (data->actions.stop_speech) data->actions.stop_speech(); else controller->stop();
      } else if (LOWORD(wparam) == kReadSelectionId && HIWORD(wparam) == BN_CLICKED) {
        if (data->actions.read_selection) data->actions.read_selection();
      } else if (LOWORD(wparam) == kReadClipboardId && HIWORD(wparam) == BN_CLICKED) {
        if (data->actions.read_clipboard) data->actions.read_clipboard();
      }
      else if (LOWORD(wparam) == kSeekBackId && HIWORD(wparam) == BN_CLICKED)
        controller->seek_by_interval(-1);
      else if (LOWORD(wparam) == kSeekForwardId && HIWORD(wparam) == BN_CLICKED)
        controller->seek_by_interval(1);
      else if (LOWORD(wparam) == kSpeedComboId && HIWORD(wparam) == CBN_SELCHANGE) {
        wchar_t value[16];
        const HWND combo = reinterpret_cast<HWND>(lparam);
        SendMessageW(combo, CB_GETLBTEXT, SendMessageW(combo, CB_GETCURSEL, 0, 0),
                     reinterpret_cast<LPARAM>(value));
        controller->set_active_speech_speed(std::wcstod(value, nullptr));
      }
      refresh(window);
      return 0;
    }
    default: return DefWindowProcW(window, message, wparam, lparam);
  }
}
}  // namespace

void show_now_playing_window(PlaybackController &controller, NowPlayingActions actions) {
  const wchar_t *name = kWindowClass;
  const HINSTANCE instance = GetModuleHandleW(nullptr);
  WNDCLASSW wc{};
  wc.lpfnWndProc = proc;
  wc.hInstance = instance;
  wc.lpszClassName = name;
  wc.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
  wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
  if (!RegisterClassW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
    throw std::runtime_error("failed to register the Now Playing window class");
  HWND window = current_window();
  if (window == nullptr) {
    window = CreateWindowExW(0, name, L"TTS Host — Now Playing", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
                             CW_USEDEFAULT, CW_USEDEFAULT, 330, 380, nullptr, nullptr, instance, nullptr);
    if (window == nullptr) throw std::runtime_error("failed to create the Now Playing window");
    auto data = std::make_unique<WindowState>(WindowState{&controller, std::move(actions)});
    SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(data.release()));
    CreateWindowExW(0, L"STATIC", L"Idle", WS_CHILD | WS_VISIBLE, 16, 16, 250, 24, window,
                    reinterpret_cast<HMENU>(static_cast<UINT_PTR>(3)), instance, nullptr);
    CreateWindowExW(0, L"STATIC", L"0.0 s elapsed  /  0.0 s generated", WS_CHILD | WS_VISIBLE, 16,
                    42, 290, 22, window,
                    reinterpret_cast<HMENU>(static_cast<UINT_PTR>(7)), instance, nullptr);
    CreateWindowExW(0, L"BUTTON", L"-5 s", WS_CHILD | WS_VISIBLE, 16, 72, 90, 28, window,
                    reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kSeekBackId)), instance, nullptr);
    CreateWindowExW(0, L"BUTTON", L"+5 s", WS_CHILD | WS_VISIBLE, 116, 72, 90, 28, window,
                    reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kSeekForwardId)), instance, nullptr);
    CreateWindowExW(0, L"BUTTON", L"Pause", WS_CHILD | WS_VISIBLE, 16, 112, 110, 28, window,
                    reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kPauseResumeId)), instance, nullptr);
    CreateWindowExW(0, L"BUTTON", L"Stop", WS_CHILD | WS_VISIBLE, 140, 112, 110, 28, window,
                    reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kStopId)), instance, nullptr);
    CreateWindowExW(0, L"STATIC", L"Speed:", WS_CHILD | WS_VISIBLE, 16, 158, 60, 24, window,
                    nullptr, instance, nullptr);
    const HWND speed_combo = CreateWindowExW(
        0, L"COMBOBOX", nullptr, WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST, 80, 154, 100, 160, window,
        reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kSpeedComboId)), instance, nullptr);
    for (const wchar_t *speed : {L"0.5×", L"0.75×", L"1.0×", L"1.25×", L"1.5×", L"1.75×", L"2.0×"})
      SendMessageW(speed_combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(speed));
    CreateWindowExW(0, L"STATIC", L"Source: none", WS_CHILD | WS_VISIBLE | SS_ENDELLIPSIS, 16, 196,
                    290, 22, window, reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kSourceId)),
                    instance, nullptr);
    CreateWindowExW(0, L"BUTTON", L"Read selection", WS_CHILD | WS_VISIBLE, 16, 224, 130, 28, window,
                    reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kReadSelectionId)), instance, nullptr);
    CreateWindowExW(0, L"BUTTON", L"Read clipboard", WS_CHILD | WS_VISIBLE, 156, 224, 130, 28, window,
                    reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kReadClipboardId)), instance, nullptr);
    CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_ENDELLIPSIS, 16, 260, 290, 22,
                    window, reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kTargetId)), instance, nullptr);
    CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE, 16, 284, 290, 52, window,
                    reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kCaptureStatusId)), instance, nullptr);
    SetTimer(window, kStateTimerId, 200, nullptr);
  }
  refresh(window);
  ShowWindow(window, SW_SHOWNORMAL);
  SetForegroundWindow(window);
}
void destroy_now_playing_window() {
  if (const HWND window = current_window()) DestroyWindow(window);
}
}  // namespace tts_host
#else
namespace tts_host {
void show_now_playing_window(PlaybackController &, NowPlayingActions) {
  throw std::runtime_error("Now Playing is not implemented on this platform yet (Windows only)");
}
void destroy_now_playing_window() {}
}  // namespace tts_host
#endif
