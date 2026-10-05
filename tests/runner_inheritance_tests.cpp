#include "tts_host/runner_launcher.hpp"
#include <windows.h>
#include <barrier>
#include <future>
#include <iostream>
#include <stdexcept>

int main(int argc, char **argv) {
  try {
    if (argc != 2) throw std::runtime_error("expected stub runner path");
    // Keep unrelated inheritable writers open across concurrent child launches.
    // Neither child may retain them, even while it is alive waiting for input.
    SECURITY_ATTRIBUTES attributes{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    HANDLE read = nullptr, write = nullptr;
    if (!CreatePipe(&read, &write, &attributes, 0)) throw std::runtime_error("CreatePipe failed");
    SetHandleInformation(read, HANDLE_FLAG_INHERIT, 0);
    std::barrier ready(2);
    auto launch = [&] {
      ready.arrive_and_wait();
      return std::make_unique<tts_host::RunnerSession>(std::filesystem::path(argv[1]));
    };
    auto pending = std::async(std::launch::async, launch);
    auto second = launch();
    auto first = pending.get();
    CloseHandle(write);
    DWORD available = 0;
    const BOOL peeked = PeekNamedPipe(read, nullptr, 0, nullptr, &available, nullptr);
    const DWORD error = GetLastError();
    CloseHandle(read);
    // Finish one while the other is still alive: its stdin EOF must not depend
    // on the lifetime of that other session.
    if (first->finish() != 0) throw std::runtime_error("first runner failed");
    auto response = second->send_request(tts_host::make_runner_initialize_request(1));
    tts_host::parse_runner_initialize_response(response);
    second->send_request(tts_host::make_runner_synthesize_request(2, "isolated pipes"));
    if (second->read_audio_stream_until_end().empty()) throw std::runtime_error("audio missing");
    if (second->finish() != 0) throw std::runtime_error("second runner failed");
    if (peeked || error != ERROR_BROKEN_PIPE) {
      throw std::runtime_error("runner inherited an unrelated pipe writer");
    }
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
  return 0;
}
