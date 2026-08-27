#include "tts_host/runner_protocol.hpp"

#include <charconv>
#include <cctype>
#include <limits>
#include <optional>

namespace tts_host {
namespace {

constexpr std::size_t kMaximumHeaderBytes = 16 * 1024;
constexpr std::size_t kMaximumMessageBytes = 16 * 1024 * 1024;

std::string_view trim(std::string_view value) {
  while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front()))) {
    value.remove_prefix(1);
  }
  while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) {
    value.remove_suffix(1);
  }
  return value;
}

std::size_t parse_content_length(std::string_view headers) {
  std::optional<std::size_t> content_length;
  std::size_t line_start = 0;
  while (line_start < headers.size()) {
    const auto line_end = headers.find("\r\n", line_start);
    const auto line = headers.substr(line_start, line_end - line_start);
    const auto separator = line.find(':');
    if (separator != std::string_view::npos && line.substr(0, separator) == "Content-Length") {
      if (content_length.has_value()) {
        throw RunnerProtocolError("runner control frame has duplicate Content-Length headers");
      }

      const auto value = trim(line.substr(separator + 1));
      std::size_t parsed_length = 0;
      const auto [end, error] =
          std::from_chars(value.data(), value.data() + value.size(), parsed_length);
      if (error != std::errc{} || end != value.data() + value.size()) {
        throw RunnerProtocolError("runner control frame has an invalid Content-Length header");
      }
      if (parsed_length > kMaximumMessageBytes) {
        throw RunnerProtocolError("runner control frame exceeds the maximum message size");
      }
      content_length = parsed_length;
    }

    if (line_end == std::string_view::npos) {
      break;
    }
    line_start = line_end + 2;
  }

  if (!content_length.has_value()) {
    throw RunnerProtocolError("runner control frame is missing a Content-Length header");
  }
  return *content_length;
}

const nlohmann::json &required_member(const nlohmann::json &object, std::string_view name) {
  if (!object.is_object() || !object.contains(name)) {
    throw RunnerProtocolError("runner initialize message is missing '" + std::string(name) + "'");
  }
  return object.at(name);
}

void require_jsonrpc_2_0(const nlohmann::json &message) {
  const auto &version = required_member(message, "jsonrpc");
  if (!version.is_string() || version != "2.0") {
    throw RunnerProtocolError("runner initialize message must use JSON-RPC 2.0");
  }
}

void append_u32_be(std::vector<std::uint8_t> &bytes, std::uint32_t value) {
  for (int shift = 24; shift >= 0; shift -= 8) {
    bytes.push_back(static_cast<std::uint8_t>(value >> shift));
  }
}

void append_u64_be(std::vector<std::uint8_t> &bytes, std::uint64_t value) {
  for (int shift = 56; shift >= 0; shift -= 8) {
    bytes.push_back(static_cast<std::uint8_t>(value >> shift));
  }
}

std::uint32_t read_u32_be(const std::vector<std::uint8_t> &bytes, std::size_t offset) {
  return (static_cast<std::uint32_t>(bytes[offset]) << 24) |
         (static_cast<std::uint32_t>(bytes[offset + 1]) << 16) |
         (static_cast<std::uint32_t>(bytes[offset + 2]) << 8) |
         static_cast<std::uint32_t>(bytes[offset + 3]);
}

std::uint64_t read_u64_be(const std::vector<std::uint8_t> &bytes, std::size_t offset) {
  std::uint64_t value = 0;
  for (std::size_t index = 0; index < sizeof(value); ++index) {
    value = (value << 8) | bytes[offset + index];
  }
  return value;
}

}  // namespace

RunnerInitializeRequest parse_runner_initialize_request(const nlohmann::json &message) {
  require_jsonrpc_2_0(message);
  const auto &method = required_member(message, "method");
  if (!method.is_string() || method != "initialize") {
    throw RunnerProtocolError("runner control message is not an initialize request");
  }

  const auto &id = required_member(message, "id");
  if (id.is_null() || (!id.is_string() && !id.is_number_integer() && !id.is_number_unsigned())) {
    throw RunnerProtocolError("runner initialize request has an invalid id");
  }

  const auto &params = required_member(message, "params");
  const auto &protocol_version = required_member(params, "protocolVersion");
  if (!protocol_version.is_number_integer()) {
    throw RunnerProtocolError("runner initialize request has an invalid protocolVersion");
  }
  return {.id = id, .protocol_version = protocol_version.get<int>()};
}

nlohmann::json make_runner_initialize_request(nlohmann::json id) {
  return {{"jsonrpc", "2.0"},
          {"id", std::move(id)},
          {"method", "initialize"},
          {"params", {{"protocolVersion", kRunnerProtocolVersion}}}};
}

nlohmann::json make_runner_initialize_response(const RunnerInitializeRequest &request,
                                               std::vector<std::string> capabilities) {
  return {{"jsonrpc", "2.0"},
          {"id", request.id},
          {"result", {{"protocolVersion", request.protocol_version},
                      {"capabilities", std::move(capabilities)}}}};
}

RunnerInitializeResponse parse_runner_initialize_response(const nlohmann::json &message) {
  require_jsonrpc_2_0(message);
  const auto &id = required_member(message, "id");
  const auto &result = required_member(message, "result");
  const auto &protocol_version = required_member(result, "protocolVersion");
  const auto &capabilities = required_member(result, "capabilities");
  if (id.is_null() || (!id.is_string() && !id.is_number_integer() && !id.is_number_unsigned()) ||
      !protocol_version.is_number_integer() || !capabilities.is_array()) {
    throw RunnerProtocolError("runner initialize response has invalid fields");
  }

  std::vector<std::string> parsed_capabilities;
  parsed_capabilities.reserve(capabilities.size());
  for (const auto &capability : capabilities) {
    if (!capability.is_string()) {
      throw RunnerProtocolError("runner initialize response has a non-string capability");
    }
    parsed_capabilities.push_back(capability.get<std::string>());
  }
  return {.id = id,
          .protocol_version = protocol_version.get<int>(),
          .capabilities = std::move(parsed_capabilities)};
}

RunnerSynthesizeRequest parse_runner_synthesize_request(const nlohmann::json &message) {
  require_jsonrpc_2_0(message);
  const auto &method = required_member(message, "method");
  if (!method.is_string() || method != "synthesize") {
    throw RunnerProtocolError("runner control message is not a synthesize request");
  }

  const auto &id = required_member(message, "id");
  if (id.is_null() || (!id.is_string() && !id.is_number_integer() && !id.is_number_unsigned())) {
    throw RunnerProtocolError("runner synthesize request has an invalid id");
  }
  const auto &params = required_member(message, "params");
  const auto &text = required_member(params, "text");
  if (!text.is_string() || text.get_ref<const std::string &>().empty()) {
    throw RunnerProtocolError("runner synthesize request has an invalid text");
  }
  return {.id = id, .text = text.get<std::string>()};
}

nlohmann::json make_runner_synthesize_response(const RunnerSynthesizeRequest &request,
                                               std::uint32_t sample_rate_hz,
                                               std::uint32_t channels,
                                               std::string sample_format,
                                               std::uint64_t total_sample_frames) {
  return {{"jsonrpc", "2.0"},
          {"id", request.id},
          {"result", {{"sampleRateHz", sample_rate_hz},
                      {"channels", channels},
                      {"sampleFormat", std::move(sample_format)},
                      {"totalSampleFrames", total_sample_frames}}}};
}

RunnerSynthesizeResponse parse_runner_synthesize_response(const nlohmann::json &message) {
  require_jsonrpc_2_0(message);
  const auto &id = required_member(message, "id");
  const auto &result = required_member(message, "result");
  const auto &sample_rate_hz = required_member(result, "sampleRateHz");
  const auto &channels = required_member(result, "channels");
  const auto &sample_format = required_member(result, "sampleFormat");
  const auto &total_sample_frames = required_member(result, "totalSampleFrames");
  if (id.is_null() || (!id.is_string() && !id.is_number_integer() && !id.is_number_unsigned()) ||
      !sample_rate_hz.is_number_unsigned() || sample_rate_hz.get<std::uint32_t>() == 0 ||
      !channels.is_number_unsigned() || channels.get<std::uint32_t>() == 0 ||
      !sample_format.is_string() || sample_format != "pcm_s16le" ||
      !total_sample_frames.is_number_unsigned()) {
    throw RunnerProtocolError("runner synthesize response has invalid fields");
  }
  return {.id = id,
          .sample_rate_hz = sample_rate_hz.get<std::uint32_t>(),
          .channels = channels.get<std::uint32_t>(),
          .sample_format = sample_format.get<std::string>(),
          .total_sample_frames = total_sample_frames.get<std::uint64_t>()};
}

std::vector<std::uint8_t> frame_runner_audio_message(const RunnerAudioFrame &frame) {
  if (frame.payload.size() > kMaximumRunnerAudioFramePayloadBytes) {
    throw RunnerProtocolError("runner audio frame exceeds the maximum payload size");
  }

  std::vector<std::uint8_t> bytes;
  bytes.reserve(kRunnerAudioFrameHeaderBytes + frame.payload.size());
  append_u32_be(bytes, static_cast<std::uint32_t>(frame.payload.size()));
  append_u64_be(bytes, frame.sequence_number);
  append_u32_be(bytes, frame.sample_count);
  append_u32_be(bytes, frame.flags);
  bytes.insert(bytes.end(), frame.payload.begin(), frame.payload.end());
  return bytes;
}

std::vector<RunnerAudioFrame> RunnerAudioFrameParser::push(
    const std::vector<std::uint8_t> &bytes) {
  buffer_.insert(buffer_.end(), bytes.begin(), bytes.end());
  std::vector<RunnerAudioFrame> frames;

  while (buffer_.size() >= kRunnerAudioFrameHeaderBytes) {
    const auto payload_size = read_u32_be(buffer_, 0);
    if (payload_size > kMaximumRunnerAudioFramePayloadBytes) {
      throw RunnerProtocolError("runner audio frame exceeds the maximum payload size");
    }
    const auto frame_size = kRunnerAudioFrameHeaderBytes + payload_size;
    if (buffer_.size() < frame_size) {
      return frames;
    }

    RunnerAudioFrame frame{
        .sequence_number = read_u64_be(buffer_, 4),
        .sample_count = read_u32_be(buffer_, 12),
        .flags = read_u32_be(buffer_, 16),
        .payload = std::vector<std::uint8_t>(
            buffer_.begin() + static_cast<std::ptrdiff_t>(kRunnerAudioFrameHeaderBytes),
            buffer_.begin() + static_cast<std::ptrdiff_t>(frame_size)),
    };
    frames.push_back(std::move(frame));
    buffer_.erase(buffer_.begin(), buffer_.begin() + static_cast<std::ptrdiff_t>(frame_size));
  }
  return frames;
}

std::string frame_runner_control_message(const nlohmann::json &message) {
  const auto body = message.dump();
  return "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body;
}

std::vector<nlohmann::json> RunnerControlMessageParser::push(std::string_view bytes) {
  buffer_.append(bytes);
  std::vector<nlohmann::json> messages;

  while (true) {
    const auto header_end = buffer_.find("\r\n\r\n");
    if (header_end == std::string::npos) {
      if (buffer_.size() > kMaximumHeaderBytes) {
        throw RunnerProtocolError("runner control frame header exceeds the maximum size");
      }
      return messages;
    }
    if (header_end > kMaximumHeaderBytes) {
      throw RunnerProtocolError("runner control frame header exceeds the maximum size");
    }

    const auto body_length = parse_content_length(std::string_view(buffer_).substr(0, header_end));
    const auto frame_size = header_end + 4 + body_length;
    if (buffer_.size() < frame_size) {
      return messages;
    }

    try {
      messages.push_back(nlohmann::json::parse(buffer_.substr(header_end + 4, body_length)));
    } catch (const nlohmann::json::parse_error &error) {
      throw RunnerProtocolError("runner control frame contains invalid JSON: " +
                                std::string(error.what()));
    }
    buffer_.erase(0, frame_size);
  }
}

}  // namespace tts_host
