#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace tts_host {

// Raw, short-read stdin/stdout I/O for a runner's JSON-RPC control channel.
// The channel is a pipe, not a regular file: buffered iostream reads
// (std::cin.read) block until the requested count is fully satisfied rather
// than returning whatever is already available, which deadlocks against a
// host that is waiting on a reply to a short request.
std::vector<std::uint8_t> read_some_stdin();

// Throws RunnerProtocolError if the bytes cannot be written.
void write_stdout(const std::string &bytes);

}  // namespace tts_host
