#include "tts_host/playback_sink.hpp"

#include "tts_host/audio_conversion.hpp"

#include <stdexcept>

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
#include <thread>
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

}  // namespace

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
  if (pcm_s16le.empty()) {
    return false;
  }

  const ComGuard com_guard = initialize_com();

  ComPtr<IMMDeviceEnumerator> enumerator = create_device_enumerator();

  ComPtr<IMMDevice> device = resolve_output_device(*enumerator.Get(), device_name);

  ComPtr<IAudioClient> audio_client;
  throw_if_failed(device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, &audio_client),
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
  const HRESULT support_result = audio_client->IsFormatSupported(
      AUDCLNT_SHAREMODE_SHARED, &format, &closest_format.value);
  WAVEFORMATEX *endpoint_format = &format;
  if (support_result == S_FALSE && closest_format.value != nullptr) {
    endpoint_format = closest_format.value;
  } else if (support_result == S_FALSE) {
    throw std::runtime_error(
        "WASAPI IAudioClient::IsFormatSupported returned no usable shared-mode format");
  } else if (support_result != S_OK) {
    throw_if_failed(support_result, "IAudioClient::IsFormatSupported");
  }

  const bool needs_conversion = endpoint_format->nSamplesPerSec != sample_rate_hz ||
                                endpoint_format->nChannels != channels ||
                                endpoint_format->wBitsPerSample != 16 ||
                                endpoint_format->wFormatTag != WAVE_FORMAT_PCM;
  const std::vector<std::uint8_t> converted_pcm =
      needs_conversion ? convert_pcm_s16le(pcm_s16le, sample_rate_hz, channels,
                                           audio_format_from_wave_format(*endpoint_format))
                       : std::vector<std::uint8_t>();
  const std::vector<std::uint8_t> &playback_pcm =
      needs_conversion ? converted_pcm : pcm_s16le;

  // 100 ms shared-mode buffer, in 100-ns units.
  constexpr REFERENCE_TIME kBufferDuration = 1'000'000;
  throw_if_failed(
      audio_client->Initialize(AUDCLNT_SHAREMODE_SHARED, 0, kBufferDuration, 0, endpoint_format, nullptr),
      "IAudioClient::Initialize");

  UINT32 buffer_frame_count = 0;
  throw_if_failed(audio_client->GetBufferSize(&buffer_frame_count), "IAudioClient::GetBufferSize");

  ComPtr<IAudioRenderClient> render_client;
  throw_if_failed(audio_client->GetService(IID_PPV_ARGS(&render_client)),
                  "IAudioClient::GetService(IAudioRenderClient)");

  const std::size_t bytes_per_frame = endpoint_format->nBlockAlign;
  const std::size_t total_frames = playback_pcm.size() / bytes_per_frame;
  const auto start_seconds = static_cast<double>(start_source_frame) / sample_rate_hz;
  const auto start_endpoint_frame = static_cast<std::size_t>(std::min<std::uint64_t>(
      total_frames,
      convert_frame_position(start_source_frame, sample_rate_hz, endpoint_format->nSamplesPerSec)));
  std::size_t frames_written = start_endpoint_frame;

  const auto write_frames = [&](UINT32 frames_available) {
    if (frames_available == 0 || frames_written >= total_frames) {
      return;
    }
    const std::size_t frames_remaining = total_frames - frames_written;
    const UINT32 frames_to_write =
        static_cast<UINT32>(std::min<std::size_t>(frames_available, frames_remaining));
    BYTE *buffer = nullptr;
    throw_if_failed(render_client->GetBuffer(frames_to_write, &buffer),
                    "IAudioRenderClient::GetBuffer");
    std::memcpy(buffer, playback_pcm.data() + frames_written * bytes_per_frame,
               frames_to_write * bytes_per_frame);
    throw_if_failed(render_client->ReleaseBuffer(frames_to_write, 0),
                    "IAudioRenderClient::ReleaseBuffer");
    frames_written += frames_to_write;
  };

  if (control != nullptr) control->update_position(source_origin_seconds + start_seconds);
  write_frames(buffer_frame_count);
  if (control != nullptr && control->paused() &&
      !control->wait_until_resumed_or_seek(seek_generation)) {
    return control->seek_generation() != seek_generation;
  }
  if (control != nullptr && control->seek_generation() != seek_generation) return true;
  throw_if_failed(audio_client->Start(), "IAudioClient::Start");

  const auto poll_interval = std::chrono::milliseconds(kBufferDuration / 10'000 / 2);
  const auto wait_if_paused = [&]() {
    if (control == nullptr || !control->paused()) {
      return true;
    }
    audio_client->Stop();
    if (!control->wait_until_resumed_or_seek(seek_generation)) {
      return false;
    }
    throw_if_failed(audio_client->Start(), "IAudioClient::Start after pause");
    return true;
  };
  while (frames_written < total_frames) {
    if (control != nullptr && control->cancelled()) {
      audio_client->Stop();
      return false;
    }
    if (control != nullptr && control->seek_generation() != seek_generation) {
      audio_client->Stop();
      return true;
    }
    if (!wait_if_paused()) {
      audio_client->Stop();
      return false;
    }
    std::this_thread::sleep_for(poll_interval);
    UINT32 padding_frames = 0;
    throw_if_failed(audio_client->GetCurrentPadding(&padding_frames),
                    "IAudioClient::GetCurrentPadding");
    if (control != nullptr) {
      const auto played_frames = frames_written >= padding_frames ? frames_written - padding_frames : 0;
      control->update_position(source_origin_seconds +
                               static_cast<double>(played_frames) / endpoint_format->nSamplesPerSec);
    }
    write_frames(buffer_frame_count - padding_frames);
  }

  // Drain: wait for the device to finish playing what's buffered, otherwise
  // Stop() cuts off the tail of the last chunk.
  UINT32 padding_frames = 0;
  do {
    if (control != nullptr && control->cancelled()) {
      audio_client->Stop();
      return false;
    }
    if (control != nullptr && control->seek_generation() != seek_generation) {
      audio_client->Stop();
      return true;
    }
    if (!wait_if_paused()) {
      audio_client->Stop();
      return false;
    }
    std::this_thread::sleep_for(poll_interval);
    throw_if_failed(audio_client->GetCurrentPadding(&padding_frames),
                    "IAudioClient::GetCurrentPadding");
    if (control != nullptr) {
      const auto played_frames = frames_written >= padding_frames ? frames_written - padding_frames : 0;
      control->update_position(source_origin_seconds +
                               static_cast<double>(played_frames) / endpoint_format->nSamplesPerSec);
    }
  } while (padding_frames > 0);

  throw_if_failed(audio_client->Stop(), "IAudioClient::Stop");
  if (control != nullptr) control->update_position(source_origin_seconds +
                                                   static_cast<double>(total_frames) /
                                                       endpoint_format->nSamplesPerSec);
  return false;
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

std::vector<std::string> list_output_devices() {
  throw std::runtime_error(
      "audio output device enumeration is not implemented on this platform yet (Windows only, "
      "see docs/design/architecture.md#desktop-integration)");
}

#endif

}  // namespace tts_host
