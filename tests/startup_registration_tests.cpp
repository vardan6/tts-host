#include "tts_host/startup_registration.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const char *message) {
  if (!condition) throw std::runtime_error(message);
}

constexpr wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";

// Redirect HKCU only in this test process. Production code runs unchanged, but
// its Run key lives beneath a unique temporary test key instead of login settings.
class IsolatedUserRegistry {
 public:
  IsolatedUserRegistry() {
    require(RegOpenCurrentUser(KEY_ALL_ACCESS, &real_user_) == ERROR_SUCCESS,
            "could not open the user hive for an isolated test");
    path_ = L"Software\\TtsHostStartupTest-" + std::to_wstring(GetCurrentProcessId()) +
            L"-" + std::to_wstring(GetTickCount64());
    try {
      require(RegCreateKeyExW(real_user_, path_.c_str(), 0, nullptr, REG_OPTION_NON_VOLATILE,
                              KEY_ALL_ACCESS, nullptr, &isolated_, nullptr) == ERROR_SUCCESS,
              "could not create isolated registry key");
      require(RegOverridePredefKey(HKEY_CURRENT_USER, isolated_) == ERROR_SUCCESS,
              "could not isolate this process's user registry");
      redirected_ = true;
    } catch (...) {
      cleanup();
      throw;
    }
  }

  ~IsolatedUserRegistry() { cleanup(); }
  IsolatedUserRegistry(const IsolatedUserRegistry &) = delete;
  IsolatedUserRegistry &operator=(const IsolatedUserRegistry &) = delete;

  HKEY root() const { return isolated_; }

 private:
  void cleanup() noexcept {
    if (redirected_) RegOverridePredefKey(HKEY_CURRENT_USER, nullptr);
    if (isolated_) RegCloseKey(isolated_);
    if (real_user_) {
      RegDeleteTreeW(real_user_, path_.c_str());
      RegCloseKey(real_user_);
    }
  }

  HKEY real_user_ = nullptr;
  HKEY isolated_ = nullptr;
  std::wstring path_;
  bool redirected_ = false;
};

std::wstring read_command(HKEY root) {
  wchar_t command[32768]{};
  DWORD bytes = sizeof(command);
  const auto status = RegGetValueW(root, kRunKey, L"TTS Host", RRF_RT_REG_SZ,
                                   nullptr, command, &bytes);
  if (status == ERROR_FILE_NOT_FOUND) return {};
  require(status == ERROR_SUCCESS, "could not read startup command");
  return command;
}

std::wstring executable_path() {
  wchar_t path[32768]{};
  const auto length = GetModuleFileNameW(nullptr, path, 32768);
  require(length > 0 && length < 32768, "could not resolve test executable");
  return std::wstring(path, length);
}

}  // namespace

int main() {
  try {
    IsolatedUserRegistry registry;
    const auto config = std::filesystem::path(L"config with spaces \u03b1.json");
    tts_host::set_start_at_login(false, config);
    require(read_command(registry.root()).empty(), "disable created a startup entry");

    tts_host::set_start_at_login(true, config);
    const auto expected = L"\"" + executable_path() + L"\" --config \"" +
                          std::filesystem::absolute(config).wstring() + L"\"";
    require(read_command(registry.root()) == expected,
            "startup command must quote the executable and absolute Unicode config path");

    const auto other_config = std::filesystem::path(L"different config.json");
    tts_host::set_start_at_login(true, other_config);
    require(read_command(registry.root()) ==
                L"\"" + executable_path() + L"\" --config \"" +
                    std::filesystem::absolute(other_config).wstring() + L"\"",
            "reenabling must update the configuration path");

    DWORD unrelated = 42;
    require(RegSetKeyValueW(registry.root(), kRunKey, L"Unrelated test application", REG_DWORD,
                            &unrelated, sizeof(unrelated)) == ERROR_SUCCESS,
            "could not create unrelated test entry");
    tts_host::set_start_at_login(false, other_config);
    tts_host::set_start_at_login(false, other_config);
    require(read_command(registry.root()).empty(), "disable did not remove startup entry");
    DWORD preserved = 0;
    DWORD bytes = sizeof(preserved);
    require(RegGetValueW(registry.root(), kRunKey, L"Unrelated test application", RRF_RT_REG_DWORD,
                          nullptr, &preserved, &bytes) == ERROR_SUCCESS && preserved == unrelated,
            "disable modified another application's entry");
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
  return 0;
}
