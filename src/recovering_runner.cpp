#include "tts_host/recovering_runner.hpp"
#include "tts_host/path_encoding.hpp"
#include "tts_host/activity_log.hpp"
#include <iostream>
#include <utility>

namespace tts_host {
RecoveringRunner::RecoveringRunner(std::filesystem::path executable,
    std::optional<std::filesystem::path> model, std::optional<std::filesystem::path> voice,
    RunnerDeadlines deadlines, std::function<bool()> cancelled,
    std::function<void(const std::string &)> log)
    : executable_(std::move(executable)), model_(std::move(model)), voice_(std::move(voice)),
      deadlines_(deadlines), cancelled_(std::move(cancelled)), log_(std::move(log)) {}

void RecoveringRunner::start() {
  session_ = std::make_unique<RunnerSession>(executable_, deadlines_,
      [this] { return cancelled_ && cancelled_(); });
  const auto result = parse_runner_initialize_response(
      session_->send_request(make_runner_initialize_request(1)));
  if (result.protocol_version != kRunnerProtocolVersion)
    throw RunnerLaunchError("runner reported an unsupported protocol version");
  if (model_) {
    try {
      parse_runner_load_response(session_->send_request(make_runner_load_request(
          2, path_to_utf8(*model_), voice_ ? std::optional<std::string>(path_to_utf8(*voice_)) : std::nullopt)));
    } catch (const RunnerLoadRejected &error) {
      // A restart would repeat the failure: shut the unloaded runner down and
      // report the runner's reason (docs/design/architecture.md#load-rejection).
      session_->finish();
      session_.reset();
      if (load_rejected_callback_) load_rejected_callback_(error.what());
      throw;
    }
  }
  if (state_callback_) state_callback_(true);
}

void RecoveringRunner::drop_session() {
  session_.reset();
  if (state_callback_) state_callback_(false);
}

void RecoveringRunner::recover_or_throw(const RunnerLaunchError &error, bool retried, const std::string &what) {
  drop_session();
  if (cancelled_ && cancelled_()) throw RunnerCancelled();
  if (retried) throw RunnerLaunchError(
      std::string("runner failed again after one restart: ") + error.what() +
      "; check the model/runner installation and available memory, or choose another model");
  const auto message = "Restarting runner and retrying " + what + " once: " + error.what();
  record_activity(message);
  if (log_) log_(message); else std::cerr << message << '\n';
}

// A start that needed its restart spends the first chunk's: one restart per
// unplayed chunk (ADR 0002). A rejected load is never retried.
void RecoveringRunner::prepare() {
  for (bool retried = false;; retried = true) {
    try {
      if (!session_) start();
      return;
    } catch (const RunnerCancelled &) {
      drop_session();
      throw;
    } catch (const RunnerLaunchError &error) {
      recover_or_throw(error, retried, "its start");
      restart_spent_ = true;
    } catch (...) {
      drop_session();
      throw;
    }
  }
}

void RecoveringRunner::unload() {
  if (!session_) return;
  try {
    parse_runner_unload_response(session_->send_request(
        make_runner_unload_request(request_id_++), deadlines_.shutdown));
  } catch (const std::exception &) {
    // Process termination frees the model even if polite unload fails.
  }
  session_->finish();
  session_.reset();
  restart_spent_ = false;
  if (state_callback_) state_callback_(false);
}

SynthesizedChunk RecoveringRunner::synthesize(const std::string &text, double speed) {
  for (int attempt = std::exchange(restart_spent_, false) ? 1 : 0; attempt != 2; ++attempt) {
    try {
      if (cancelled_ && cancelled_()) throw RunnerCancelled();
      if (!session_) start();
      const auto response = session_->send_request(
          make_runner_synthesize_request(request_id_++, text, speed));
      // Consume the stream first: a rejection (RunnerRequestError) leaves the
      // session aligned and loaded, without a restart or retry.
      const auto frames = session_->read_audio_stream_until_end();
      SynthesizedChunk chunk;
      chunk.format = parse_runner_synthesize_response(response);
      for (const auto &frame : frames)
        chunk.pcm.insert(chunk.pcm.end(), frame.payload.begin(), frame.payload.end());
      return chunk;
    } catch (const RunnerCancelled &) {
      drop_session();
      throw;
    } catch (const RunnerLaunchError &error) {
      recover_or_throw(error, attempt != 0, "unplayed chunk");
    }
  }
  throw RunnerLaunchError("runner recovery exhausted");
}

RunnerStatsResponse RecoveringRunner::stats() {
  return parse_runner_stats_response(session_->send_request(make_runner_stats_request(request_id_++)));
}
int RecoveringRunner::finish() { return session_ ? session_->finish() : 0; }
}  // namespace tts_host
