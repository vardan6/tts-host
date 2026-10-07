#pragma once

#include <chrono>
#include <cstdint>
#include "tts_host/playback_controller.hpp"
#include <memory>
#include <string>
#include <vector>

namespace tts_host {

// Config value meaning "follow the operating system's default output device"
// rather than pinning to a specific one (docs/requirements/product.md).
inline constexpr const char *kSystemDefaultOutputDevice = "system-default";

// Requested endpoint buffer for an utterance stream; the endpoint may round it
// up (docs/design/architecture.md#utterance-audio-stream).
inline constexpr std::chrono::milliseconds kDefaultPlaybackBuffer{50};

// One endpoint client for one utterance
// (docs/design/architecture.md#utterance-audio-stream). Every sentence is fed
// into the same running client; only drain() waits for the endpoint buffer to
// empty. Calls are never concurrent: the pipeline joins one before the next.
class PlaybackStream {
 public:
  virtual ~PlaybackStream() = default;
  // Queues pcm_s16le from start_source_frame and returns once its last frame
  // is queued, not played. A seek generation other than the previous call's
  // flushes stale buffered audio first. Returns true when a seek interrupted
  // it; cancellation returns false.
  virtual bool play_from(std::uint32_t sample_rate_hz, std::uint16_t channels,
                         const std::vector<std::uint8_t> &pcm_s16le,
                         std::uint64_t start_source_frame, double source_origin_seconds,
                         PlaybackControl *control, std::uint64_t seek_generation) = 0;
  // Waits until queued audio has played: once, at the end of the utterance.
  // Returns true when a seek interrupted it.
  virtual bool drain(PlaybackControl *control, std::uint64_t seek_generation) = 0;
};

// Plays interleaved pcm_s16le audio and blocks until playback finishes.
// device_name is the config's audio.outputDevice value: kSystemDefaultOutputDevice
// plays through the OS default device; any other value pins playback to the
// endpoint whose friendly name matches, throwing if none does. Implemented by
// SystemPlaybackSink; tests substitute a fake to exercise the calling code
// without touching hardware.
class PlaybackSink {
 public:
  virtual ~PlaybackSink() = default;
  virtual void play(std::uint32_t sample_rate_hz, std::uint16_t channels,
                    const std::vector<std::uint8_t> &pcm_s16le,
                    const std::string &device_name = kSystemDefaultOutputDevice,
                    PlaybackControl *control = nullptr) = 0;
  virtual bool play_from(std::uint32_t sample_rate_hz, std::uint16_t channels,
                         const std::vector<std::uint8_t> &pcm_s16le,
                         const std::string &device_name, std::uint64_t start_source_frame,
                         double source_origin_seconds, PlaybackControl *control,
                         std::uint64_t seek_generation);
  // The default stream forwards each call to play_from, which drains per
  // call; endpoints that can keep a client open override it.
  virtual std::unique_ptr<PlaybackStream> open_stream(const std::string &device_name,
                                                      std::chrono::milliseconds buffer);
};

// Plays through the operating system's default audio output device, or a
// device pinned by friendly name in configuration
// (docs/design/architecture.md#speech-pipeline,
// docs/requirements/product.md). Windows only in this slice (WASAPI shared
// mode); other platforms throw, per "Platform-specific code" in the design
// doc -- Windows is the only implemented backend in the first release.
class SystemPlaybackSink final : public PlaybackSink {
 public:
  void play(std::uint32_t sample_rate_hz, std::uint16_t channels,
            const std::vector<std::uint8_t> &pcm_s16le,
            const std::string &device_name = kSystemDefaultOutputDevice,
            PlaybackControl *control = nullptr) override;
  bool play_from(std::uint32_t sample_rate_hz, std::uint16_t channels,
                 const std::vector<std::uint8_t> &pcm_s16le, const std::string &device_name,
                 std::uint64_t start_source_frame, double source_origin_seconds,
                 PlaybackControl *control, std::uint64_t seek_generation) override;
  // Event-driven WASAPI shared-mode client opened lazily on the first audio.
  std::unique_ptr<PlaybackStream> open_stream(const std::string &device_name,
                                              std::chrono::milliseconds buffer) override;
};

// Lists the friendly names of active audio render endpoints, for populating
// the settings window's output-device control
// (docs/design/architecture.md#desktop-integration). Does not include
// kSystemDefaultOutputDevice -- callers add that themselves as the "follow
// the OS default" option. Windows only in this slice; other platforms throw.
std::vector<std::string> list_output_devices();

}  // namespace tts_host
