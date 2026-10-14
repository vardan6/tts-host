#pragma once

#include <cstdint>
#include <filesystem>
#include <mutex>
#include <vector>

namespace tts_host {

// Append-only per-utterance source PCM. Every byte is written to the spool;
// the first 64 MiB are also cached in RAM for common short-utterance rewinds.
class AudioHistory {
 public:
  static constexpr std::uint64_t kMemoryCacheLimit = 64ull * 1024 * 1024;
  static constexpr std::uint64_t kSpoolLimit = 1024ull * 1024 * 1024;

  explicit AudioHistory(std::uint64_t spool_limit = kSpoolLimit,
                       std::uint64_t cache_limit = kMemoryCacheLimit);
  ~AudioHistory();
  AudioHistory(const AudioHistory &) = delete;
  AudioHistory &operator=(const AudioHistory &) = delete;

  std::uint64_t append(const std::vector<std::uint8_t> &pcm);
  std::vector<std::uint8_t> read(std::uint64_t offset, std::uint64_t length) const;
  std::uint64_t size() const;
  static void clean_abandoned_spools();

 private:
  mutable std::mutex mutex_;
  std::filesystem::path path_;
  std::vector<std::uint8_t> cache_;
  std::uint64_t spool_limit_;
  std::uint64_t cache_limit_;
  std::uint64_t size_ = 0;
  bool removed_ = false;
};

}  // namespace tts_host
