#pragma once

#include "tts_host/config_loader.hpp"
#include "tts_host/playback_sink.hpp"
#include "tts_host/runner_launcher.hpp"
#include "tts_host/runner_selection.hpp"
#include <optional>

namespace tts_host {
class RecoveringRunner;

// The portable synthesis/playback coordinator. The desktop uses the system
// sink; tests supply an endpoint without accessing audio hardware.
void run_synthesis_pipeline(const std::string &spoken_text, const CliOptions &options,
                            const ConfigDocument &document, const RunnerSelection &selection,
                            PlaybackControl *control = nullptr,
                            PlaybackController *playback_controller = nullptr,
                            PlaybackSink *sink = nullptr, RunnerDeadlines deadlines = {},
                            RecoveringRunner *resident = nullptr);
}  // namespace tts_host
