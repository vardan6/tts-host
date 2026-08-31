#include "tts_host/runner_audio_output.hpp"
#include "tts_host/runner_protocol.hpp"
#include "tts_host/stub_runner.hpp"

#include <iostream>
#include <stdexcept>
#include <string>

#ifndef _WIN32
#include <cstdlib>
#include <unistd.h>
#endif

namespace {

void require(bool condition, const std::string &message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void expect_protocol_error(const std::string &frame, const std::string &expected_message) {
  tts_host::RunnerControlMessageParser parser;
  try {
    static_cast<void>(parser.push(frame));
  } catch (const tts_host::RunnerProtocolError &error) {
    require(std::string(error.what()).find(expected_message) != std::string::npos,
            "protocol error did not contain expected message");
    return;
  }
  throw std::runtime_error("expected runner protocol error");
}

void expect_initialize_error(const nlohmann::json &message, const std::string &expected_message) {
  try {
    static_cast<void>(tts_host::handle_stub_runner_control_message(message));
  } catch (const tts_host::RunnerProtocolError &error) {
    require(std::string(error.what()).find(expected_message) != std::string::npos,
            "initialize error did not contain expected message");
    return;
  }
  throw std::runtime_error("expected initialize error");
}

void expect_synthesize_error(const nlohmann::json &message, const std::string &expected_message) {
  try {
    static_cast<void>(tts_host::handle_stub_runner_synthesize_message(message));
  } catch (const tts_host::RunnerProtocolError &error) {
    require(std::string(error.what()).find(expected_message) != std::string::npos,
            "synthesize error did not contain expected message");
    return;
  }
  throw std::runtime_error("expected synthesize error");
}

void expect_audio_protocol_error(const std::vector<std::uint8_t> &frame,
                                 const std::string &expected_message) {
  tts_host::RunnerAudioFrameParser parser;
  try {
    static_cast<void>(parser.push(frame));
  } catch (const tts_host::RunnerProtocolError &error) {
    require(std::string(error.what()).find(expected_message) != std::string::npos,
            "audio protocol error did not contain expected message");
    return;
  }
  throw std::runtime_error("expected audio protocol error");
}

#ifndef _WIN32
void require_inherited_audio_pipe_writes_frames() {
  int pipe_descriptors[2]{};
  require(pipe(pipe_descriptors) == 0, "could not create audio test pipe");
  const auto descriptor_text = std::to_string(pipe_descriptors[1]);
  require(setenv("TTS_HOST_AUDIO_FD", descriptor_text.c_str(), 1) == 0,
          "could not set audio pipe environment variable");

  const tts_host::RunnerAudioFrame expected{
      .sequence_number = 7,
      .sample_count = 1,
      .flags = 0,
      .payload = {0x34, 0x12},
  };
  tts_host::RunnerAudioOutput::open_inherited().write_frame(expected);
  require(close(pipe_descriptors[1]) == 0, "could not close audio test pipe writer");
  unsetenv("TTS_HOST_AUDIO_FD");

  std::vector<std::uint8_t> bytes(tts_host::kRunnerAudioFrameHeaderBytes + expected.payload.size());
  const auto read_count = read(pipe_descriptors[0], bytes.data(), bytes.size());
  require(close(pipe_descriptors[0]) == 0, "could not close audio test pipe reader");
  require(read_count == static_cast<ssize_t>(bytes.size()), "audio test pipe returned incomplete frame");

  tts_host::RunnerAudioFrameParser parser;
  const auto frames = parser.push(bytes);
  require(frames.size() == 1 && frames.front().sequence_number == expected.sequence_number &&
              frames.front().sample_count == expected.sample_count &&
              frames.front().payload == expected.payload,
          "inherited audio pipe frame did not round-trip");
}
#endif

}  // namespace

int main() {
  try {
    const auto initialize = tts_host::make_runner_initialize_request(1);
    const auto framed_initialize = tts_host::frame_runner_control_message(initialize);
    require(framed_initialize.starts_with("Content-Length: "), "missing Content-Length frame");

    tts_host::RunnerControlMessageParser parser;
    const auto split = framed_initialize.size() / 2;
    require(parser.push(std::string_view(framed_initialize).substr(0, split)).empty(),
            "fragment should not produce a message");
    const auto parsed_initialize = parser.push(std::string_view(framed_initialize).substr(split));
    require(parsed_initialize.size() == 1 && parsed_initialize.front() == initialize,
            "fragmented frame did not round-trip");

    const auto stats = nlohmann::json{{"jsonrpc", "2.0"}, {"id", 2}, {"method", "stats"}};
    const auto parsed_messages = parser.push(tts_host::frame_runner_control_message(initialize) +
                                             tts_host::frame_runner_control_message(stats));
    require(parsed_messages.size() == 2 && parsed_messages[0] == initialize &&
                parsed_messages[1] == stats,
            "adjacent frames did not parse in order");

    const auto response = tts_host::handle_stub_runner_control_message(initialize);
    const auto parsed_response = tts_host::parse_runner_initialize_response(response);
    require(parsed_response.id == 1, "stub runner response id did not match request");
    require(parsed_response.protocol_version == tts_host::kRunnerProtocolVersion,
            "stub runner returned the wrong protocol version");
    require(parsed_response.capabilities ==
                std::vector<std::string>{"load", "unload", "synthesize", "cancel", "stats"},
            "stub runner returned unexpected capabilities");

    const auto framed_response = tts_host::frame_runner_control_message(response);
    tts_host::RunnerControlMessageParser response_parser;
    const auto response_messages = response_parser.push(framed_response);
    require(response_messages.size() == 1 && response_messages.front() == response,
            "stub runner response did not round-trip through control framing");

    const auto load = tts_host::make_runner_load_request(2, "/models/demo/demo.onnx");
    const auto load_response = tts_host::handle_stub_runner_load_message(load);
    const auto parsed_load_response = tts_host::parse_runner_load_response(load_response);
    require(parsed_load_response.id == 2, "stub runner load response id did not match request");

    const auto synthesize = nlohmann::json{{"jsonrpc", "2.0"},
                                           {"id", "synthesis-1"},
                                           {"method", "synthesize"},
                                           {"params", {{"text", "Hello, world."}}}};
    const auto synthesis_response = tts_host::handle_stub_runner_synthesize_message(synthesize);
    const auto parsed_synthesis_response =
        tts_host::parse_runner_synthesize_response(synthesis_response);
    require(parsed_synthesis_response.id == "synthesis-1" &&
                parsed_synthesis_response.sample_rate_hz == 24000 &&
                parsed_synthesis_response.channels == 1 &&
                parsed_synthesis_response.sample_format == "pcm_s16le" &&
                parsed_synthesis_response.total_sample_frames == 4,
            "stub runner returned unexpected synthesis metadata");

    const auto stats_request = tts_host::make_runner_stats_request(9);
    const auto parsed_stats_request = tts_host::parse_runner_stats_request(stats_request);
    require(parsed_stats_request.id == 9, "stats request id did not round-trip");
    const auto stats_response =
        tts_host::make_runner_stats_response(parsed_stats_request, 123456, 0, 42.5, 4);
    const auto parsed_stats_response = tts_host::parse_runner_stats_response(stats_response);
    require(parsed_stats_response.id == 9 && parsed_stats_response.peak_rss_bytes == 123456 &&
                parsed_stats_response.peak_vram_bytes == 0 &&
                parsed_stats_response.time_to_first_chunk_ms == 42.5 &&
                parsed_stats_response.sample_count == 4,
            "stats response did not round-trip");

    const auto stub_audio = tts_host::make_stub_runner_synthesis_frame();
    require(stub_audio.sequence_number == 0 && stub_audio.sample_count == 4 &&
                stub_audio.flags == tts_host::kRunnerAudioFrameFlagEndOfStream &&
                stub_audio.payload ==
                    std::vector<std::uint8_t>{0x00, 0x00, 0x00, 0x10, 0x00, 0xf0, 0x00, 0x00},
            "stub runner emitted unexpected deterministic PCM audio");

    const tts_host::RunnerAudioFrame audio_frame{
        .sequence_number = 42,
        .sample_count = 2,
        .flags = 1,
        .payload = {0x00, 0x01, 0x7f, 0xff},
    };
    const auto framed_audio = tts_host::frame_runner_audio_message(audio_frame);
    require(framed_audio.size() == tts_host::kRunnerAudioFrameHeaderBytes + audio_frame.payload.size(),
            "audio frame has an unexpected size");
    require(framed_audio[0] == 0 && framed_audio[3] == audio_frame.payload.size() &&
                framed_audio[11] == 42,
            "audio frame header is not big-endian");

    tts_host::RunnerAudioFrameParser audio_parser;
    const auto audio_split = framed_audio.size() - 1;
    require(audio_parser.push({framed_audio.begin(), framed_audio.begin() + audio_split}).empty(),
            "incomplete audio frame produced a message");
    const auto parsed_audio = audio_parser.push({framed_audio.back()});
    require(parsed_audio.size() == 1 && parsed_audio.front().sequence_number == 42 &&
                parsed_audio.front().sample_count == 2 && parsed_audio.front().flags == 1 &&
                parsed_audio.front().payload == audio_frame.payload,
            "fragmented audio frame did not round-trip");

    const auto parsed_adjacent_audio = audio_parser.push(framed_audio);
    require(parsed_adjacent_audio.size() == 1 && parsed_adjacent_audio.front().payload == audio_frame.payload,
            "adjacent audio frame did not parse");

    expect_protocol_error("\r\n\r\n{}", "missing a Content-Length");
    expect_protocol_error("Content-Length: nope\r\n\r\n{}", "invalid Content-Length");
    expect_protocol_error("Content-Length: 1\r\n\r\nx", "invalid JSON");
    expect_initialize_error(nlohmann::json{{"jsonrpc", "2.0"},
                                            {"id", 1},
                                            {"method", "initialize"},
                                            {"params", {{"protocolVersion", "one"}}}},
                            "invalid protocolVersion");
    expect_synthesize_error(nlohmann::json{{"jsonrpc", "2.0"},
                                            {"id", 1},
                                            {"method", "synthesize"},
                                            {"params", {{"text", ""}}}},
                            "invalid text");
    expect_audio_protocol_error(
        {1, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
        "maximum payload size");
#ifndef _WIN32
    require_inherited_audio_pipe_writes_frames();
#endif
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
  return 0;
}
