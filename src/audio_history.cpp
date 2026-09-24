#include "tts_host/audio_history.hpp"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <random>
#include <stdexcept>

namespace tts_host {
namespace {
constexpr char kPrefix[] = "tts-host-utterance-";
constexpr auto kAbandonedAge = std::chrono::hours(24);

std::filesystem::path new_spool_path() {
  std::random_device random;
  for (int attempt = 0; attempt < 16; ++attempt) {
    const auto nonce = (static_cast<std::uint64_t>(random()) << 32) | random();
    const auto path = std::filesystem::temp_directory_path() /
                      (std::string(kPrefix) + std::to_string(nonce) + ".pcm");
    if (!std::filesystem::exists(path)) return path;
  }
  throw std::runtime_error("could not allocate a temporary audio-history spool");
}
}  // namespace

AudioHistory::AudioHistory(std::uint64_t spool_limit, std::uint64_t cache_limit)
    : path_(new_spool_path()), spool_limit_(spool_limit), cache_limit_(cache_limit) {
  std::ofstream file(path_, std::ios::binary | std::ios::out);
  if (!file) throw std::runtime_error("could not create audio-history spool in the temp directory");
}

AudioHistory::~AudioHistory() {
  std::lock_guard lock(mutex_);
  if (!removed_) {
    std::error_code error;
    std::filesystem::remove(path_, error);
    removed_ = true;
  }
}

std::uint64_t AudioHistory::append(const std::vector<std::uint8_t> &pcm) {
  std::lock_guard lock(mutex_);
  if (removed_) throw std::runtime_error("audio history has already been closed");
  if (size_ > spool_limit_ || pcm.size() > spool_limit_ - size_) {
    throw std::runtime_error(
        "generated audio exceeded the 1 GiB retention limit; stop this utterance or shorten the text");
  }
  const auto offset = size_;
  std::ofstream file(path_, std::ios::binary | std::ios::app);
  if (!file) throw std::runtime_error("could not open the audio-history spool; check free disk space");
  file.write(reinterpret_cast<const char *>(pcm.data()), static_cast<std::streamsize>(pcm.size()));
  file.flush();
  if (!file) {
    throw std::runtime_error(
        "audio-history storage is full or unavailable; stop this utterance and free disk space");
  }
  const auto remaining = cache_.size() < cache_limit_ ? cache_limit_ - cache_.size() : 0;
  const auto cached = std::min<std::uint64_t>(remaining, pcm.size());
  cache_.insert(cache_.end(), pcm.begin(), pcm.begin() + static_cast<std::ptrdiff_t>(cached));
  size_ += pcm.size();
  return offset;
}

std::vector<std::uint8_t> AudioHistory::read(std::uint64_t offset, std::uint64_t length) const {
  std::lock_guard lock(mutex_);
  if (removed_ || offset > size_ || length > size_ - offset) {
    throw std::out_of_range("requested audio range is not generated");
  }
  if (length > std::vector<std::uint8_t>().max_size()) {
    throw std::length_error("requested audio range is too large to load");
  }
  std::vector<std::uint8_t> result(static_cast<std::size_t>(length));
  std::uint64_t copied = 0;
  if (offset < cache_.size()) {
    copied = std::min<std::uint64_t>(length, cache_.size() - offset);
    std::copy_n(cache_.begin() + static_cast<std::ptrdiff_t>(offset),
                static_cast<std::size_t>(copied), result.begin());
  }
  if (copied < length) {
    std::ifstream file(path_, std::ios::binary);
    if (!file) throw std::runtime_error("audio-history spool is unavailable");
    file.seekg(static_cast<std::streamoff>(offset + copied));
    file.read(reinterpret_cast<char *>(result.data() + copied),
              static_cast<std::streamsize>(length - copied));
    if (!file) throw std::runtime_error("could not read retained audio from the spool");
  }
  return result;
}

std::uint64_t AudioHistory::size() const {
  std::lock_guard lock(mutex_);
  return size_;
}

void AudioHistory::clean_abandoned_spools() {
  std::error_code error;
  const auto directory = std::filesystem::temp_directory_path(error);
  if (error) return;
  const auto cutoff = std::filesystem::file_time_type::clock::now() - kAbandonedAge;
  for (std::filesystem::directory_iterator it(directory, error), end; !error && it != end;
       it.increment(error)) {
    const auto name = it->path().filename().string();
    if (!name.starts_with(kPrefix) || it->path().extension() != ".pcm") continue;
    const auto modified = it->last_write_time(error);
    if (error) {
      error.clear();
      continue;
    }
    if (modified < cutoff) std::filesystem::remove(it->path(), error);
    error.clear();
  }
}

}  // namespace tts_host
