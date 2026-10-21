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
  const auto &audio = document.value.at("audio");
  const std::string output_device = audio.at("outputDevice").get<std::string>();
  const double default_speech_speed = audio.value("speechSpeed", 1.0);
  // The buffer applies per utterance stream; pause and lookahead are read at
  // each sentence boundary, from live Settings when a controller is present
  // (docs/design/architecture.md#utterance-audio-stream).
  const std::chrono::milliseconds buffer(
      audio.value("bufferMilliseconds", static_cast<int>(kDefaultPlaybackBuffer.count())));
  const auto sentence_pause_milliseconds = [&] {
    return playback_controller != nullptr ? playback_controller->sentence_pause_milliseconds()
                                          : audio.value("sentencePauseMilliseconds", 0);
  };
  const auto lookahead_sentences = [&] {
    return playback_controller != nullptr ? playback_controller->lookahead_sentences()
                                          : audio.value("lookaheadSentences", 1);
  };
  const auto current_speed = [&] {
    return playback_controller == nullptr ? default_speech_speed : playback_controller->speech_speed();
  };
  const auto current_speed_generation = [&]() -> std::uint64_t {
    return playback_controller == nullptr ? 0 : playback_controller->speech_speed_generation();
  };

  // One endpoint stream per utterance, so sentence boundaries add no device
  // gap (docs/design/architecture.md#utterance-audio-stream). Synthesized
  // sentences go straight into retained history, so lookahead of any depth is
  // already seekable. Both are declared before the playback thread, which
  // uses them until joined.
  std::unique_ptr<PlaybackStream> stream;
  std::unique_ptr<tts_host::AudioHistory> audio_history;
  if (options.play_audio) {
    stream = playback_sink.open_stream(output_device, buffer);
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
    std::size_t text_index;
    std::uint64_t speed_generation;
  };
  std::vector<RetainedChunk> retained_chunks;
  // Retained chunks below this index have been launched at least once; the
  // rest are lookahead that no listener has heard yet.
  std::size_t launched_chunks = 0;
  double generated_seconds = 0.0;
  std::size_t next_text = 0;

  std::vector<std::uint8_t> pcm_payload;
  std::uint32_t sample_rate_hz = 0;
  std::uint32_t channels = 0;
  std::uint64_t total_sample_frames = 0;

  // Plays each chunk on a background thread so the next chunk's runner round
  // trip overlaps with playback instead of waiting for it -- the lookahead in
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

  // A speed change discards every unheard sentence, however deep, and
  // resynthesizes from the first of them. Their spool bytes stay unused.
  const auto discard_unplayed = [&]() {
    if (launched_chunks >= retained_chunks.size()) return;
    const auto &first = retained_chunks[launched_chunks];
    next_text = first.text_index;
    generated_seconds = first.start_seconds;
    retained_chunks.resize(launched_chunks);
    next_playback_chunk = std::min(next_playback_chunk, retained_chunks.size());
    if (playback_controller != nullptr) playback_controller->set_generated_seconds(control, generated_seconds);
  };

  const auto launch_retained_chunk = [&]() {
    if (stream == nullptr || next_playback_chunk >= retained_chunks.size()) return false;
    const auto index = next_playback_chunk;
    if (index >= launched_chunks) {
      // First playback of lookahead: one synthesized at a stale requested
      // speed is regenerated, leaving audible PCM alone. This avoids PCM
      // resampling and an audio-to-word alignment guess.
      if (playback_controller != nullptr &&
          !playback_controller->prepare_sentence_for_playback(
              control, retained_chunks[index].speed_generation)) {
        discard_unplayed();
        return false;
      }
      launched_chunks = index + 1;
    }
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

  // Advances retained playback without blocking: collects a finished or
  // seek-interrupted sentence and launches the next one.
  const auto service_playback = [&]() {
    throw_if_cancelled(control);
    if (stream == nullptr) return;
    const bool seek_pending = control != nullptr && control->seek_generation() != handled_seek_generation;
    if (playback_thread.joinable() && (seek_pending || playback_done.load())) {
      join_playback();
      if (!playback_interrupted_for_seek) next_playback_chunk = playback_chunk_index + 1;
    }
    apply_pending_seek();
    if (!playback_thread.joinable()) launch_retained_chunk();
  };

  while (true) {
    while (next_text < chunks.size()) {
      service_playback();
      // Pause lets at most the in-flight request finish; full lookahead waits
      // for playback to take a sentence.
      if (stream != nullptr && !retained_chunks.empty() &&
          ((control != nullptr && control->paused()) ||
           retained_chunks.size() - launched_chunks >=
               static_cast<std::size_t>(lookahead_sentences()))) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        continue;
      }
      const auto text_index = next_text;
      const auto &chunk = chunks[text_index];
      const double speech_speed = current_speed();
      const auto speed_generation = current_speed_generation();
      const auto synthesized = [&]() {
        if (stream == nullptr) return session.synthesize(chunk, speech_speed);
        auto pending = std::async(std::launch::async, [&] { return session.synthesize(chunk, speech_speed); });
        return tts_host::await_lookahead(pending, service_playback, [control] {
          if (control != nullptr) control->cancel();
        });
      }();
      // A speed change rewound lookahead while this was in flight, or made
      // this sentence stale; either way it is synthesized again.
      if (next_text != text_index) continue;
      if (current_speed_generation() != speed_generation) {
        discard_unplayed();
        continue;
      }

      const auto &format = synthesized.format;
      std::vector<std::uint8_t> chunk_payload = synthesized.pcm;
      // The sentence pause is source PCM: retained, seekable, and counted in
      // position.
      if (text_index > 0 && format.sample_rate_hz != 0) {
        const auto pause_frames =
            static_cast<std::uint64_t>(format.sample_rate_hz) * sentence_pause_milliseconds() / 1000;
        chunk_payload.insert(chunk_payload.begin(), pause_frames * format.channels * 2, 0);
      }
      const auto bytes_per_frame = static_cast<std::uint64_t>(format.channels) * 2;
      const std::uint64_t frames = bytes_per_frame == 0 ? 0 : chunk_payload.size() / bytes_per_frame;
      sample_rate_hz = format.sample_rate_hz;
      channels = format.channels;

      if (audio_history != nullptr) {
        const auto byte_offset = audio_history->append(chunk_payload);
        const auto duration =
            format.sample_rate_hz == 0 ? 0.0 : static_cast<double>(frames) / format.sample_rate_hz;
        retained_chunks.push_back({byte_offset, chunk_payload.size(), format.sample_rate_hz,
                                   static_cast<std::uint16_t>(format.channels), frames,
                                   generated_seconds, duration, text_index, speed_generation});
        generated_seconds += duration;
        if (playback_controller != nullptr) playback_controller->set_generated_seconds(control, generated_seconds);
      } else {
        total_sample_frames += frames;
        if (options.output_path.has_value()) {
          pcm_payload.insert(pcm_payload.end(), chunk_payload.begin(), chunk_payload.end());
        }
      }
      ++next_text;
      service_playback();
    }
    if (stream == nullptr) break;

    // Play out the retained sentences.
    while (next_text == chunks.size() &&
           (playback_thread.joinable() || next_playback_chunk < retained_chunks.size())) {
      if (playback_thread.joinable()) {
        join_playback();
        if (!playback_interrupted_for_seek) next_playback_chunk = playback_chunk_index + 1;
      }
      apply_pending_seek();
      throw_if_cancelled(control);
      launch_retained_chunk();
    }
    // A speed change discarded unheard sentences; synthesize them again.
    if (next_text < chunks.size()) continue;
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
  }
  throw_if_cancelled(control);

  if (audio_history != nullptr) {
    for (const auto &segment : retained_chunks) {
      total_sample_frames += segment.frames;
      if (options.output_path.has_value()) {
        const auto pcm = audio_history->read(segment.byte_offset, segment.byte_length);
        pcm_payload.insert(pcm_payload.end(), pcm.begin(), pcm.end());
      }
    }
  }

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
    std::cout << "Stats: peak RSS " << stats->peak_rss_bytes << " bytes, peak VRAM ";
    if (stats->vram_measurement == "unavailable") std::cout << "unavailable";
    else std::cout << stats->peak_vram_bytes << " bytes";
    std::cout << ", time to first chunk " << stats->time_to_first_chunk_ms << " ms, sample count " << stats->sample_count
              << '\n';
  }
}
}  // namespace tts_host
