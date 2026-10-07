#include "tts_host/playback_sink.hpp"

#include "tts_host/audio_conversion.hpp"

#include <stdexcept>
#include <utility>

#ifdef _WIN32
#include <windows.h>

#include <mmdeviceapi.h>

#include <audioclient.h>
#include <functiondiscoverykeys_devpkey.h>
#include <ksmedia.h>
#include <propvarutil.h>
#include <wrl/client.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <string>
#endif

namespace tts_host {

bool PlaybackSink::play_from(std::uint32_t sample_rate_hz, std::uint16_t channels,
                             const std::vector<std::uint8_t> &pcm_s16le,
                             const std::string &device_name, std::uint64_t start_source_frame,
                             double source_origin_seconds, PlaybackControl *control,
                             std::uint64_t) {
  if (sample_rate_hz == 0) throw std::invalid_argument("source sample rate must be positive");
  const auto source_bytes_per_frame = static_cast<std::uint64_t>(channels) * 2;
  const auto start_byte = start_source_frame * source_bytes_per_frame;
  if (source_bytes_per_frame == 0 || start_byte > pcm_s16le.size()) return false;
  if (control != nullptr) {
    control->update_position(source_origin_seconds +
                             static_cast<double>(start_source_frame) / sample_rate_hz);
  }
  const std::vector<std::uint8_t> remainder(pcm_s16le.begin() + static_cast<std::ptrdiff_t>(start_byte),
                                            pcm_s16le.end());
  play(sample_rate_hz, channels, remainder, device_name, control);
  return false;
}

namespace {
// Adapts endpoints without a persistent client: each sentence drains itself.
class PerCallPlaybackStream final : public PlaybackStream {
 public:
  PerCallPlaybackStream(PlaybackSink &sink, std::string device_name)
      : sink_(sink), device_name_(std::move(device_name)) {}
  bool play_from(std::uint32_t sample_rate_hz, std::uint16_t channels,
                 const std::vector<std::uint8_t> &pcm_s16le, std::uint64_t start_source_frame,
                 double source_origin_seconds, PlaybackControl *control,
                 std::uint64_t seek_generation) override {
    return sink_.play_from(sample_rate_hz, channels, pcm_s16le, device_name_, start_source_frame,
                           source_origin_seconds, control, seek_generation);
  }
  bool drain(PlaybackControl *, std::uint64_t) override { return false; }

 private:
  PlaybackSink &sink_;
  std::string device_name_;
};
}  // namespace

std::unique_ptr<PlaybackStream> PlaybackSink::open_stream(const std::string &device_name,
                                                          std::chrono::milliseconds) {
  return std::make_unique<PerCallPlaybackStream>(*this, device_name);
}

#ifdef _WIN32
namespace {

void throw_if_failed(HRESULT result, const char *what) {
  if (FAILED(result)) {
    throw std::runtime_error(std::string("WASAPI ") + what + " failed (hresult " +
                             std::to_string(result) + ")");
  }
}

std::string wide_to_utf8(const wchar_t *wide_text) {
  const int required = WideCharToMultiByte(CP_UTF8, 0, wide_text, -1, nullptr, 0, nullptr, nullptr);
  std::string utf8_text;
  if (required > 1) {
    utf8_text.resize(static_cast<std::size_t>(required) - 1);
    WideCharToMultiByte(CP_UTF8, 0, wide_text, -1, utf8_text.data(), required, nullptr, nullptr);
  }
  return utf8_text;
}

using Microsoft::WRL::ComPtr;

std::string device_friendly_name(IMMDevice &device) {
  ComPtr<IPropertyStore> properties;
  throw_if_failed(device.OpenPropertyStore(STGM_READ, &properties),
                  "IMMDevice::OpenPropertyStore");
  PROPVARIANT friendly_name;
  PropVariantInit(&friendly_name);
  throw_if_failed(properties->GetValue(PKEY_Device_FriendlyName, &friendly_name),
                  "IPropertyStore::GetValue(PKEY_Device_FriendlyName)");
  const std::string name =
      friendly_name.vt == VT_LPWSTR ? wide_to_utf8(friendly_name.pwszVal) : std::string();
  PropVariantClear(&friendly_name);
  return name;
}

ComPtr<IMMDeviceEnumerator> create_device_enumerator() {
  ComPtr<IMMDeviceEnumerator> enumerator;
  throw_if_failed(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                   IID_PPV_ARGS(&enumerator)),
                  "CoCreateInstance(MMDeviceEnumerator)");
  return enumerator;
}

struct ComGuard {
  bool owns;
  ~ComGuard() {
    if (owns) {
      CoUninitialize();
    }
  }
};

ComGuard initialize_com() {
  const HRESULT com_result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  const bool owns_com = SUCCEEDED(com_result);
  if (!owns_com && com_result != RPC_E_CHANGED_MODE) {
    throw_if_failed(com_result, "CoInitializeEx");
  }
  return ComGuard{owns_com};
}

// Resolves device_name to a render endpoint: the OS default when it is
// kSystemDefaultOutputDevice, otherwise the endpoint whose friendly name
// matches exactly (docs/requirements/product.md's device pinning).
ComPtr<IMMDevice> resolve_output_device(IMMDeviceEnumerator &enumerator,
                                        const std::string &device_name) {
  ComPtr<IMMDevice> device;
  if (device_name == kSystemDefaultOutputDevice) {
    throw_if_failed(enumerator.GetDefaultAudioEndpoint(eRender, eConsole, &device),
                    "GetDefaultAudioEndpoint");
    return device;
  }

  ComPtr<IMMDeviceCollection> endpoints;
  throw_if_failed(
      enumerator.EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &endpoints),
      "EnumAudioEndpoints");
  UINT endpoint_count = 0;
  throw_if_failed(endpoints->GetCount(&endpoint_count), "IMMDeviceCollection::GetCount");

  for (UINT index = 0; index < endpoint_count; ++index) {
    ComPtr<IMMDevice> candidate;
    throw_if_failed(endpoints->Item(index, &candidate), "IMMDeviceCollection::Item");

    if (device_friendly_name(*candidate.Get()) == device_name) {
      return candidate;
    }
  }

  throw std::runtime_error("no audio output device named \"" + device_name +
                           "\" found (check audio.outputDevice in config.json)");
}

AudioFormat audio_format_from_wave_format(const WAVEFORMATEX &format) {
  const WAVEFORMATEXTENSIBLE *extensible =
      format.wFormatTag == WAVE_FORMAT_EXTENSIBLE && format.cbSize >= 22
          ? reinterpret_cast<const WAVEFORMATEXTENSIBLE *>(&format)
          : nullptr;
  const bool is_float = format.wFormatTag == WAVE_FORMAT_IEEE_FLOAT ||
                        (extensible != nullptr &&
                         IsEqualGUID(extensible->SubFormat, KSDATAFORMAT_SUBTYPE_IEEE_FLOAT));
  const bool is_pcm = format.wFormatTag == WAVE_FORMAT_PCM ||
                      (extensible != nullptr &&
                       IsEqualGUID(extensible->SubFormat, KSDATAFORMAT_SUBTYPE_PCM));
  if (!is_float && !is_pcm) {
    throw std::runtime_error("WASAPI endpoint returned an unsupported sample encoding");
  }
  return {.sample_rate_hz = format.nSamplesPerSec,
          .channels = format.nChannels,
          .bits_per_sample = format.wBitsPerSample,
          .encoding = is_float ? AudioSampleEncoding::ieee_float
                               : AudioSampleEncoding::signed_integer};
}

struct CoTaskMemFormat {
  WAVEFORMATEX *value = nullptr;
  ~CoTaskMemFormat() { CoTaskMemFree(value); }
};

// Thrown when the endpoint disappears mid-stream, so the stream can reopen it.
struct EndpointInvalidated : std::runtime_error {
  EndpointInvalidated() : std::runtime_error("WASAPI endpoint invalidated") {}
};

void check_stream(HRESULT result, const char *what) {
  if (result == AUDCLNT_E_DEVICE_INVALIDATED) throw EndpointInvalidated();
  throw_if_failed(result, what);
}

std::wstring endpoint_id(IMMDevice &device) {
  LPWSTR id = nullptr;
  throw_if_failed(device.GetId(&id), "IMMDevice::GetId");
  std::wstring value(id);
  CoTaskMemFree(id);
  return value;
}

// One event-driven shared-mode client per utterance
// (docs/design/architecture.md#utterance-audio-stream). The client is opened
// on the first audio and kept running between sentences: when the next
// sentence is late the endpoint renders silence instead of being reopened.
// Pause stops it; a seek flushes it; a changed default endpoint or source
// format drains and reopens it at the next sentence.
class WasapiPlaybackStream final : public PlaybackStream {
 public:
  WasapiPlaybackStream(std::string device_name, std::chrono::milliseconds buffer)
      : device_name_(std::move(device_name)), buffer_(buffer) {}

  ~WasapiPlaybackStream() override {
    close();
    if (event_ != nullptr) CloseHandle(event_);
    if (mta_held_) CoDecrementMTAUsage(mta_cookie_);
  }

  bool play_from(std::uint32_t sample_rate_hz, std::uint16_t channels,
                 const std::vector<std::uint8_t> &pcm_s16le, std::uint64_t start_source_frame,
                 double source_origin_seconds, PlaybackControl *control,
                 std::uint64_t seek_generation) override {
    if (sample_rate_hz == 0) throw std::invalid_argument("source sample rate must be positive");
    if (pcm_s16le.empty()) return false;
    const ComGuard com_guard = initialize_com();
    if (client_ && seek_generation != generation_) flush();
    generation_ = seek_generation;
    if (client_ && (sample_rate_hz != source_rate_ || channels != source_channels_ ||
                    default_endpoint_changed())) {
      if (drain(control, seek_generation)) return true;
      if (control != nullptr && control->cancelled()) return false;
      close();
    }
    if (!client_) open(sample_rate_hz, channels);

    const std::vector<std::uint8_t> converted_pcm =
        needs_conversion_ ? convert_pcm_s16le(pcm_s16le, sample_rate_hz, channels, endpoint_format_)
                          : std::vector<std::uint8_t>();
    const std::vector<std::uint8_t> &playback_pcm = needs_conversion_ ? converted_pcm : pcm_s16le;
    const std::size_t total_frames = playback_pcm.size() / bytes_per_frame_;
    std::size_t next_frame = static_cast<std::size_t>(std::min<std::uint64_t>(
        total_frames,
        convert_frame_position(start_source_frame, sample_rate_hz, endpoint_format_.sample_rate_hz)));
    segment_origin_seconds_ = source_origin_seconds;
    segment_base_frame_ = next_frame;
    segment_queue_start_ = queued_;
    if (control != nullptr) {
      control->update_position(source_origin_seconds +
                               static_cast<double>(start_source_frame) / sample_rate_hz);
    }

    try {
      while (true) {
        if (!observe_controls(control, seek_generation)) return interrupted(control, seek_generation);
        UINT32 padding_frames = 0;
        check_stream(client_->GetCurrentPadding(&padding_frames), "IAudioClient::GetCurrentPadding");
        const auto frames_to_write = static_cast<UINT32>(
            std::min<std::size_t>(buffer_frames_ - padding_frames, total_frames - next_frame));
        if (frames_to_write > 0) {
          BYTE *buffer = nullptr;
          check_stream(render_->GetBuffer(frames_to_write, &buffer), "IAudioRenderClient::GetBuffer");
          std::memcpy(buffer, playback_pcm.data() + next_frame * bytes_per_frame_,
                      frames_to_write * bytes_per_frame_);
          check_stream(render_->ReleaseBuffer(frames_to_write, 0), "IAudioRenderClient::ReleaseBuffer");
          next_frame += frames_to_write;
          queued_ += frames_to_write;
        }
        start();
        if (next_frame >= total_frames) return false;
        wait_for_period(control);
      }
    } catch (const EndpointInvalidated &) {
      // Continue the unqueued remainder on whatever endpoint now resolves.
      const auto resume_frame = convert_frame_position(next_frame, endpoint_format_.sample_rate_hz,
                                                       sample_rate_hz);
      close();
      return play_from(sample_rate_hz, channels, pcm_s16le, resume_frame, source_origin_seconds,
                       control, seek_generation);
    }
  }

  bool drain(PlaybackControl *control, std::uint64_t seek_generation) override {
    if (!client_) return false;
    const ComGuard com_guard = initialize_com();
    try {
      while (true) {
        if (!observe_controls(control, seek_generation)) return interrupted(control, seek_generation);
        start();
        UINT32 padding_frames = 0;
        check_stream(client_->GetCurrentPadding(&padding_frames), "IAudioClient::GetCurrentPadding");
        if (padding_frames == 0) break;
        wait_for_period(control);
      }
    } catch (const EndpointInvalidated &) {
      close();
      return false;
    }
    stop();
    publish_position(control);
    return false;
  }

 private:
  void open(std::uint32_t sample_rate_hz, std::uint16_t channels) {
    if (!mta_held_) {
      // Playback calls arrive on short-lived threads; keep the MTA, and so
      // this client, alive between them.
      throw_if_failed(CoIncrementMTAUsage(&mta_cookie_), "CoIncrementMTAUsage");
      mta_held_ = true;
    }
    ComPtr<IMMDeviceEnumerator> enumerator = create_device_enumerator();
    ComPtr<IMMDevice> device = resolve_output_device(*enumerator.Get(), device_name_);
    endpoint_id_ = endpoint_id(*device.Get());
    throw_if_failed(device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, &client_),
                    "IMMDevice::Activate");

    WAVEFORMATEX format{};
    format.wFormatTag = WAVE_FORMAT_PCM;
    format.nChannels = channels;
    format.nSamplesPerSec = sample_rate_hz;
    format.wBitsPerSample = 16;
    format.nBlockAlign = static_cast<WORD>(channels * (format.wBitsPerSample / 8));
    format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;

    // Shared-mode endpoints need not accept runner PCM directly (Kokoro emits
    // 24 kHz mono S16LE whereas typical endpoints mix at 48 kHz float stereo).
    // Ask WASAPI for its closest supported format and convert before queuing.
    CoTaskMemFormat closest_format;
    const HRESULT support_result =
        client_->IsFormatSupported(AUDCLNT_SHAREMODE_SHARED, &format, &closest_format.value);
    WAVEFORMATEX *endpoint_format = &format;
    if (support_result == S_FALSE && closest_format.value != nullptr) {
      endpoint_format = closest_format.value;
    } else if (support_result == S_FALSE) {
      throw std::runtime_error(
          "WASAPI IAudioClient::IsFormatSupported returned no usable shared-mode format");
    } else if (support_result != S_OK) {
      throw_if_failed(support_result, "IAudioClient::IsFormatSupported");
    }
    needs_conversion_ = endpoint_format->nSamplesPerSec != sample_rate_hz ||
                        endpoint_format->nChannels != channels ||
                        endpoint_format->wBitsPerSample != 16 ||
                        endpoint_format->wFormatTag != WAVE_FORMAT_PCM;
    endpoint_format_ = audio_format_from_wave_format(*endpoint_format);
    bytes_per_frame_ = endpoint_format->nBlockAlign;

    // Buffer duration is in 100-ns units.
    const REFERENCE_TIME buffer_duration = buffer_.count() * 10'000;
    throw_if_failed(client_->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
                                        buffer_duration, 0, endpoint_format, nullptr),
                    "IAudioClient::Initialize");
    if (event_ == nullptr) {
      event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
      if (event_ == nullptr) throw std::runtime_error("CreateEvent for WASAPI refill failed");
    }
    throw_if_failed(client_->SetEventHandle(event_), "IAudioClient::SetEventHandle");
    throw_if_failed(client_->GetBufferSize(&buffer_frames_), "IAudioClient::GetBufferSize");
    throw_if_failed(client_->GetService(IID_PPV_ARGS(&render_)),
                    "IAudioClient::GetService(IAudioRenderClient)");
    source_rate_ = sample_rate_hz;
    source_channels_ = channels;
    queued_ = 0;
    segment_queue_start_ = 0;
    started_ = false;
  }

  void close() {
    if (client_ && started_) client_->Stop();
    started_ = false;
    render_.Reset();
    client_.Reset();
  }

  void start() {
    if (started_) return;
    check_stream(client_->Start(), "IAudioClient::Start");
    started_ = true;
  }

  void stop() {
    if (!started_) return;
    check_stream(client_->Stop(), "IAudioClient::Stop");
    started_ = false;
  }

  // Discards buffered audio for a seek; the next call restarts the client.
  void flush() {
    if (started_) client_->Stop();
    started_ = false;
    client_->Reset();
    queued_ = 0;
    segment_queue_start_ = 0;
  }

  // Only a followed system default can move between sentences; a pinned
  // device is reopened only if it is invalidated.
  bool default_endpoint_changed() const {
    if (device_name_ != kSystemDefaultOutputDevice) return false;
    try {
      ComPtr<IMMDevice> device;
      if (FAILED(create_device_enumerator()->GetDefaultAudioEndpoint(eRender, eConsole, &device)))
        return false;
      return endpoint_id(*device.Get()) != endpoint_id_;
    } catch (const std::exception &) {
      return false;
    }
  }

  // Stops on Stop, flushes on seek, and holds the client stopped while
  // paused. False means the caller must return.
  bool observe_controls(PlaybackControl *control, std::uint64_t seek_generation) {
    if (control == nullptr) return true;
    if (control->cancelled()) {
      stop();
      return false;
    }
    if (control->seek_generation() != seek_generation) {
      flush();
      return false;
    }
    if (!control->paused()) return true;
    stop();
    if (control->wait_until_resumed_or_seek(seek_generation)) return true;
    if (!control->cancelled()) flush();
    return false;
  }

  static bool interrupted(const PlaybackControl *control, std::uint64_t seek_generation) {
    return control != nullptr && !control->cancelled() &&
           control->seek_generation() != seek_generation;
  }

  void wait_for_period(PlaybackControl *control) {
    WaitForSingleObject(event_, static_cast<DWORD>(buffer_.count()));
    publish_position(control);
  }

  // Position counts frames the endpoint has consumed from this segment; the
  // previous sentence's buffered tail leaves it at the segment start.
  void publish_position(PlaybackControl *control) {
    UINT32 padding_frames = 0;
    if (control == nullptr || FAILED(client_->GetCurrentPadding(&padding_frames))) return;
    const std::uint64_t played = queued_ >= padding_frames ? queued_ - padding_frames : 0;
    if (played < segment_queue_start_) return;
    control->update_position(
        segment_origin_seconds_ +
        static_cast<double>(segment_base_frame_ + (played - segment_queue_start_)) /
            endpoint_format_.sample_rate_hz);
  }

  std::string device_name_;
  std::chrono::milliseconds buffer_;
  bool mta_held_ = false;
  CO_MTA_USAGE_COOKIE mta_cookie_{};
  HANDLE event_ = nullptr;
  ComPtr<IAudioClient> client_;
  ComPtr<IAudioRenderClient> render_;
  std::wstring endpoint_id_;
  std::uint32_t source_rate_ = 0;
  std::uint16_t source_channels_ = 0;
  AudioFormat endpoint_format_{};
  bool needs_conversion_ = false;
  std::size_t bytes_per_frame_ = 0;
  UINT32 buffer_frames_ = 0;
  bool started_ = false;
  std::uint64_t generation_ = 0;
  std::uint64_t queued_ = 0;               // endpoint frames queued since open or flush
  std::uint64_t segment_queue_start_ = 0;  // queued_ when the current segment began
  std::size_t segment_base_frame_ = 0;     // segment frame its first queued frame came from
  double segment_origin_seconds_ = 0.0;
};

}  // namespace

std::unique_ptr<PlaybackStream> SystemPlaybackSink::open_stream(const std::string &device_name,
                                                                std::chrono::milliseconds buffer) {
  return std::make_unique<WasapiPlaybackStream>(device_name, buffer);
}

void SystemPlaybackSink::play(std::uint32_t sample_rate_hz, std::uint16_t channels,
                              const std::vector<std::uint8_t> &pcm_s16le,
                              const std::string &device_name,
                              PlaybackControl *control) {
  play_from(sample_rate_hz, channels, pcm_s16le, device_name, 0, 0.0, control,
            control == nullptr ? 0 : control->seek_generation());
}

bool SystemPlaybackSink::play_from(std::uint32_t sample_rate_hz, std::uint16_t channels,
                                   const std::vector<std::uint8_t> &pcm_s16le,
                                   const std::string &device_name,
                                   std::uint64_t start_source_frame,
                                   double source_origin_seconds, PlaybackControl *control,
                                   std::uint64_t seek_generation) {
  const auto stream = open_stream(device_name, kDefaultPlaybackBuffer);
  return stream->play_from(sample_rate_hz, channels, pcm_s16le, start_source_frame,
                           source_origin_seconds, control, seek_generation) ||
         stream->drain(control, seek_generation);
}

std::vector<std::string> list_output_devices() {
  const ComGuard com_guard = initialize_com();

  ComPtr<IMMDeviceEnumerator> enumerator = create_device_enumerator();

  ComPtr<IMMDeviceCollection> endpoints;
  throw_if_failed(
      enumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &endpoints),
      "EnumAudioEndpoints");
  UINT endpoint_count = 0;
  throw_if_failed(endpoints->GetCount(&endpoint_count), "IMMDeviceCollection::GetCount");

  std::vector<std::string> names;
  names.reserve(endpoint_count);
  for (UINT index = 0; index < endpoint_count; ++index) {
    ComPtr<IMMDevice> candidate;
    throw_if_failed(endpoints->Item(index, &candidate), "IMMDeviceCollection::Item");
    names.push_back(device_friendly_name(*candidate.Get()));
  }
  return names;
}

#else

void SystemPlaybackSink::play(std::uint32_t, std::uint16_t, const std::vector<std::uint8_t> &,
                              const std::string &device_name, PlaybackControl *) {
  throw std::runtime_error(
      "audio playback is not implemented on this platform yet (Windows only, see "
      "docs/design/architecture.md#speech-pipeline); requested output device: \"" +
      device_name + "\"");
}

bool SystemPlaybackSink::play_from(std::uint32_t, std::uint16_t,
                                   const std::vector<std::uint8_t> &, const std::string &device_name,
                                   std::uint64_t, double, PlaybackControl *, std::uint64_t) {
  throw std::runtime_error(
      "audio playback is not implemented on this platform yet (Windows only, see "
      "docs/design/architecture.md#speech-pipeline); requested output device: \"" +
      device_name + "\"");
}

std::unique_ptr<PlaybackStream> SystemPlaybackSink::open_stream(const std::string &device_name,
                                                                std::chrono::milliseconds buffer) {
  return PlaybackSink::open_stream(device_name, buffer);
}

std::vector<std::string> list_output_devices() {
  throw std::runtime_error(
      "audio output device enumeration is not implemented on this platform yet (Windows only, "
      "see docs/design/architecture.md#desktop-integration)");
}

#endif

}  // namespace tts_host
