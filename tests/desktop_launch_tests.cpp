#include <windows.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

namespace {
void require(bool value, const char *message) {
  if (!value) throw std::runtime_error(message);
}
struct DialogSearch { DWORD pid; HWND window = nullptr; };
BOOL CALLBACK find_dialog(HWND window, LPARAM parameter) {
  auto &search = *reinterpret_cast<DialogSearch *>(parameter);
  DWORD pid = 0;
  GetWindowThreadProcessId(window, &pid);
  wchar_t name[64]{};
  GetClassNameW(window, name, 64);
  if (pid == search.pid && std::wstring(name) == L"#32770") search.window = window;
  return TRUE;
}
}
int wmain(int argc, wchar_t **argv) {
  PROCESS_INFORMATION process{};
  try {
    require(argc == 2, "expected Host path");
    std::ifstream binary(std::filesystem::path(argv[1]), std::ios::binary);
    IMAGE_DOS_HEADER dos{};
    binary.read(reinterpret_cast<char *>(&dos), sizeof(dos));
    binary.seekg(dos.e_lfanew);
    IMAGE_NT_HEADERS64 headers{};
    binary.read(reinterpret_cast<char *>(&headers), sizeof(headers));
    require(binary && headers.Signature == IMAGE_NT_SIGNATURE &&
                headers.OptionalHeader.Subsystem == IMAGE_SUBSYSTEM_WINDOWS_GUI,
            "Host must use GUI subsystem for console-free Explorer/login launch");
    // Config validation fails before startup registration is ever touched.
    auto command = L"\"" + std::wstring(argv[1]) +
        L"\" --config \"" + (std::filesystem::temp_directory_path() /
        (L"tts-missing-" + std::to_wstring(GetCurrentProcessId()) + L".json")).wstring() + L"\"";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    require(CreateProcessW(argv[1], command.data(), nullptr, nullptr, FALSE, 0,
                           nullptr, nullptr, &startup, &process), "could not launch Host");
    DialogSearch search{process.dwProcessId};
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!search.window && std::chrono::steady_clock::now() < deadline) {
      EnumWindows(find_dialog, reinterpret_cast<LPARAM>(&search));
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    require(search.window, "fatal desktop startup error did not show a dialog");
    wchar_t message[2048]{};
    GetDlgItemTextW(search.window, 65535, message, 2048);
    require(std::wstring(message).find(L"tts-missing-") != std::wstring::npos,
            "fatal dialog lost the actionable configuration path");
    PostMessageW(search.window, WM_CLOSE, 0, 0);
    require(WaitForSingleObject(process.hProcess, 5000) == WAIT_OBJECT_0,
            "Host did not exit after fatal dialog was dismissed");
    DWORD code = 0;
    GetExitCodeProcess(process.hProcess, &code);
    require(code != 0, "fatal desktop startup returned success");
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return 0;
  } catch (const std::exception &error) {
    if (process.hProcess) {
      TerminateProcess(process.hProcess, 1);
      WaitForSingleObject(process.hProcess, 5000);
      CloseHandle(process.hThread);
      CloseHandle(process.hProcess);
    }
    std::cerr << error.what() << '\n';
    return 1;
  }
}
