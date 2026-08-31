#include "tts_host/playback_sink.hpp"

#include <stdexcept>

#ifdef _WIN32
#include <windows.h>

#include <mmdeviceapi.h>

#include <audioclient.h>
#include <functiondiscoverykeys_devpkey.h>
#include <propvarutil.h>
#include <wrl/client.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <string>
#include <thread>
#endif

namespace tts_host {

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

}  // namespace

void SystemPlaybackSink::play(std::uint32_t sample_rate_hz, std::uint16_t channels,
                              const std::vector<std::uint8_t> &pcm_s16le,
                              const std::string &device_name) {
  if (pcm_s16le.empty()) {
    return;
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

  // 100 ms shared-mode buffer, in 100-ns units.
  constexpr REFERENCE_TIME kBufferDuration = 1'000'000;
  throw_if_failed(
      audio_client->Initialize(AUDCLNT_SHAREMODE_SHARED, 0, kBufferDuration, 0, &format, nullptr),
      "IAudioClient::Initialize");

  UINT32 buffer_frame_count = 0;
  throw_if_failed(audio_client->GetBufferSize(&buffer_frame_count), "IAudioClient::GetBufferSize");

  ComPtr<IAudioRenderClient> render_client;
  throw_if_failed(audio_client->GetService(IID_PPV_ARGS(&render_client)),
                  "IAudioClient::GetService(IAudioRenderClient)");

  const std::size_t bytes_per_frame = format.nBlockAlign;
  const std::size_t total_frames = pcm_s16le.size() / bytes_per_frame;
  std::size_t frames_written = 0;

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
    std::memcpy(buffer, pcm_s16le.data() + frames_written * bytes_per_frame,
               frames_to_write * bytes_per_frame);
    throw_if_failed(render_client->ReleaseBuffer(frames_to_write, 0),
                    "IAudioRenderClient::ReleaseBuffer");
    frames_written += frames_to_write;
  };

  write_frames(buffer_frame_count);
  throw_if_failed(audio_client->Start(), "IAudioClient::Start");

  const auto poll_interval = std::chrono::milliseconds(kBufferDuration / 10'000 / 2);
  while (frames_written < total_frames) {
    std::this_thread::sleep_for(poll_interval);
    UINT32 padding_frames = 0;
    throw_if_failed(audio_client->GetCurrentPadding(&padding_frames),
                    "IAudioClient::GetCurrentPadding");
    write_frames(buffer_frame_count - padding_frames);
  }

  // Drain: wait for the device to finish playing what's buffered, otherwise
  // Stop() cuts off the tail of the last chunk.
  UINT32 padding_frames = 0;
  do {
    std::this_thread::sleep_for(poll_interval);
    throw_if_failed(audio_client->GetCurrentPadding(&padding_frames),
                    "IAudioClient::GetCurrentPadding");
  } while (padding_frames > 0);

  throw_if_failed(audio_client->Stop(), "IAudioClient::Stop");
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
                              const std::string &device_name) {
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
