#include "tts_host/runner_launcher.hpp"

#include <array>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <stdexcept>

#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;
#endif

namespace tts_host {
namespace {

#ifdef _WIN32

[[noreturn]] void throw_windows_error(const std::string &what) {
  throw RunnerLaunchError(what + " (Windows error " + std::to_string(GetLastError()) + ")");
}

HANDLE make_inheritable_child_handle(HANDLE original) {
  HANDLE duplicate = nullptr;
  if (!DuplicateHandle(GetCurrentProcess(), original, GetCurrentProcess(), &duplicate, 0, TRUE,
                       DUPLICATE_SAME_ACCESS)) {
    throw_windows_error("could not duplicate a handle for runner inheritance");
  }
  return duplicate;
}

// Builds an ANSI environment block equal to the current process environment
// plus TTS_HOST_AUDIO_HANDLE, without mutating this process's own environment.
std::vector<char> build_child_environment(const std::string &audio_handle_value) {
  const auto *strings = GetEnvironmentStringsA();
  if (strings == nullptr) {
    throw_windows_error("could not read the current environment");
  }

  std::vector<char> block;
  const char *cursor = strings;
  while (*cursor != '\0') {
    const auto length = std::strlen(cursor);
    block.insert(block.end(), cursor, cursor + length + 1);
    cursor += length + 1;
  }
  FreeEnvironmentStringsA(const_cast<char *>(strings));

  const std::string entry = "TTS_HOST_AUDIO_HANDLE=" + audio_handle_value;
  block.insert(block.end(), entry.begin(), entry.end());
  block.push_back('\0');
  block.push_back('\0');
  return block;
}

#endif

}  // namespace

#ifdef _WIN32

RunnerSession::RunnerSession(const std::filesystem::path &runner_executable) {
  SECURITY_ATTRIBUTES inheritable_pipe{};
  inheritable_pipe.nLength = sizeof(inheritable_pipe);
  inheritable_pipe.bInheritHandle = TRUE;

  HANDLE child_stdin_read = nullptr;
  HANDLE host_stdin_write = nullptr;
  if (!CreatePipe(&child_stdin_read, &host_stdin_write, &inheritable_pipe, 0)) {
    throw_windows_error("could not create the runner control input pipe");
  }
  SetHandleInformation(host_stdin_write, HANDLE_FLAG_INHERIT, 0);

  HANDLE host_stdout_read = nullptr;
  HANDLE child_stdout_write = nullptr;
  if (!CreatePipe(&host_stdout_read, &child_stdout_write, &inheritable_pipe, 0)) {
    throw_windows_error("could not create the runner control output pipe");
  }
  SetHandleInformation(host_stdout_read, HANDLE_FLAG_INHERIT, 0);

  HANDLE host_audio_read = nullptr;
  HANDLE child_audio_write = nullptr;
  if (!CreatePipe(&host_audio_read, &child_audio_write, &inheritable_pipe, 0)) {
    throw_windows_error("could not create the runner audio pipe");
  }
  SetHandleInformation(host_audio_read, HANDLE_FLAG_INHERIT, 0);

  const HANDLE child_stderr = make_inheritable_child_handle(GetStdHandle(STD_ERROR_HANDLE));

  const auto environment_block =
      build_child_environment(std::to_string(reinterpret_cast<std::uintptr_t>(child_audio_write)));

  STARTUPINFOA startup_info{};
  startup_info.cb = sizeof(startup_info);
  startup_info.dwFlags = STARTF_USESTDHANDLES;
  startup_info.hStdInput = child_stdin_read;
  startup_info.hStdOutput = child_stdout_write;
  startup_info.hStdError = child_stderr;

  PROCESS_INFORMATION process_info{};
  std::string application_name = runner_executable.string();
  const BOOL created = CreateProcessA(
      application_name.c_str(), nullptr, nullptr, nullptr, TRUE, 0,
      const_cast<char *>(environment_block.data()), nullptr, &startup_info, &process_info);

  CloseHandle(child_stdin_read);
  CloseHandle(child_stdout_write);
  CloseHandle(child_audio_write);
  CloseHandle(child_stderr);

  if (!created) {
    CloseHandle(host_stdin_write);
    CloseHandle(host_stdout_read);
    CloseHandle(host_audio_read);
    throw_windows_error("could not start the runner process (" + application_name + ")");
  }

  CloseHandle(process_info.hThread);

  control_write_handle_ = reinterpret_cast<std::uintptr_t>(host_stdin_write);
  control_read_handle_ = reinterpret_cast<std::uintptr_t>(host_stdout_read);
  audio_read_handle_ = reinterpret_cast<std::uintptr_t>(host_audio_read);
  process_handle_ = reinterpret_cast<std::uintptr_t>(process_info.hProcess);
}

void RunnerSession::close_control_input() {
  if (control_input_closed_) {
    return;
  }
  CloseHandle(reinterpret_cast<HANDLE>(control_write_handle_));
  control_input_closed_ = true;
}

int RunnerSession::finish() {
  if (finished_) {
    return 0;
  }
  close_control_input();

  const auto process = reinterpret_cast<HANDLE>(process_handle_);
  WaitForSingleObject(process, INFINITE);
  DWORD exit_code = 0;
  GetExitCodeProcess(process, &exit_code);
  CloseHandle(process);
  CloseHandle(reinterpret_cast<HANDLE>(control_read_handle_));
  CloseHandle(reinterpret_cast<HANDLE>(audio_read_handle_));
  finished_ = true;
  return static_cast<int>(exit_code);
}

RunnerSession::~RunnerSession() {
  if (!finished_) {
    finish();
  }
}

namespace {

std::vector<std::uint8_t> read_some(HANDLE handle) {
  std::array<char, 4096> buffer{};
  DWORD bytes_read = 0;
  if (!ReadFile(handle, buffer.data(), static_cast<DWORD>(buffer.size()), &bytes_read, nullptr)) {
    if (GetLastError() == ERROR_BROKEN_PIPE) {
      return {};
    }
    throw_windows_error("could not read from the runner");
  }
  return std::vector<std::uint8_t>(buffer.begin(), buffer.begin() + bytes_read);
}

void write_all(HANDLE handle, const std::string &bytes) {
  std::size_t offset = 0;
  while (offset < bytes.size()) {
    DWORD written = 0;
    if (!WriteFile(handle, bytes.data() + offset, static_cast<DWORD>(bytes.size() - offset),
                   &written, nullptr) ||
        written == 0) {
      throw_windows_error("could not write to the runner");
    }
    offset += written;
  }
}

}  // namespace

nlohmann::json RunnerSession::send_request(const nlohmann::json &request) {
  write_all(reinterpret_cast<HANDLE>(control_write_handle_), frame_runner_control_message(request));
  return receive_control_message();
}

nlohmann::json RunnerSession::receive_control_message() {
  while (pending_control_messages_.empty()) {
    const auto bytes = read_some(reinterpret_cast<HANDLE>(control_read_handle_));
    if (bytes.empty()) {
      throw RunnerLaunchError("runner closed its control channel before responding");
    }
    for (auto &message :
        control_parser_.push(std::string_view(reinterpret_cast<const char *>(bytes.data()), bytes.size()))) {
      pending_control_messages_.push_back(std::move(message));
    }
  }
  auto message = std::move(pending_control_messages_.front());
  pending_control_messages_.pop_front();
  return message;
}

std::vector<RunnerAudioFrame> RunnerSession::read_audio_stream_until_end() {
  std::vector<RunnerAudioFrame> frames;
  bool saw_end_of_stream = false;
  while (!saw_end_of_stream) {
    const auto bytes = read_some(reinterpret_cast<HANDLE>(audio_read_handle_));
    if (bytes.empty()) {
      break;
    }
    for (auto &frame : audio_parser_.push(bytes)) {
      saw_end_of_stream = saw_end_of_stream || (frame.flags & kRunnerAudioFrameFlagEndOfStream) != 0;
      frames.push_back(std::move(frame));
    }
  }
  if (!saw_end_of_stream) {
    throw RunnerLaunchError("runner audio channel closed before the end of the stream");
  }
  return frames;
}

#else  // POSIX

namespace {

void set_close_on_exec(int file_descriptor, bool value) {
  const int flags = fcntl(file_descriptor, F_GETFD);
  if (flags == -1) {
    throw RunnerLaunchError("could not read descriptor flags");
  }
  const int updated = value ? (flags | FD_CLOEXEC) : (flags & ~FD_CLOEXEC);
  if (fcntl(file_descriptor, F_SETFD, updated) == -1) {
    throw RunnerLaunchError("could not set descriptor flags");
  }
}

std::vector<std::uint8_t> read_some(int file_descriptor) {
  std::array<char, 4096> buffer{};
  while (true) {
    const auto bytes_read = read(file_descriptor, buffer.data(), buffer.size());
    if (bytes_read < 0) {
      if (errno == EINTR) {
        continue;
      }
      throw RunnerLaunchError(std::string("could not read from the runner (errno ") +
                              std::to_string(errno) + ")");
    }
    return std::vector<std::uint8_t>(buffer.begin(), buffer.begin() + bytes_read);
  }
}

void write_all(int file_descriptor, const std::string &bytes) {
  std::size_t offset = 0;
  while (offset < bytes.size()) {
    const auto written = write(file_descriptor, bytes.data() + offset, bytes.size() - offset);
    if (written < 0) {
      if (errno == EINTR) {
        continue;
      }
      throw RunnerLaunchError(std::string("could not write to the runner (errno ") +
                              std::to_string(errno) + ")");
    }
    offset += static_cast<std::size_t>(written);
  }
}

}  // namespace

RunnerSession::RunnerSession(const std::filesystem::path &runner_executable) {
  int stdin_pipe[2];
  int stdout_pipe[2];
  int audio_pipe[2];
  if (pipe(stdin_pipe) != 0 || pipe(stdout_pipe) != 0 || pipe(audio_pipe) != 0) {
    throw RunnerLaunchError("could not create runner pipes");
  }

  // The host's own ends must never leak across exec, in this process or the
  // runner's, or EOF detection on process exit breaks.
  set_close_on_exec(stdin_pipe[1], true);
  set_close_on_exec(stdout_pipe[0], true);
  set_close_on_exec(audio_pipe[0], true);

  const auto executable = runner_executable.string();
  const auto audio_fd_value = std::to_string(audio_pipe[1]);

  const pid_t pid = fork();
  if (pid < 0) {
    throw RunnerLaunchError("could not fork the runner process");
  }

  if (pid == 0) {
    dup2(stdin_pipe[0], STDIN_FILENO);
    dup2(stdout_pipe[1], STDOUT_FILENO);
    close(stdin_pipe[0]);
    close(stdin_pipe[1]);
    close(stdout_pipe[0]);
    close(stdout_pipe[1]);
    close(audio_pipe[0]);

    setenv("TTS_HOST_AUDIO_FD", audio_fd_value.c_str(), 1);

    std::array<char *, 2> argv{const_cast<char *>(executable.c_str()), nullptr};
    execve(executable.c_str(), argv.data(), environ);
    _exit(127);
  }

  close(stdin_pipe[0]);
  close(stdout_pipe[1]);
  close(audio_pipe[1]);

  control_write_handle_ = static_cast<std::uintptr_t>(stdin_pipe[1]);
  control_read_handle_ = static_cast<std::uintptr_t>(stdout_pipe[0]);
  audio_read_handle_ = static_cast<std::uintptr_t>(audio_pipe[0]);
  process_handle_ = static_cast<std::uintptr_t>(pid);
}

void RunnerSession::close_control_input() {
  if (control_input_closed_) {
    return;
  }
  close(static_cast<int>(control_write_handle_));
  control_input_closed_ = true;
}

int RunnerSession::finish() {
  if (finished_) {
    return 0;
  }
  close_control_input();

  int status = 0;
  while (waitpid(static_cast<pid_t>(process_handle_), &status, 0) < 0 && errno == EINTR) {
  }
  close(static_cast<int>(control_read_handle_));
  close(static_cast<int>(audio_read_handle_));
  finished_ = true;
  return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

RunnerSession::~RunnerSession() {
  if (!finished_) {
    finish();
  }
}

nlohmann::json RunnerSession::send_request(const nlohmann::json &request) {
  write_all(static_cast<int>(control_write_handle_), frame_runner_control_message(request));
  return receive_control_message();
}

nlohmann::json RunnerSession::receive_control_message() {
  while (pending_control_messages_.empty()) {
    const auto bytes = read_some(static_cast<int>(control_read_handle_));
    if (bytes.empty()) {
      throw RunnerLaunchError("runner closed its control channel before responding");
    }
    for (auto &message :
        control_parser_.push(std::string_view(reinterpret_cast<const char *>(bytes.data()), bytes.size()))) {
      pending_control_messages_.push_back(std::move(message));
    }
  }
  auto message = std::move(pending_control_messages_.front());
  pending_control_messages_.pop_front();
  return message;
}

std::vector<RunnerAudioFrame> RunnerSession::read_audio_stream_until_end() {
  std::vector<RunnerAudioFrame> frames;
  bool saw_end_of_stream = false;
  while (!saw_end_of_stream) {
    const auto bytes = read_some(static_cast<int>(audio_read_handle_));
    if (bytes.empty()) {
      break;
    }
    for (auto &frame : audio_parser_.push(bytes)) {
      saw_end_of_stream = saw_end_of_stream || (frame.flags & kRunnerAudioFrameFlagEndOfStream) != 0;
      frames.push_back(std::move(frame));
    }
  }
  if (!saw_end_of_stream) {
    throw RunnerLaunchError("runner audio channel closed before the end of the stream");
  }
  return frames;
}

#endif

}  // namespace tts_host
