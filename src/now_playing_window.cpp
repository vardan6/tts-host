#include "tts_host/now_playing_window.hpp"

#include "tts_host/playback_controller.hpp"

#include <cmath>
#include <cwchar>
#include <iterator>
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
constexpr UINT_PTR kStateTimerId = 1;

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
  auto *controller = reinterpret_cast<PlaybackController *>(GetWindowLongPtrW(window, GWLP_USERDATA));
  if (controller == nullptr) return;
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
    case WM_COMMAND: {
      auto *controller = reinterpret_cast<PlaybackController *>(GetWindowLongPtrW(window, GWLP_USERDATA));
      if (controller == nullptr) return 0;
      if (LOWORD(wparam) == kPauseResumeId && HIWORD(wparam) == BN_CLICKED) {
        if (controller->state() == PlaybackState::Paused) controller->resume(); else controller->pause();
      } else if (LOWORD(wparam) == kStopId && HIWORD(wparam) == BN_CLICKED) controller->stop();
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

void show_now_playing_window(PlaybackController &controller) {
  const wchar_t *name = L"TtsHostNowPlayingWindow";
  const HINSTANCE instance = GetModuleHandleW(nullptr);
  WNDCLASSW wc{};
  wc.lpfnWndProc = proc;
  wc.hInstance = instance;
  wc.lpszClassName = name;
  wc.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
  wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
  if (!RegisterClassW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
    throw std::runtime_error("failed to register the Now Playing window class");
  HWND window = FindWindowW(name, nullptr);
  if (window == nullptr) {
    window = CreateWindowExW(0, name, L"TTS Host — Now Playing", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
                             CW_USEDEFAULT, CW_USEDEFAULT, 330, 245, nullptr, nullptr, instance, nullptr);
    if (window == nullptr) throw std::runtime_error("failed to create the Now Playing window");
    SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&controller));
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
    SetTimer(window, kStateTimerId, 200, nullptr);
  }
  refresh(window);
  ShowWindow(window, SW_SHOWNORMAL);
  SetForegroundWindow(window);
}
}  // namespace tts_host
#else
namespace tts_host {
void show_now_playing_window(PlaybackController &) {
  throw std::runtime_error("Now Playing is not implemented on this platform yet (Windows only)");
}
}  // namespace tts_host
#endif
