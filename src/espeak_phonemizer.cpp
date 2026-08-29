#include "tts_host/espeak_phonemizer.hpp"

#include "tts_host/runner_protocol.hpp"

#include <array>
#include <stdexcept>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <cerrno>
#include <cstring>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace tts_host {
namespace {

std::string trim_one_trailing_line_ending(std::string output) {
  if (!output.empty() && output.back() == '\n') {
    output.pop_back();
    if (!output.empty() && output.back() == '\r') {
      output.pop_back();
    }
  }
  return output;
}

#ifdef _WIN32
std::wstring quote_windows_argument(const std::wstring &argument) {
  std::wstring quoted = L"\"";
  std::size_t slash_count = 0;
  for (const auto character : argument) {
    if (character == L'\\') {
      ++slash_count;
      continue;
    }
    if (character == L'\"') {
      quoted.append(slash_count * 2 + 1, L'\\');
      quoted.push_back(character);
      slash_count = 0;
      continue;
    }
    quoted.append(slash_count, L'\\');
    slash_count = 0;
    quoted.push_back(character);
  }
  quoted.append(slash_count * 2, L'\\');
  quoted.push_back(L'\"');
  return quoted;
}

std::wstring utf8_to_wstring(std::string_view text) {
  if (text.empty()) {
    return {};
  }
  const auto required = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                                            static_cast<int>(text.size()), nullptr, 0);
  if (required == 0) {
    throw RunnerProtocolError("espeak-ng argument is not valid UTF-8");
  }
  std::wstring result(static_cast<std::size_t>(required), L'\0');
  if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()),
                          result.data(), required) == 0) {
    throw RunnerProtocolError("could not encode espeak-ng argument for Windows");
  }
  return result;
}

std::string run_espeak(const std::filesystem::path &executable,
                       const std::vector<std::string> &arguments) {
  SECURITY_ATTRIBUTES attributes{};
  attributes.nLength = sizeof(attributes);
  attributes.bInheritHandle = TRUE;

  HANDLE output_read = nullptr;
  HANDLE output_write = nullptr;
  if (!CreatePipe(&output_read, &output_write, &attributes, 0) ||
      !SetHandleInformation(output_read, HANDLE_FLAG_INHERIT, 0)) {
    throw RunnerProtocolError("could not create espeak-ng output pipe");
  }

  std::wstring command_line;
  for (const auto &argument : arguments) {
    if (!command_line.empty()) {
      command_line.push_back(L' ');
    }
    command_line += quote_windows_argument(utf8_to_wstring(argument));
  }
  std::vector<wchar_t> mutable_command_line(command_line.begin(), command_line.end());
  mutable_command_line.push_back(L'\0');

  STARTUPINFOW startup_info{};
  startup_info.cb = sizeof(startup_info);
  startup_info.dwFlags = STARTF_USESTDHANDLES;
  startup_info.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
  startup_info.hStdOutput = output_write;
  startup_info.hStdError = GetStdHandle(STD_ERROR_HANDLE);
  PROCESS_INFORMATION process_info{};
  const auto executable_text = executable.wstring();
  if (!CreateProcessW(executable_text.c_str(), mutable_command_line.data(), nullptr, nullptr, TRUE, 0,
                      nullptr, nullptr, &startup_info, &process_info)) {
    CloseHandle(output_read);
    CloseHandle(output_write);
    throw RunnerProtocolError("could not start espeak-ng: " + executable.string());
  }
  CloseHandle(output_write);

  std::string output;
  std::array<char, 4096> buffer{};
  DWORD read_count = 0;
  while (ReadFile(output_read, buffer.data(), static_cast<DWORD>(buffer.size()), &read_count, nullptr) &&
         read_count != 0) {
    output.append(buffer.data(), read_count);
  }
  CloseHandle(output_read);
  WaitForSingleObject(process_info.hProcess, INFINITE);
  DWORD exit_code = 1;
  GetExitCodeProcess(process_info.hProcess, &exit_code);
  CloseHandle(process_info.hThread);
  CloseHandle(process_info.hProcess);
  if (exit_code != 0) {
    throw RunnerProtocolError("espeak-ng exited with code " + std::to_string(exit_code));
  }
  return output;
}
#else
std::string run_espeak(const std::filesystem::path &executable,
                       const std::vector<std::string> &arguments) {
  int output_pipe[2]{};
  if (pipe(output_pipe) != 0) {
    throw RunnerProtocolError("could not create espeak-ng output pipe: " + std::string(std::strerror(errno)));
  }
  const auto process_id = fork();
  if (process_id < 0) {
    close(output_pipe[0]);
    close(output_pipe[1]);
    throw RunnerProtocolError("could not fork espeak-ng: " + std::string(std::strerror(errno)));
  }
  if (process_id == 0) {
    close(output_pipe[0]);
    if (dup2(output_pipe[1], STDOUT_FILENO) < 0) {
      _exit(127);
    }
    close(output_pipe[1]);
    std::vector<char *> argv;
    argv.reserve(arguments.size() + 1);
    for (const auto &argument : arguments) {
      argv.push_back(const_cast<char *>(argument.c_str()));
    }
    argv.push_back(nullptr);
    execvp(executable.c_str(), argv.data());
    _exit(127);
  }

  close(output_pipe[1]);
  std::string output;
  std::array<char, 4096> buffer{};
  ssize_t read_count = 0;
  while ((read_count = read(output_pipe[0], buffer.data(), buffer.size())) > 0) {
    output.append(buffer.data(), static_cast<std::size_t>(read_count));
  }
  close(output_pipe[0]);
  int status = 0;
  if (waitpid(process_id, &status, 0) < 0) {
    throw RunnerProtocolError("could not wait for espeak-ng: " + std::string(std::strerror(errno)));
  }
  if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
    throw RunnerProtocolError("espeak-ng exited with code " +
                              std::to_string(WIFEXITED(status) ? WEXITSTATUS(status) : -1));
  }
  return output;
}
#endif

}  // namespace

std::string phonemize_with_espeak_ng(const std::filesystem::path &executable,
                                     std::string_view voice, std::string_view text) {
  if (voice.empty() || text.empty()) {
    throw RunnerProtocolError("espeak-ng voice and text must both be non-empty");
  }
  const std::vector<std::string> arguments{executable.string(), "-q", "--ipa=3", "-v",
                                           std::string(voice), "--", std::string(text)};
  const auto output = trim_one_trailing_line_ending(run_espeak(executable, arguments));
  if (output.empty()) {
    throw RunnerProtocolError("espeak-ng produced no IPA phonemes");
  }
  return output;
}

std::filesystem::path default_espeak_ng_executable() {
#ifdef _WIN32
  std::array<wchar_t, 32768> module_path{};
  const auto length = GetModuleFileNameW(nullptr, module_path.data(), static_cast<DWORD>(module_path.size()));
  if (length == 0 || length >= module_path.size()) {
    throw RunnerProtocolError("could not resolve the Kokoro runner directory for espeak-ng");
  }
  return std::filesystem::path(std::wstring(module_path.data(), length)).parent_path() / "espeak_ng.exe";
#else
  return "espeak-ng";
#endif
}

}  // namespace tts_host
