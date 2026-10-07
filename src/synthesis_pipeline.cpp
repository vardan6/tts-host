#include "tts_host/synthesis_pipeline.hpp"
#include "tts_host/audio_history.hpp"
#include "tts_host/lookahead.hpp"
#include "tts_host/path_encoding.hpp"
#include "tts_host/recovering_runner.hpp"
#include "tts_host/text_splitter.hpp"
#include "tts_host/wav_writer.hpp"
#include <algorithm>
#include <exception>
#include <iostream>
#include <thread>
#include <utility>

namespace tts_host {
namespace {
void throw_if_cancelled(const PlaybackControl *control) {
  if (control && control->cancelled()) throw RunnerCancelled();
}
}  // namespace

void run_synthesis_pipeline(const std::string &spoken_text, const CliOptions &options,
                            const ConfigDocument &document, const RunnerSelection &selection,
                            PlaybackControl *control, PlaybackController *playback_controller,
                            PlaybackSink *sink, RunnerDeadlines deadlines, RecoveringRunner *resident) {
  tts_host::RecoveringRunner one_shot(selection.runner_path, selection.model_path,
      selection.voice_path, deadlines, [control] { return control != nullptr && control->cancelled(); });

  auto &session = resident ? *resident : one_shot;

  // Splitting into sentence-scale chunks and synthesizing them as separate
  // back-to-back requests is what lets a chunk start playing before the rest
  // of a long text is synthesized (docs/design/architecture.md#speech-pipeline).
  const auto chunks = tts_host::split_into_sentences(spoken_text);

  SystemPlaybackSink system_sink;
  PlaybackSink &playback_sink = sink == nullptr ? system_sink : *sink;
  const std::string output_device = document.value.at("audio").at("outputDevice").get<std::string>();
  const double default_speech_speed = document.value.at("audio").value("speechSpeed", 1.0);
  // One endpoint stream per utterance, so sentence boundaries add no device
  // gap (docs/design/architecture.md#utterance-audio-stream). Declared before
  // the playback thread, which uses it until joined.
  std::unique_ptr<PlaybackStream> stream;
  if (options.play_audio) stream = playback_sink.open_stream(output_device, kDefaultPlaybackBuffer);
  std::unique_ptr<tts_host::AudioHistory> audio_history;
  if (options.play_audio && control != nullptr && playback_controller != nullptr) {
    audio_history = std::make_unique<tts_host::AudioHistory>();
  }

  struct RetainedChunk {
    std::uint64_t byte_offset;
    std::uint64_t byte_length;
    std::uint32_t sample_rate_hz;
    std::uint16_t channels;
    std::uint64_t frames;
    double start_seconds;
    double duration_seconds;
  };
  std::vector<RetainedChunk> retained_chunks;
  double generated_seconds = 0.0;

  std::vector<std::uint8_t> pcm_payload;
  std::uint32_t sample_rate_hz = 0;
  std::uint32_t channels = 0;
  std::uint64_t total_sample_frames = 0;

  // Plays each chunk on a background thread so the next chunk's runner round
  // trip (the send_request/read_audio_stream_until_end below) overlaps with
  // playback instead of waiting for it -- the "small lookahead" in
  // docs/design/architecture.md#speech-pipeline. Joining the previous
  // playback thread before starting the next one keeps the "two utterances
  // never play simultaneously" guarantee from the same doc.
  std::thread playback_thread;
  std::atomic_bool playback_done{false};
  std::exception_ptr playback_error;
  bool playback_interrupted_for_seek = false;
  std::size_t playback_chunk_index = 0;
  std::uint64_t handled_seek_generation = control == nullptr ? 0 : control->seek_generation();
  std::size_t next_playback_chunk = 0;
  std::uint64_t next_playback_frame = 0;
  const auto join_playback = [&playback_thread, &playback_error]() {
    if (playback_thread.joinable()) {
      playback_thread.join();
    }
    if (playback_error) {
      std::rethrow_exception(std::exchange(playback_error, nullptr));
    }
  };

  struct PlaybackCleanup {
    std::thread &thread;
    tts_host::PlaybackControl *control;
    int exceptions = std::uncaught_exceptions();
    ~PlaybackCleanup() {
      if (thread.joinable()) {
        if (control && std::uncaught_exceptions() > exceptions) control->cancel();
        thread.join();
      }
    }
  } playback_cleanup{playback_thread, control};

  const auto apply_pending_seek = [&]() {
    if (control == nullptr || playback_controller == nullptr || retained_chunks.empty()) return;
    const auto generation = control->seek_generation();
    if (generation == handled_seek_generation) return;
    const double target = std::clamp(control->requested_seek_seconds(), 0.0, generated_seconds);
    std::size_t index = 0;
    while (index + 1 < retained_chunks.size() &&
           target >= retained_chunks[index].start_seconds + retained_chunks[index].duration_seconds) {
      ++index;
    }
    const auto &segment = retained_chunks[index];
    const double within = std::max(0.0, target - segment.start_seconds);
    next_playback_chunk = index;
    next_playback_frame = std::min<std::uint64_t>(
        segment.frames, static_cast<std::uint64_t>(within * segment.sample_rate_hz));
    control->update_position(target);
    handled_seek_generation = generation;
  };

  const auto launch_retained_chunk = [&]() {
    if (audio_history == nullptr || next_playback_chunk >= retained_chunks.size()) return false;
    const auto index = next_playback_chunk;
    const auto segment = retained_chunks[index];
    const auto payload = audio_history->read(segment.byte_offset, segment.byte_length);
    const auto start_frame = next_playback_frame;
    next_playback_frame = 0;
    playback_chunk_index = index;
    playback_interrupted_for_seek = false;
    playback_done = false;
    const auto generation = handled_seek_generation;
    playback_thread = std::thread(
        [endpoint = stream.get(), segment, payload, start_frame, &playback_error,
         &playback_interrupted_for_seek, &playback_done, control, generation]() {
          try {
            playback_interrupted_for_seek = endpoint->play_from(
                segment.sample_rate_hz, segment.channels, payload, start_frame,
                segment.start_seconds, control, generation);
          } catch (...) {
            playback_error = std::current_exception();
          }
          playback_done = true;
        });
    return true;
  };

  for (const auto &chunk : chunks) {
  retry_chunk:
    // Runner waits and endpoint playback both observe Stop/replacement.
    throw_if_cancelled(control);
    // Keep at most one sentence of lookahead. If Now Playing changes speed
    // while it is being prepared or waiting behind audible PCM, discard it and
    // regenerate this exact sentence. This preserves text order without PCM
    // resampling or an audio-to-word alignment guess.
    tts_host::RunnerSynthesizeResponse synthesize_result;
    std::vector<std::uint8_t> chunk_payload;
    std::uint64_t speed_generation = 0;
    while (true) {
      const double speech_speed = playback_controller == nullptr
                                      ? default_speech_speed
                                      : playback_controller->speech_speed();
      speed_generation = playback_controller == nullptr
                             ? 0
                             : playback_controller->speech_speed_generation();
      const auto synthesized = [&]() {
        if (audio_history == nullptr) return session.synthesize(chunk, speech_speed);
        auto pending = std::async(std::launch::async, [&] { return session.synthesize(chunk, speech_speed); });
        return tts_host::await_lookahead(pending, [&] {
          throw_if_cancelled(control);
          const bool seek_pending = control->seek_generation() != handled_seek_generation;
          if (playback_thread.joinable() && (seek_pending || playback_done.load())) {
            join_playback();
            if (!playback_interrupted_for_seek) next_playback_chunk = playback_chunk_index + 1;
          }
          apply_pending_seek();
          if (!playback_thread.joinable()) launch_retained_chunk();
        }, [&] { control->cancel(); });
      }();
      synthesize_result = synthesized.format;
      chunk_payload = synthesized.pcm;
      // The lookahead remains unplayed until join_playback returns. A changed
      // generation means it was synthesized at a stale requested speed.
      if (playback_controller == nullptr ||
          playback_controller->speech_speed_generation() == speed_generation) {
        break;
      }
      throw_if_cancelled(control);
    }
    if (options.play_audio) {
      const bool had_playback = playback_thread.joinable();
      join_playback();
      if (had_playback && !playback_interrupted_for_seek) {
        next_playback_chunk = playback_chunk_index + 1;
      }
      apply_pending_seek();
      throw_if_cancelled(control);
      // A speed change may have arrived while this sentence waited as
      // lookahead. Regenerate it before playback, leaving audible PCM alone.
      if (playback_controller != nullptr &&
          !playback_controller->prepare_sentence_for_playback(control, speed_generation)) {
        goto retry_chunk;
      }
    }

    sample_rate_hz = synthesize_result.sample_rate_hz;
    channels = synthesize_result.channels;
    total_sample_frames += synthesize_result.total_sample_frames;

    if (audio_history != nullptr) {
      const auto byte_offset = audio_history->append(chunk_payload);
      const auto duration = synthesize_result.sample_rate_hz == 0
                                ? 0.0
                                : static_cast<double>(synthesize_result.total_sample_frames) /
                                      synthesize_result.sample_rate_hz;
      retained_chunks.push_back({byte_offset, chunk_payload.size(), synthesize_result.sample_rate_hz,
                                 static_cast<std::uint16_t>(synthesize_result.channels),
                                 synthesize_result.total_sample_frames, generated_seconds, duration});
      generated_seconds += duration;
      playback_controller->set_generated_seconds(control, generated_seconds);
      launch_retained_chunk();
    } else if (options.play_audio) {
      const auto sample_rate = synthesize_result.sample_rate_hz;
      const auto channel_count = static_cast<std::uint16_t>(synthesize_result.channels);
      playback_thread = std::thread(
          [endpoint = stream.get(), sample_rate, channel_count, chunk_payload, &playback_error,
           control, generation = handled_seek_generation]() {
            try {
              endpoint->play_from(sample_rate, channel_count, chunk_payload, 0, 0.0, control,
                                  generation);
            } catch (...) {
              playback_error = std::current_exception();
            }
          });
    }

    if (options.output_path.has_value()) {
      pcm_payload.insert(pcm_payload.end(), chunk_payload.begin(), chunk_payload.end());
    }
  }
  while (stream != nullptr) {
    while (playback_thread.joinable()) {
      join_playback();
      const bool was_interrupted = playback_interrupted_for_seek;
      if (!was_interrupted) next_playback_chunk = playback_chunk_index + 1;
      apply_pending_seek();
      throw_if_cancelled(control);
      if (!launch_retained_chunk()) break;
    }
    throw_if_cancelled(control);
    // The stream drains once, at the end of the utterance. A seek during the
    // drain replays retained audio into the same stream.
    playback_interrupted_for_seek = false;
    playback_thread = std::thread(
        [endpoint = stream.get(), &playback_error, &playback_interrupted_for_seek, control,
         generation = handled_seek_generation]() {
          try {
            playback_interrupted_for_seek = endpoint->drain(control, generation);
          } catch (...) {
            playback_error = std::current_exception();
          }
        });
    join_playback();
    if (!playback_interrupted_for_seek) break;
    apply_pending_seek();
    throw_if_cancelled(control);
    if (!launch_retained_chunk()) break;
  }
  throw_if_cancelled(control);

  std::optional<tts_host::RunnerStatsResponse> stats;
  if (options.report_stats) {
    stats = session.stats();
  }

  const auto exit_code = resident ? 0 : session.finish();
  if (exit_code != 0) {
    throw std::runtime_error("runner process exited with code " + std::to_string(exit_code));
  }

  if (options.output_path.has_value()) {
    tts_host::write_wav_file(*options.output_path, sample_rate_hz,
                             static_cast<std::uint16_t>(channels), 16, pcm_payload);
    std::cout << "Synthesized " << total_sample_frames << " sample frames to "
              << tts_host::path_to_utf8(*options.output_path) << '\n';
  }

  if (options.play_audio) {
    std::cout << "Played " << total_sample_frames << " sample frames\n";
  }

  if (stats.has_value()) {
    std::cout << "Stats: peak RSS " << stats->peak_rss_bytes << " bytes, peak VRAM "
              << stats->peak_vram_bytes << " bytes, time to first chunk "
              << stats->time_to_first_chunk_ms << " ms, sample count " << stats->sample_count
              << '\n';
  }
}
}  // namespace tts_host
