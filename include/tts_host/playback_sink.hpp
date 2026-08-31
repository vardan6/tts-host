#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace tts_host {

// Config value meaning "follow the operating system's default output device"
// rather than pinning to a specific one (docs/requirements/product.md).
inline constexpr const char *kSystemDefaultOutputDevice = "system-default";

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
                    const std::string &device_name = kSystemDefaultOutputDevice) = 0;
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
            const std::string &device_name = kSystemDefaultOutputDevice) override;
};

// Lists the friendly names of active audio render endpoints, for populating
// the settings window's output-device control
// (docs/design/architecture.md#desktop-integration). Does not include
// kSystemDefaultOutputDevice -- callers add that themselves as the "follow
// the OS default" option. Windows only in this slice; other platforms throw.
std::vector<std::string> list_output_devices();

}  // namespace tts_host
