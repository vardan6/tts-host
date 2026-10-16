#include "tts_host/runner_launcher.hpp"
#include "tts_host/path_encoding.hpp"

#include <algorithm>
#include <atomic>
#include <thread>
#include <array>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <exception>
#include <stdexcept>

#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <pthread.h>
#include <spawn.h>
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
  // GUI-subsystem launches (Explorer, login) have no standard handles; give
  // the child the NUL device instead of failing to duplicate a null handle.
  if (original == nullptr || original == INVALID_HANDLE_VALUE) {
    SECURITY_ATTRIBUTES inheritable{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    HANDLE null_device = CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                     &inheritable, OPEN_EXISTING, 0, nullptr);
    if (null_device == INVALID_HANDLE_VALUE) {
      throw_windows_error("could not open the null device for runner inheritance");
    }
    return null_device;
  }
  HANDLE duplicate = nullptr;
  if (!DuplicateHandle(GetCurrentProcess(), original, GetCurrentProcess(), &duplicate, 0, TRUE,
                       DUPLICATE_SAME_ACCESS)) {
    throw_windows_error("could not duplicate a handle for runner inheritance");
  }
  return duplicate;
}

// Builds a Unicode environment block equal to the current process environment
// plus TTS_HOST_AUDIO_HANDLE, without mutating this process's own environment.
std::vector<wchar_t> build_child_environment(const std::string &audio_handle_value) {
  const auto *strings = GetEnvironmentStringsW();
  if (strings == nullptr) {
    throw_windows_error("could not read the current environment");
  }

  std::vector<wchar_t> block;
  const wchar_t *cursor = strings;
  while (*cursor != '\0') {
    const auto length = std::wcslen(cursor);
    block.insert(block.end(), cursor, cursor + length + 1);
    cursor += length + 1;
  }
  FreeEnvironmentStringsW(const_cast<wchar_t *>(strings));

  const std::wstring entry = L"TTS_HOST_AUDIO_HANDLE=" +
      std::wstring(audio_handle_value.begin(), audio_handle_value.end());
  block.insert(block.end(), entry.begin(), entry.end());
  block.push_back('\0');
  block.push_back('\0');
  return block;
}

#endif

}  // namespace

#ifdef _WIN32

RunnerSession::RunnerSession(const std::filesystem::path &runner_executable,
                             RunnerDeadlines deadlines, std::function<bool()> cancelled)
    : deadlines_(deadlines), cancelled_(std::move(cancelled)) {
  // Own every temporary handle, including construction failures.
  struct Handles {
    std::vector<HANDLE> values;
    ~Handles() { for (HANDLE value : values) if (value) CloseHandle(value); }
    void release(HANDLE value) {
      for (auto &entry : values) if (entry == value) entry = nullptr;
    }
  } handles;
  // Host endpoints use overlapped named pipes so deadlines/cancellation never
  // depend on a child draining or writing its anonymous pipe.
  static std::atomic_uint64_t pipe_id{0};
  auto pipe = [&](HANDLE &read, HANDLE &write, bool host_reads) {
    const auto name = L"\\\\.\\pipe\\tts-host-" + std::to_wstring(GetCurrentProcessId()) +
                      L"-" + std::to_wstring(pipe_id.fetch_add(1));
    HANDLE host = CreateNamedPipeW(name.c_str(),
        (host_reads ? PIPE_ACCESS_INBOUND : PIPE_ACCESS_OUTBOUND) | FILE_FLAG_OVERLAPPED |
            FILE_FLAG_FIRST_PIPE_INSTANCE,
        PIPE_TYPE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS, 1, 4096, 4096, 0, nullptr);
    if (host == INVALID_HANDLE_VALUE) throw_windows_error("could not create a runner pipe");
    handles.values.push_back(host);
    SECURITY_ATTRIBUTES inheritable{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    HANDLE child = CreateFileW(name.c_str(), host_reads ? GENERIC_WRITE : GENERIC_READ,
        0, &inheritable, OPEN_EXISTING, 0, nullptr);
    if (child == INVALID_HANDLE_VALUE) throw_windows_error("could not open a runner pipe");
    handles.values.push_back(child);
    // Opening the client before connecting completes the connection immediately.
    OVERLAPPED connection{};
    if (!ConnectNamedPipe(host, &connection) && GetLastError() != ERROR_PIPE_CONNECTED) {
      throw_windows_error("could not connect a runner pipe");
    }
    read = host_reads ? host : child;
    write = host_reads ? child : host;
  };
  HANDLE child_stdin_read = nullptr, host_stdin_write = nullptr;
  HANDLE host_stdout_read = nullptr, child_stdout_write = nullptr;
  HANDLE host_audio_read = nullptr, child_audio_write = nullptr;
  pipe(child_stdin_read, host_stdin_write, false);
  pipe(host_stdout_read, child_stdout_write, true);
  pipe(host_audio_read, child_audio_write, true);
  const HANDLE child_stderr = make_inheritable_child_handle(GetStdHandle(STD_ERROR_HANDLE));
  handles.values.push_back(child_stderr);

  const auto environment_block =
      build_child_environment(std::to_string(reinterpret_cast<std::uintptr_t>(child_audio_write)));

  SIZE_T attribute_bytes = 0;
  InitializeProcThreadAttributeList(nullptr, 1, 0, &attribute_bytes);
  std::vector<std::uint8_t> attribute_storage(attribute_bytes);
  auto *attributes = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attribute_storage.data());
  if (!InitializeProcThreadAttributeList(attributes, 1, 0, &attribute_bytes)) {
    throw_windows_error("could not initialize runner handle inheritance");
  }
  struct AttributeCleanup {
    LPPROC_THREAD_ATTRIBUTE_LIST value;
    ~AttributeCleanup() { DeleteProcThreadAttributeList(value); }
  } attribute_cleanup{attributes};
  std::array<HANDLE, 4> inherited{child_stdin_read, child_stdout_write,
                                   child_audio_write, child_stderr};
  if (!UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                                 inherited.data(), sizeof(inherited), nullptr, nullptr)) {
    throw_windows_error("could not restrict runner handle inheritance");
  }
  STARTUPINFOEXW startup_info{};
  startup_info.StartupInfo.cb = sizeof(startup_info);
  startup_info.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
  startup_info.StartupInfo.hStdInput = child_stdin_read;
  startup_info.StartupInfo.hStdOutput = child_stdout_write;
  startup_info.StartupInfo.hStdError = child_stderr;
  startup_info.lpAttributeList = attributes;

  PROCESS_INFORMATION process_info{};
  const std::wstring application_name = runner_executable.wstring();
  if (!CreateProcessW(application_name.c_str(), nullptr, nullptr, nullptr, TRUE,
                       EXTENDED_STARTUPINFO_PRESENT | CREATE_UNICODE_ENVIRONMENT,
                       const_cast<wchar_t *>(environment_block.data()), nullptr,
                       &startup_info.StartupInfo, &process_info)) {
    throw_windows_error("could not start the runner process (" + path_to_utf8(runner_executable) + ")");
  }
  CloseHandle(process_info.hThread);
  handles.release(host_stdin_write);
  handles.release(host_stdout_read);
  handles.release(host_audio_read);

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
  if (WaitForSingleObject(process, static_cast<DWORD>(deadlines_.shutdown.count())) != WAIT_OBJECT_0) {
    TerminateProcess(process, 1);
    WaitForSingleObject(process, static_cast<DWORD>(deadlines_.shutdown.count()));
  }
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
DWORD transfer(HANDLE handle, void *buffer, DWORD length, bool writing,
               const std::function<void()> &check) {
  OVERLAPPED operation{};
  operation.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  if (!operation.hEvent) throw_windows_error("could not create runner I/O event");
  DWORD count = 0;
  const BOOL completed = writing ? WriteFile(handle, buffer, length, &count, &operation)
                                : ReadFile(handle, buffer, length, &count, &operation);
  if (!completed && GetLastError() != ERROR_IO_PENDING) {
    const auto error = GetLastError();
    CloseHandle(operation.hEvent);
    if (!writing && error == ERROR_BROKEN_PIPE) return 0;
    SetLastError(error);
    throw_windows_error("runner pipe I/O failed");
  }
  try {
    while (!completed && WaitForSingleObject(operation.hEvent, 10) == WAIT_TIMEOUT) check();
    check();
  } catch (...) {
    CancelIoEx(handle, &operation);
    GetOverlappedResult(handle, &operation, &count, TRUE);
    CloseHandle(operation.hEvent);
    throw;
  }
  const BOOL result = GetOverlappedResult(handle, &operation, &count, TRUE);
  const auto error = GetLastError();
  CloseHandle(operation.hEvent);
  if (!result) {
    if (!writing && error == ERROR_BROKEN_PIPE) return 0;
    SetLastError(error);
    throw_windows_error("runner pipe I/O failed");
  }
  return count;
}
}  // namespace

std::vector<std::uint8_t> RunnerSession::read_some(std::uintptr_t handle) {
  check_wait();
  std::array<std::uint8_t, 4096> bytes{};
  const auto count = transfer(reinterpret_cast<HANDLE>(handle), bytes.data(),
                              static_cast<DWORD>(bytes.size()), false, [this] { check_wait(); });
  return {bytes.begin(), bytes.begin() + count};
}

void RunnerSession::write_all(const std::string &bytes) {
  std::size_t offset = 0;
  while (offset < bytes.size()) {
    check_wait();
    const auto count = transfer(reinterpret_cast<HANDLE>(control_write_handle_),
        const_cast<char *>(bytes.data() + offset),
        static_cast<DWORD>(std::min<std::size_t>(bytes.size() - offset, 4096)), true,
        [this] { check_wait(); });
    if (count == 0) throw RunnerLaunchError("runner closed its control input");
    offset += count;
  }
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

void set_nonblocking(int fd) {
  if (fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK) == -1)
    throw RunnerLaunchError("could not make runner pipe nonblocking");
}

// Block SIGPIPE only on this writing thread, consume only the signal generated
// by our failed write, and restore the caller's signal mask (no global handler).
ssize_t write_without_sigpipe(int fd, const void *data, std::size_t size) {
  sigset_t set, previous, pending;
  sigemptyset(&set);
  sigaddset(&set, SIGPIPE);
  pthread_sigmask(SIG_BLOCK, &set, &previous);
  sigpending(&pending);
  const bool already_pending = sigismember(&pending, SIGPIPE);
  const auto result = write(fd, data, size);
  const auto error = errno;
  if (result < 0 && error == EPIPE && !already_pending) {
    sigpending(&pending);
    if (sigismember(&pending, SIGPIPE)) {
      int signal = 0;
      sigwait(&set, &signal);
    }
  }
  pthread_sigmask(SIG_SETMASK, &previous, nullptr);
  errno = error;
  return result;
}
}  // namespace

std::vector<std::uint8_t> RunnerSession::read_some(std::uintptr_t handle) {
  std::array<std::uint8_t, 4096> buffer{};
  while (true) {
    check_wait();
    pollfd descriptor{static_cast<int>(handle), POLLIN, 0};
    const auto ready = poll(&descriptor, 1, 10);
    if (ready < 0 && errno != EINTR) throw RunnerLaunchError("runner read poll failed");
    if (ready <= 0) continue;
    const auto count = read(descriptor.fd, buffer.data(), buffer.size());
    if (count < 0 && (errno == EINTR || errno == EAGAIN)) continue;
    if (count < 0) throw RunnerLaunchError("could not read from runner");
    return {buffer.begin(), buffer.begin() + count};
  }
}

void RunnerSession::write_all(const std::string &bytes) {
  std::size_t offset = 0;
  while (offset < bytes.size()) {
    check_wait();
    pollfd descriptor{static_cast<int>(control_write_handle_), POLLOUT, 0};
    const auto ready = poll(&descriptor, 1, 10);
    if (ready < 0 && errno != EINTR) throw RunnerLaunchError("runner write poll failed");
    if (ready <= 0) continue;
    const auto count = write_without_sigpipe(descriptor.fd, bytes.data() + offset, bytes.size() - offset);
    if (count < 0 && (errno == EINTR || errno == EAGAIN)) continue;
    if (count <= 0) throw RunnerLaunchError("could not write to runner (closed pipe)");
    offset += static_cast<std::size_t>(count);
  }
}

RunnerSession::RunnerSession(const std::filesystem::path &runner_executable,
                             RunnerDeadlines deadlines, std::function<bool()> cancelled)
    : deadlines_(deadlines), cancelled_(std::move(cancelled)) {
  struct Pipes {
    std::vector<int> descriptors;
    ~Pipes() { for (int fd : descriptors) if (fd >= 0) close(fd); }
    void release(int fd) { for (auto &entry : descriptors) if (entry == fd) entry = -1; }
  } pipes;
  auto make_pipe = [&](int (&ends)[2]) {
#ifdef __linux__
    if (pipe2(ends, O_CLOEXEC) != 0) throw RunnerLaunchError("could not create runner pipe");
#else
    if (pipe(ends) != 0) throw RunnerLaunchError("could not create runner pipe");
#endif
    pipes.descriptors.push_back(ends[0]);
    pipes.descriptors.push_back(ends[1]);
    set_close_on_exec(ends[0], true);
    set_close_on_exec(ends[1], true);
  };
  int stdin_pipe[2], stdout_pipe[2], audio_pipe[2];
  make_pipe(stdin_pipe);
  make_pipe(stdout_pipe);
  make_pipe(audio_pipe);
  set_nonblocking(stdin_pipe[1]);
  set_nonblocking(stdout_pipe[0]);
  set_nonblocking(audio_pipe[0]);

  // posix_spawn avoids running allocator/environment code in a forked child
  // of the multithreaded desktop Host, including during recovery.
  posix_spawn_file_actions_t actions;
  if (posix_spawn_file_actions_init(&actions) != 0)
    throw RunnerLaunchError("could not initialize runner spawn actions");
  struct ActionCleanup {
    posix_spawn_file_actions_t &value;
    ~ActionCleanup() { posix_spawn_file_actions_destroy(&value); }
  } action_cleanup{actions};
  if (posix_spawn_file_actions_adddup2(&actions, stdin_pipe[0], STDIN_FILENO) != 0 ||
      posix_spawn_file_actions_adddup2(&actions, stdout_pipe[1], STDOUT_FILENO) != 0 ||
      posix_spawn_file_actions_adddup2(&actions, audio_pipe[1], audio_pipe[1]) != 0)
    throw RunnerLaunchError("could not configure runner inherited descriptors");

  const auto executable = runner_executable.string();
  std::vector<std::string> environment;
  for (auto cursor = environ; *cursor; ++cursor) {
    if (std::string_view(*cursor).starts_with("TTS_HOST_AUDIO_FD=")) continue;
    environment.emplace_back(*cursor);
  }
  environment.push_back("TTS_HOST_AUDIO_FD=" + std::to_string(audio_pipe[1]));
  std::vector<char *> envp;
  for (auto &entry : environment) envp.push_back(entry.data());
  envp.push_back(nullptr);
  std::array<char *, 2> argv{const_cast<char *>(executable.c_str()), nullptr};
  pid_t pid = 0;
  const auto error = posix_spawn(&pid, executable.c_str(), &actions, nullptr, argv.data(), envp.data());
  if (error != 0) throw RunnerLaunchError("could not start runner (errno " + std::to_string(error) + ")");

  pipes.release(stdin_pipe[1]);
  pipes.release(stdout_pipe[0]);
  pipes.release(audio_pipe[0]);
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
  bool reaped = false;
  const auto pid = static_cast<pid_t>(process_handle_);
  const auto deadline = std::chrono::steady_clock::now() + deadlines_.shutdown;
  while (true) {
    const auto result = waitpid(pid, &status, WNOHANG);
    if (result == pid) { reaped = true; break; }
    if (result < 0 && errno != EINTR) break;
    if (std::chrono::steady_clock::now() >= deadline) {
      kill(pid, SIGKILL);
      // Reap with a second bounded wait rather than an unbounded destructor.
      const auto killed_deadline = std::chrono::steady_clock::now() + deadlines_.shutdown;
      while (std::chrono::steady_clock::now() < killed_deadline) {
        const auto killed_result = waitpid(pid, &status, WNOHANG);
        if (killed_result == pid) { reaped = true; break; }
        if (killed_result < 0 && errno != EINTR) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
      }
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  close(static_cast<int>(control_read_handle_));
  close(static_cast<int>(audio_read_handle_));
  finished_ = true;
  return reaped && WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

RunnerSession::~RunnerSession() {
  if (!finished_) {
    finish();
  }
}

#endif

void RunnerSession::check_wait() const {
  if (cancelled_ && cancelled_()) throw RunnerCancelled();
  if (abort_io_) throw RunnerLaunchError("runner synthesis I/O interrupted");
  if (std::chrono::steady_clock::now() >= operation_deadline_)
    throw RunnerLaunchError("runner deadline exceeded; check model resources or try another model");
}

nlohmann::json RunnerSession::send_request(const nlohmann::json &request,
                                         std::chrono::milliseconds timeout) {
  if (finished_ || control_input_closed_) throw RunnerLaunchError("runner session is closed");
  const auto method = request.value("method", "");
  if (timeout == std::chrono::milliseconds::zero()) {
    timeout = method == "load" ? deadlines_.load : method == "synthesize" ? deadlines_.synthesis
              : method == "unload" ? deadlines_.shutdown : deadlines_.control;
  }
  operation_deadline_ = std::chrono::steady_clock::now() + timeout;
  abort_io_ = false;
  if (method == "synthesize" && pending_audio_)
    throw RunnerLaunchError("previous synthesis audio was not consumed");
  write_all(frame_runner_control_message(request));
  if (method == "synthesize") {
    // A streaming engine can emit PCM before it knows the final sample count
    // in its control reply. Drain both pipes to avoid blocking it on audio.
    // The caller still receives only a complete sentence (ADR 0002).
    std::exception_ptr audio_error;
    bool audio_failed_first = false;
    std::thread reader([&] {
      try { pending_audio_ = receive_audio_stream(); }
      catch (...) { audio_error = std::current_exception(); audio_failed_first = !abort_io_.exchange(true); }
    });
    nlohmann::json response;
    std::exception_ptr control_error;
    try { response = receive_control_message(); }
    catch (...) { control_error = std::current_exception(); abort_io_ = true; }
    reader.join();
    abort_io_ = false;
    if (control_error || audio_error) pending_audio_.reset();
    // The first failing reader aborts the other with a generic interruption;
    // report the original failure rather than that secondary error.
    if (audio_error && (audio_failed_first || !control_error)) std::rethrow_exception(audio_error);
    if (control_error) std::rethrow_exception(control_error);
    return response;
  }
  return receive_control_message();
}

nlohmann::json RunnerSession::receive_control_message() {
  while (pending_control_messages_.empty()) {
    const auto bytes = read_some(control_read_handle_);
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
  if (pending_audio_) {
    auto frames = std::move(*pending_audio_);
    pending_audio_.reset();
    return frames;
  }
  return receive_audio_stream();
}

std::vector<RunnerAudioFrame> RunnerSession::receive_audio_stream() {
  std::vector<RunnerAudioFrame> frames;
  bool saw_end_of_stream = false;
  while (!saw_end_of_stream) {
    const auto bytes = read_some(audio_read_handle_);
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

}  // namespace tts_host
