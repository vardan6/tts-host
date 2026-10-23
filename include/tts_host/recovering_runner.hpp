#pragma once

#include "tts_host/runner_launcher.hpp"
#include <memory>
#include <optional>

namespace tts_host {
struct SynthesizedChunk {
  RunnerSynthesizeResponse format;
  std::vector<std::uint8_t> pcm;
};

// Publishes only complete chunks. Recovery cannot duplicate audible PCM.
class RecoveringRunner {
 public:
  RecoveringRunner(std::filesystem::path executable,
                   std::optional<std::filesystem::path> model = {},
                   std::optional<std::filesystem::path> voice = {},
                   RunnerDeadlines deadlines = {}, std::function<bool()> cancelled = {},
                   std::function<void(const std::string &)> log = {});
  // Starts and loads the runner if it is not running, restarting once after
  // a RunnerLaunchError (launch, transport, or deadline). A restart spent
  // here leaves the next synthesize() none for its chunk.
  void prepare();
  void unload();
  bool loaded() const { return session_ != nullptr; }
  void set_state_callback(std::function<void(bool)> callback) { state_callback_ = std::move(callback); }
  // Called with the runner's reason when it rejects `load`; the runner has
  // been shut down and RunnerLoadRejected follows, without restart or retry.
  void set_load_rejected_callback(std::function<void(const std::string &)> callback) {
    load_rejected_callback_ = std::move(callback);
  }
  void set_cancelled(std::function<bool()> cancelled) { cancelled_ = std::move(cancelled); }
  SynthesizedChunk synthesize(const std::string &text, double speed);
  RunnerStatsResponse stats();
  int finish();
 private:
  void start();
  void drop_session();
  // Drops the session, then throws if cancelled or `retried`; otherwise logs
  // the restart of `what`.
  void recover_or_throw(const std::exception &error, bool retried, const std::string &what);
  std::filesystem::path executable_;
  std::optional<std::filesystem::path> model_, voice_;
  RunnerDeadlines deadlines_;
  std::function<bool()> cancelled_;
  std::function<void(const std::string &)> log_;
  std::function<void(bool)> state_callback_;
  std::function<void(const std::string &)> load_rejected_callback_;
  std::unique_ptr<RunnerSession> session_;
  int request_id_ = 3;
  bool restart_spent_ = false;
};
}  // namespace tts_host
