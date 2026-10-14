#include "tts_host/startup_registration.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>

#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <system_error>

namespace tts_host {
namespace {

constexpr wchar_t kRunKeyPath[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t kRunValueName[] = L"TTS Host";
// Written by Task Manager and Settings -> Apps -> Startup; read, never written.
constexpr wchar_t kStartupApprovedKeyPath[] =
    L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\Run";

class RegistryKey {
 public:
  explicit RegistryKey(HKEY value) : value_(value) {}
  RegistryKey(const RegistryKey &) = delete;
  RegistryKey &operator=(const RegistryKey &) = delete;
  ~RegistryKey() {
    if (value_ != nullptr) {
      RegCloseKey(value_);
    }
  }

  HKEY get() const { return value_; }

 private:
  HKEY value_;
};

template <typename ErrorCode>
[[noreturn]] void throw_win32_error(const char *action, ErrorCode error) {
  throw std::runtime_error(std::string(action) + " (Windows error " +
                           std::to_string(static_cast<unsigned long>(error)) + ")");
}

std::wstring current_executable_path() {
  std::wstring path(260, L'\0');
  while (path.size() < 32768) {
    const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (length == 0) {
      throw_win32_error("could not resolve the TTS Host executable path", GetLastError());
    }
    if (length < path.size()) {
      path.resize(length);
      return path;
    }
    path.resize(path.size() * 2);
  }
  throw std::runtime_error("the TTS Host executable path exceeds the Windows path limit");
}

std::wstring absolute_config_path(const std::filesystem::path &config_path) {
  std::error_code error;
  const auto absolute = std::filesystem::absolute(config_path, error);
  if (error) {
    throw std::runtime_error("could not resolve the config path for start at login: " +
                             error.message());
  }
  return absolute.wstring();
}

std::wstring startup_command(const std::filesystem::path &config_path) {
  return L"\"" + current_executable_path() + L"\" --config \"" + absolute_config_path(config_path) +
         L"\"";
}

// The registered command, or nothing when this user has no entry.
std::optional<std::wstring> read_run_command() {
  DWORD bytes = 0;
  LSTATUS status = RegGetValueW(HKEY_CURRENT_USER, kRunKeyPath, kRunValueName,
                                RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ | RRF_NOEXPAND, nullptr,
                                nullptr, &bytes);
  if (status == ERROR_FILE_NOT_FOUND) {
    return std::nullopt;
  }
  std::wstring command(bytes / sizeof(wchar_t) + 1, L'\0');
  if (status == ERROR_SUCCESS) {
    bytes = static_cast<DWORD>(command.size() * sizeof(wchar_t));
    status = RegGetValueW(HKEY_CURRENT_USER, kRunKeyPath, kRunValueName,
                          RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ | RRF_NOEXPAND, nullptr,
                          command.data(), &bytes);
  }
  if (status == ERROR_FILE_NOT_FOUND) {
    return std::nullopt;
  }
  if (status != ERROR_SUCCESS) {
    throw_win32_error("could not read the TTS Host startup entry", status);
  }
  command.resize(wcsnlen(command.c_str(), command.size()));
  return command;
}

// An odd first byte in the StartupApproved record means the user disabled the
// entry from the system's startup page; a missing record means enabled.
bool disabled_by_system() {
  BYTE record[64]{};
  DWORD bytes = sizeof(record);
  const LSTATUS status = RegGetValueW(HKEY_CURRENT_USER, kStartupApprovedKeyPath, kRunValueName,
                                      RRF_RT_REG_BINARY, nullptr, record, &bytes);
  if (status == ERROR_FILE_NOT_FOUND) {
    return false;
  }
  if (status != ERROR_SUCCESS) {
    throw_win32_error("could not read the system startup approval", status);
  }
  return bytes > 0 && (record[0] & 1) != 0;
}

}  // namespace

StartAtLoginState read_start_at_login_state(const std::filesystem::path &config_path) {
  const auto command = read_run_command();
  if (!command) {
    return StartAtLoginState::off;
  }
  if (disabled_by_system()) {
    return StartAtLoginState::disabledBySystem;
  }
  const auto expected = startup_command(config_path);
  // Windows paths compare case-insensitively.
  const bool same = CompareStringOrdinal(command->c_str(), static_cast<int>(command->size()),
                                         expected.c_str(), static_cast<int>(expected.size()),
                                         TRUE) == CSTR_EQUAL;
  return same ? StartAtLoginState::on : StartAtLoginState::otherCopy;
}

void open_system_startup_settings() {
  const auto result = reinterpret_cast<INT_PTR>(
      ShellExecuteW(nullptr, L"open", L"ms-settings:startupapps", nullptr, nullptr, SW_SHOWNORMAL));
  if (result <= 32) {
    throw std::runtime_error("could not open Windows startup settings (error " +
                             std::to_string(result) + ")");
  }
}

void set_start_at_login(bool enabled, const std::filesystem::path &config_path) {
  if (!enabled) {
    HKEY raw_key = nullptr;
    const LSTATUS open_status = RegOpenKeyExW(HKEY_CURRENT_USER, kRunKeyPath, 0, KEY_SET_VALUE,
                                               &raw_key);
    if (open_status == ERROR_FILE_NOT_FOUND) {
      return;
    }
    if (open_status != ERROR_SUCCESS) {
      throw_win32_error("could not open the current user's startup settings", open_status);
    }
    const RegistryKey key(raw_key);
    const LSTATUS delete_status = RegDeleteValueW(key.get(), kRunValueName);
    if (delete_status != ERROR_SUCCESS && delete_status != ERROR_FILE_NOT_FOUND) {
      throw_win32_error("could not remove the TTS Host startup entry", delete_status);
    }
    return;
  }

  HKEY raw_key = nullptr;
  DWORD disposition = 0;
  const LSTATUS create_status = RegCreateKeyExW(HKEY_CURRENT_USER, kRunKeyPath, 0, nullptr,
                                                 REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, nullptr,
                                                 &raw_key, &disposition);
  if (create_status != ERROR_SUCCESS) {
    throw_win32_error("could not open the current user's startup settings", create_status);
  }
  const RegistryKey key(raw_key);

  const std::wstring command = startup_command(config_path);
  const DWORD byte_count = static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t));
  const LSTATUS set_status = RegSetValueExW(
      key.get(), kRunValueName, 0, REG_SZ, reinterpret_cast<const BYTE *>(command.c_str()),
      byte_count);
  if (set_status != ERROR_SUCCESS) {
    throw_win32_error("could not save the TTS Host startup entry", set_status);
  }
}

}  // namespace tts_host
