#include "tts_host/tray_icon.hpp"
#include "tts_host/settings_window.hpp"
#include "tts_host/model_session.hpp"
#include "tts_host/now_playing_window.hpp"
#include <windows.h>
#include <commctrl.h>
#include <chrono>
#include <iostream>
#include <fstream>
#include <stdexcept>
#include <thread>

namespace {
void require(bool condition, const char *message) {
  if (!condition) throw std::runtime_error(message);
}
template <typename Predicate> void await(Predicate predicate) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(8);
  while (!predicate()) {
    if (std::chrono::steady_clock::now() > deadline) throw std::runtime_error("UI wait timed out");
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
}
BOOL CALLBACK find_settings(HWND window, LPARAM value) {
  wchar_t name[128]{};
  GetClassNameW(window, name, 128);
  DWORD process = 0;
  GetWindowThreadProcessId(window, &process);
  if (process == GetCurrentProcessId() && wcscmp(name, L"TtsHostSettingsWindow") == 0) {
    *reinterpret_cast<HWND *>(value) = window;
    return FALSE;
  }
  return TRUE;
}
HWND settings() {
  HWND found = nullptr;
  EnumWindows(find_settings, reinterpret_cast<LPARAM>(&found));
  return found;
}
HWND tray_window() {
  HWND previous = nullptr;
  while ((previous = FindWindowExW(HWND_MESSAGE, previous, L"TtsHostTrayWindow", nullptr))) {
    DWORD process = 0;
    GetWindowThreadProcessId(previous, &process);
    if (process == GetCurrentProcessId()) return previous;
  }
  return nullptr;
}
BOOL CALLBACK count_settings(HWND window, LPARAM value) {
  wchar_t name[128]{};
  GetClassNameW(window, name, 128);
  DWORD process = 0;
  GetWindowThreadProcessId(window, &process);
  if (process == GetCurrentProcessId() && wcscmp(name, L"TtsHostSettingsWindow") == 0)
    ++*reinterpret_cast<int *>(value);
  return TRUE;
}
}

int main(int argc, char **argv) {
  try {
    require(argc == 3, "expected config and model fixture paths");
    tts_host::CliOptions options;
    options.config_path_override = argv[1];
    auto document = tts_host::load_config(options, argv[0]);
    document.value["hotkeys"]["readSelection"] = "";
    document.value["audio"]["playbackSkipSeconds"] = 5;
    const auto temporary = std::filesystem::temp_directory_path() /
        ("tts-settings-lifetime-" + std::to_string(GetCurrentProcessId()));
    std::filesystem::create_directories(temporary);
    struct Cleanup {
      std::filesystem::path path;
      ~Cleanup() { std::error_code ignored; std::filesystem::remove_all(path, ignored); }
    } cleanup{temporary};
    document.paths.config_path = temporary / "config.json";
    document.value["$schema"] = document.paths.schema_path.generic_string();
    { std::ofstream output(document.paths.config_path); output << document.value.dump(2); }
    const auto original = document.value;
    // Load a real stub model into the tray-owned manager; Settings close/reopen
    // and Quit must not destroy it while the owner remains alive.
    tts_host::CliOptions model_options;
    model_options.config_path_override = argv[2];
    const auto model_config = tts_host::load_config(model_options, argv[0]);
    tts_host::ModelSessionManager sessions(std::filesystem::path(argv[0]).parent_path());
    sessions.load("stub-package", model_config);
    // No startup checkbox commands or registry calls.
    const auto ui_thread = GetCurrentThreadId();
    std::string failure;
    int publications = 0;
    tts_host::PlaybackController controller;
    const auto active = controller.begin();
    controller.mark_playing(active.get());
    int queued_reads = 2;
    int stops = 0;
    HWND now_playing = nullptr;
    std::thread driver([&] {
      try {
        HWND tray = nullptr;
        await([&] { tray = tray_window(); return tray != nullptr; });
        PostMessageW(tray, WM_COMMAND, 5, 0);
        await([&] {
          HWND previous = nullptr;
          while ((previous = FindWindowExW(nullptr, previous, L"TtsHostNowPlayingWindow", nullptr))) {
            DWORD process = 0;
            GetWindowThreadProcessId(previous, &process);
            if (process == GetCurrentProcessId() && IsWindowVisible(previous) &&
                GetDlgItem(previous, 2)) { now_playing = previous; return true; }
          }
          return false;
        });
        // The actual native Stop button must invoke the Host's queue owner,
        // rather than only cancelling the active playback token.
        SendMessageW(now_playing, WM_COMMAND, MAKEWPARAM(2, BN_CLICKED),
                     reinterpret_cast<LPARAM>(GetDlgItem(now_playing, 2)));
        require(stops == 1 && queued_reads == 0 && active->cancelled(),
                "Now Playing Stop did not clear queued reads through the Host callback");
        SendMessageW(now_playing, WM_CLOSE, 0, 0);
        require(!IsWindowVisible(now_playing) && stops == 1,
                "closing Now Playing invoked Stop instead of hiding");
        PostMessageW(tray, WM_COMMAND, 1, 0);
        await([] { return settings() && IsWindowVisible(settings()); });
        const auto first = settings();
        // Tabs show one page at a time: General (default profile, 104) first,
        // then Playback (seek interval, 115); the activity log stays visible.
        const auto tabs = GetDlgItem(first, 117);
        require(tabs != nullptr, "Settings has no tab control");
        require(IsWindowVisible(GetDlgItem(first, 104)) && !IsWindowVisible(GetDlgItem(first, 115)),
                "Settings did not open on the General tab");
        SendMessageW(tabs, TCM_SETCURSEL, 1, 0);
        NMHDR change{tabs, 117, TCN_SELCHANGE};
        SendMessageW(first, WM_NOTIFY, 117, reinterpret_cast<LPARAM>(&change));
        require(IsWindowVisible(GetDlgItem(first, 115)) && !IsWindowVisible(GetDlgItem(first, 104)),
                "Playback tab did not replace the General page");
        // Deny replacement using a real Windows sharing lock. The failed UI
        // save must restore memory/form, leave disk valid, and keep running.
        const auto lock = CreateFileW(document.paths.config_path.c_str(), GENERIC_READ,
            FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        require(lock != INVALID_HANDLE_VALUE, "failed to lock test config");
        const auto combo = GetDlgItem(first, 115);
        SendMessageW(combo, CB_SETCURSEL, 11, 0);
        SendMessageW(first, WM_COMMAND, MAKEWPARAM(115, CBN_SELCHANGE), reinterpret_cast<LPARAM>(combo));
        CloseHandle(lock);
        require(document.value == original, "failed Settings save published unsaved value");
        const auto restored_index = SendMessageW(combo, CB_GETCURSEL, 0, 0);
        if (restored_index != original["audio"].value("playbackSkipSeconds", 5) - 1) {
          throw std::runtime_error("failed Settings save left control changed: index " +
              std::to_string(restored_index) + ", expected " +
              std::to_string(original["audio"].value("playbackSkipSeconds", 5) - 1));
        }
        tts_host::CliOptions saved_options;
        saved_options.config_path_override = document.paths.config_path;
        require(tts_host::load_config(saved_options, argv[0]).value == original,
                "failed Settings save damaged disk config");
        SendMessageW(combo, CB_SETCURSEL, 11, 0);
        SendMessageW(first, WM_COMMAND, MAKEWPARAM(115, CBN_SELCHANGE), reinterpret_cast<LPARAM>(combo));
        require(document.value["audio"]["playbackSkipSeconds"] == 12,
                "Settings could not save after failed replacement");
        // Stream tuning saves its value; Reset restores only the Playback tab.
        const auto lookahead = GetDlgItem(first, 122);
        require(IsWindowVisible(lookahead) && IsWindowVisible(GetDlgItem(first, 123)),
                "Playback tab lacks the stream tuning controls");
        SendMessageW(lookahead, CB_SETCURSEL, 2, 0);
        SendMessageW(first, WM_COMMAND, MAKEWPARAM(122, CBN_SELCHANGE), reinterpret_cast<LPARAM>(lookahead));
        require(document.value["audio"]["lookaheadSentences"] == 3, "lookahead selection was not saved");
        const auto hotkey_before_reset = document.value["hotkeys"];
        SendMessageW(first, WM_COMMAND, MAKEWPARAM(123, BN_CLICKED),
                     reinterpret_cast<LPARAM>(GetDlgItem(first, 123)));
        require(document.value["audio"]["lookaheadSentences"] == 1 &&
                    document.value["audio"]["playbackSkipSeconds"] == 5 &&
                    document.value["audio"]["bufferMilliseconds"] == 50 &&
                    document.value["hotkeys"] == hotkey_before_reset,
                "Reset to defaults did not restore only the Playback tab");
        require(SendMessageW(lookahead, CB_GETCURSEL, 0, 0) == 0 &&
                    SendMessageW(combo, CB_GETCURSEL, 0, 0) == 4,
                "Reset to defaults left Playback controls stale");
        SendMessageW(tray, WM_COMMAND, 1, 0); // Reentrant command must be ignored.
        int count = 0;
        EnumWindows(count_settings, reinterpret_cast<LPARAM>(&count));
        require(count == 1, "reentrant Settings created another window");
        PostMessageW(first, WM_CLOSE, 0, 0);
        await([&] { return !IsWindow(first); });
        // Barrier: processed after the original Settings command unwinds.
        SendMessageW(tray, WM_NULL, 0, 0);
        require(IsWindow(tray), "closing Settings destroyed tray");
        require(sessions.status().loaded, "closing tray Settings unloaded the shared model");
        PostMessageW(tray, WM_COMMAND, 1, 0);
        await([] { return settings() && IsWindowVisible(settings()); });
        PostMessageW(tray, WM_COMMAND, 2, 0); // Quit from inside nested loop.
      } catch (const std::exception &error) {
        failure = error.what();
        PostThreadMessageW(ui_thread, WM_QUIT, 0, 0);
      }
    });
    tts_host::run_tray_icon(document, std::filesystem::path(argv[0]).parent_path(),
        [](const std::string &, tts_host::SpeechQueueMode) {}, controller,
        [&](const tts_host::ConfigDocument &) { ++publications; }, &sessions,
        [&] { queued_reads = 0; ++stops; controller.stop(); });
    driver.join();
    require(failure.empty(), failure.c_str());
    require(sessions.status().loaded, "tray window lifecycle destroyed the owner's resident model");
    require(settings() == nullptr, "Quit left Settings alive with destroyed stack state");
    require(!IsWindow(now_playing) && stops == 1,
            "tray Quit did not destroy Now Playing before its owner exits");
    require(publications == 1, "Settings close/Quit published unexpected number of times");
    // A standalone Settings close must leave no WM_QUIT for a later caller.
    std::thread closer([] {
      await([] { return settings() && IsWindowVisible(settings()); });
      PostMessageW(settings(), WM_CLOSE, 0, 0);
    });
    tts_host::run_settings_window(document, std::filesystem::path(argv[0]).parent_path());
    closer.join();
    MSG message{};
    require(!PeekMessageW(&message, nullptr, WM_QUIT, WM_QUIT, PM_REMOVE),
            "normal Settings close leaked WM_QUIT");
    std::cout << "Settings lifecycle regressions passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
