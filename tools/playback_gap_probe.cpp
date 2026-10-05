// Measures the audible gap SystemPlaybackSink leaves between consecutive
// sentences (review finding #16,
// docs/reviews/code-review-2026-10-03-roadmap-slices-branch.md#finding-16).
//
// Plays short tones through the default output endpoint while a WASAPI
// loopback client records the endpoint mix, then reports silence between
// tones. Two conditions run back to back:
//   control    -- one play() call with tones separated by a known silence, to
//                 validate the detector against a known gap;
//   per-call   -- one play() call per tone on a fresh thread, as
//                 synthesis_pipeline.cpp does for each retained sentence.
// A silent keep-alive render stream keeps loopback packets flowing while no
// tone is playing. The tones are audible. Windows only.

#include "tts_host/playback_sink.hpp"

#define NOMINMAX
#include <windows.h>

#include <mmdeviceapi.h>

#include <audioclient.h>
#include <wrl/client.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace {

constexpr std::uint32_t kSourceRate = 24'000;  // Kokoro emits 24 kHz mono S16LE.
constexpr double kToneSeconds = 0.4;
constexpr double kControlGapSeconds = 0.020;
constexpr double kMinGapSeconds = 0.004;

void check(HRESULT result, const char *what) {
  if (FAILED(result)) {
    throw std::runtime_error(std::string(what) + " failed (hresult " + std::to_string(result) + ")");
  }
}

std::vector<std::uint8_t> tone(double seconds, double amplitude) {
  const auto frames = static_cast<std::size_t>(seconds * kSourceRate);
  const auto fade = static_cast<std::size_t>(0.003 * kSourceRate);
  std::vector<std::uint8_t> pcm(frames * 2);
  for (std::size_t i = 0; i < frames; ++i) {
    const double envelope = std::min({1.0, static_cast<double>(i) / fade,
                                      static_cast<double>(frames - i) / fade});
    const auto sample = static_cast<std::int16_t>(
        32767.0 * amplitude * envelope * std::sin(2.0 * 3.14159265358979 * 440.0 * i / kSourceRate));
    std::memcpy(&pcm[i * 2], &sample, 2);
  }
  return pcm;
}

std::vector<std::uint8_t> silence(double seconds) {
  return std::vector<std::uint8_t>(static_cast<std::size_t>(seconds * kSourceRate) * 2, 0);
}

ComPtr<IAudioClient> default_endpoint_client() {
  ComPtr<IMMDeviceEnumerator> enumerator;
  check(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                         IID_PPV_ARGS(&enumerator)),
        "CoCreateInstance(MMDeviceEnumerator)");
  ComPtr<IMMDevice> device;
  check(enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device), "GetDefaultAudioEndpoint");
  ComPtr<IAudioClient> client;
  check(device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, &client), "Activate");
  return client;
}

// Records the per-frame peak magnitude of the endpoint mix, plus a silent
// render stream so the loopback client keeps receiving packets between tones.
class LoopbackRecorder {
 public:
  void start() {
    thread_ = std::thread([this] { run(); });
    std::unique_lock lock(mutex_);
    ready_.wait(lock, [this] { return started_ || error_; });
    if (error_) std::rethrow_exception(error_);
  }

  std::vector<float> stop() {
    stopping_ = true;
    thread_.join();
    if (error_) std::rethrow_exception(error_);
    return std::move(samples_);
  }

  std::uint32_t rate() const { return rate_; }
  std::size_t discontinuities() const { return discontinuities_; }

 private:
  void run() {
    try {
      check(CoInitializeEx(nullptr, COINIT_MULTITHREADED), "CoInitializeEx");
      ComPtr<IAudioClient> capture = default_endpoint_client();
      ComPtr<IAudioClient> keep_alive = default_endpoint_client();
      WAVEFORMATEX *mix = nullptr;
      check(capture->GetMixFormat(&mix), "GetMixFormat");
      rate_ = mix->nSamplesPerSec;
      const auto channels = mix->nChannels;
      const bool is_float = mix->wBitsPerSample == 32;  // Shared mix format is float32 in practice.
      if (!is_float) throw std::runtime_error("loopback mix format is not 32-bit float");
      constexpr REFERENCE_TIME kBuffer = 1'000'000;
      check(capture->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK, kBuffer, 0,
                                mix, nullptr),
            "Initialize(loopback)");
      check(keep_alive->Initialize(AUDCLNT_SHAREMODE_SHARED, 0, kBuffer, 0, mix, nullptr),
            "Initialize(keep-alive)");
      ComPtr<IAudioCaptureClient> capture_client;
      check(capture->GetService(IID_PPV_ARGS(&capture_client)), "GetService(capture)");
      ComPtr<IAudioRenderClient> render_client;
      check(keep_alive->GetService(IID_PPV_ARGS(&render_client)), "GetService(render)");
      UINT32 render_frames = 0;
      check(keep_alive->GetBufferSize(&render_frames), "GetBufferSize");
      const auto fill_silence = [&] {
        UINT32 padding = 0;
        check(keep_alive->GetCurrentPadding(&padding), "GetCurrentPadding");
        const UINT32 frames = render_frames - padding;
        if (frames == 0) return;
        BYTE *buffer = nullptr;
        check(render_client->GetBuffer(frames, &buffer), "GetBuffer(render)");
        check(render_client->ReleaseBuffer(frames, AUDCLNT_BUFFERFLAGS_SILENT), "ReleaseBuffer(render)");
      };
      fill_silence();
      check(keep_alive->Start(), "Start(keep-alive)");
      check(capture->Start(), "Start(loopback)");
      {
        std::lock_guard lock(mutex_);
        started_ = true;
      }
      ready_.notify_all();
      while (!stopping_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        fill_silence();
        UINT32 packet = 0;
        check(capture_client->GetNextPacketSize(&packet), "GetNextPacketSize");
        while (packet != 0) {
          BYTE *data = nullptr;
          UINT32 frames = 0;
          DWORD flags = 0;
          check(capture_client->GetBuffer(&data, &frames, &flags, nullptr, nullptr), "GetBuffer(capture)");
          if (flags & AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY) ++discontinuities_;
          const auto *samples = reinterpret_cast<const float *>(data);
          for (UINT32 frame = 0; frame < frames; ++frame) {
            float mono = 0.0F;
            if (!(flags & AUDCLNT_BUFFERFLAGS_SILENT)) {
              for (WORD channel = 0; channel < channels; ++channel) {
                mono += samples[frame * channels + channel] / channels;
              }
            }
            samples_.push_back(mono);
          }
          check(capture_client->ReleaseBuffer(frames), "ReleaseBuffer(capture)");
          check(capture_client->GetNextPacketSize(&packet), "GetNextPacketSize");
        }
      }
      capture->Stop();
      keep_alive->Stop();
      CoTaskMemFree(mix);
      CoUninitialize();
    } catch (...) {
      std::lock_guard lock(mutex_);
      error_ = std::current_exception();
      ready_.notify_all();
    }
  }

  std::thread thread_;
  std::mutex mutex_;
  std::condition_variable ready_;
  bool started_ = false;
  std::exception_ptr error_;
  std::atomic_bool stopping_{false};
  std::vector<float> samples_;
  std::uint32_t rate_ = 0;
  std::size_t discontinuities_ = 0;
};

// Tone absence between the first and last tone, from 440 Hz Goertzel
// magnitude over 5 ms windows hopped by 1 ms. Detecting the probe frequency
// rather than broadband level ignores other applications' audio. A window
// counts as tone when at least ~30% of it overlaps the tone, so an absent run
// of N hops corresponds to an N + 2 ms gap; the control condition checks this.
std::vector<double> gaps(const std::vector<float> &samples, std::uint32_t rate) {
  const std::size_t window = rate / 200;
  const std::size_t hop = rate / 1000;
  const double coefficient = 2.0 * std::cos(2.0 * 3.14159265358979 * 440.0 / rate);
  std::vector<double> magnitudes;
  for (std::size_t begin = 0; begin + window <= samples.size(); begin += hop) {
    double previous = 0.0, before = 0.0;
    for (std::size_t i = begin; i < begin + window; ++i) {
      const double current = samples[i] + coefficient * previous - before;
      before = previous;
      previous = current;
    }
    const double power = previous * previous + before * before - coefficient * previous * before;
    magnitudes.push_back(2.0 * std::sqrt(std::max(0.0, power)) / window);
  }
  if (magnitudes.empty()) throw std::runtime_error("loopback recorded no audio");
  std::vector<double> sorted = magnitudes;
  std::sort(sorted.begin(), sorted.end());
  const double reference = sorted[sorted.size() * 9 / 10];
  if (reference <= 1e-4) throw std::runtime_error("loopback recorded no tone; is the endpoint muted?");
  const double threshold = reference * 0.3;
  std::vector<double> result;
  std::size_t last_tone = SIZE_MAX;
  for (std::size_t i = 0; i < magnitudes.size(); ++i) {
    if (magnitudes[i] < threshold) continue;
    if (last_tone != SIZE_MAX && i - last_tone > 1) {
      const double gap = static_cast<double>(i - last_tone - 1) * hop / rate + 0.002;
      if (gap >= kMinGapSeconds) result.push_back(gap);
    }
    last_tone = i;
  }
  return result;
}

void report(const char *name, std::vector<double> values, std::size_t expected) {
  std::cout << name << ": " << values.size() << " gaps (expected " << expected << ")";
  if (values.empty()) {
    std::cout << '\n';
    return;
  }
  std::sort(values.begin(), values.end());
  const auto at = [&](double q) { return values[static_cast<std::size_t>(q * (values.size() - 1))] * 1000.0; };
  std::cout << "; ms min " << values.front() * 1000.0 << " median " << at(0.5) << " p95 " << at(0.95)
            << " max " << values.back() * 1000.0 << '\n';
}

}  // namespace

int main(int argc, char **argv) {
  try {
    const int boundaries = argc > 1 ? std::max(1, std::atoi(argv[1])) : 20;
    const auto clip = tone(kToneSeconds, 0.25);
    tts_host::SystemPlaybackSink sink;

    std::cout << "Playing " << boundaries + 1 << " tones per condition at " << kSourceRate
              << " Hz mono through the default endpoint.\n";

    LoopbackRecorder control_recorder;
    control_recorder.start();
    std::vector<std::uint8_t> joined;
    const auto spacer = silence(kControlGapSeconds);
    for (int i = 0; i <= boundaries; ++i) {
      if (i > 0) joined.insert(joined.end(), spacer.begin(), spacer.end());
      joined.insert(joined.end(), clip.begin(), clip.end());
    }
    sink.play(kSourceRate, 1, joined);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    const auto control_peaks = control_recorder.stop();

    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    LoopbackRecorder call_recorder;
    call_recorder.start();
    std::vector<double> overheads;
    for (int i = 0; i <= boundaries; ++i) {
      const auto begin = std::chrono::steady_clock::now();
      std::thread([&] { sink.play(kSourceRate, 1, clip); }).join();
      overheads.push_back(std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count() -
                          kToneSeconds);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    const auto call_peaks = call_recorder.stop();

    std::cout << "Loopback mix rate " << control_recorder.rate() << " Hz; discontinuities "
              << control_recorder.discontinuities() + call_recorder.discontinuities() << '\n';
    std::cout << "Control gap inserted: " << kControlGapSeconds * 1000.0 << " ms\n";
    report("control  (single call)", gaps(control_peaks, control_recorder.rate()), boundaries);
    report("per-call (audible)", gaps(call_peaks, call_recorder.rate()), boundaries);
    report("per-call (wall-clock overhead per play)", overheads, boundaries + 1);
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "playback_gap_probe: " << error.what() << '\n';
    return 1;
  }
}
