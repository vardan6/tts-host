#pragma once

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

namespace tts_host {

class RunnerProtocolError : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};

inline constexpr int kRunnerProtocolVersion = 1;
inline constexpr std::size_t kRunnerAudioFrameHeaderBytes = 20;
inline constexpr std::size_t kMaximumRunnerAudioFramePayloadBytes = 16 * 1024 * 1024;
inline constexpr std::uint32_t kRunnerAudioFrameFlagEndOfStream = 1;

struct RunnerInitializeRequest {
  nlohmann::json id;
  int protocol_version;
};

struct RunnerInitializeResponse {
  nlohmann::json id;
  int protocol_version;
  std::vector<std::string> capabilities;
};

RunnerInitializeRequest parse_runner_initialize_request(const nlohmann::json &message);
nlohmann::json make_runner_initialize_request(nlohmann::json id);
nlohmann::json make_runner_initialize_response(const RunnerInitializeRequest &request,
                                               std::vector<std::string> capabilities);
RunnerInitializeResponse parse_runner_initialize_response(const nlohmann::json &message);

struct RunnerLoadRequest {
  nlohmann::json id;
  std::string model_path;
  std::optional<std::string> voice_path;
};

struct RunnerLoadResponse {
  nlohmann::json id;
};

RunnerLoadRequest parse_runner_load_request(const nlohmann::json &message);
nlohmann::json make_runner_load_request(nlohmann::json id, std::string model_path,
                                        std::optional<std::string> voice_path = std::nullopt);
nlohmann::json make_runner_load_response(const RunnerLoadRequest &request);
RunnerLoadResponse parse_runner_load_response(const nlohmann::json &message);

struct RunnerSynthesizeRequest {
  nlohmann::json id;
  std::string text;
};

struct RunnerSynthesizeResponse {
  nlohmann::json id;
  std::uint32_t sample_rate_hz;
  std::uint32_t channels;
  std::string sample_format;
  std::uint64_t total_sample_frames;
};

RunnerSynthesizeRequest parse_runner_synthesize_request(const nlohmann::json &message);
nlohmann::json make_runner_synthesize_request(nlohmann::json id, std::string text);
nlohmann::json make_runner_synthesize_response(const RunnerSynthesizeRequest &request,
                                               std::uint32_t sample_rate_hz,
                                               std::uint32_t channels,
                                               std::string sample_format,
                                               std::uint64_t total_sample_frames);
RunnerSynthesizeResponse parse_runner_synthesize_response(const nlohmann::json &message);

struct RunnerStatsRequest {
  nlohmann::json id;
};

// Reports the most recent synthesize call: peak RSS and VRAM are lifetime
// process peaks, not per-call, since the OS/driver APIs that report them
// don't expose per-call attribution. `peak_vram_bytes` is 0 on a runner with
// no GPU allocation to report (e.g. a CPU-only ONNX execution provider).
struct RunnerStatsResponse {
  nlohmann::json id;
  std::uint64_t peak_rss_bytes;
  std::uint64_t peak_vram_bytes;
  double time_to_first_chunk_ms;
  std::uint64_t sample_count;
};

RunnerStatsRequest parse_runner_stats_request(const nlohmann::json &message);
nlohmann::json make_runner_stats_request(nlohmann::json id);
nlohmann::json make_runner_stats_response(const RunnerStatsRequest &request,
                                          std::uint64_t peak_rss_bytes,
                                          std::uint64_t peak_vram_bytes,
                                          double time_to_first_chunk_ms,
                                          std::uint64_t sample_count);
RunnerStatsResponse parse_runner_stats_response(const nlohmann::json &message);

struct RunnerAudioFrame {
  std::uint64_t sequence_number;
  std::uint32_t sample_count;
  std::uint32_t flags;
  std::vector<std::uint8_t> payload;
};

std::vector<std::uint8_t> frame_runner_audio_message(const RunnerAudioFrame &frame);

class RunnerAudioFrameParser {
 public:
  std::vector<RunnerAudioFrame> push(const std::vector<std::uint8_t> &bytes);

 private:
  std::vector<std::uint8_t> buffer_;
};

std::string frame_runner_control_message(const nlohmann::json &message);

class RunnerControlMessageParser {
 public:
  std::vector<nlohmann::json> push(std::string_view bytes);

 private:
  std::string buffer_;
};

}  // namespace tts_host
