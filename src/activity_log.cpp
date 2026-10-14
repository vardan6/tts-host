#include "tts_host/activity_log.hpp"

#include <chrono>
#include <ctime>
#include <deque>
#include <mutex>

namespace tts_host {
namespace {

std::mutex activity_mutex;
struct StoredEntry {
  ActivityEntry entry;
  std::string transient_key;  // empty for ordinary entries
};

std::deque<StoredEntry> activity;
std::uint64_t last_sequence = 0;
constexpr std::size_t kMaximumActivityEntries = 200;

std::string local_time_now() {
  const std::time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
  std::tm local{};
#ifdef _WIN32
  localtime_s(&local, &now);
#else
  localtime_r(&now, &local);
#endif
  char buffer[16];
  std::strftime(buffer, sizeof(buffer), "%H:%M:%S", &local);
  return buffer;
}

void append_entry(std::string time, std::string text, std::string transient_key) {
  if (activity.size() == kMaximumActivityEntries) activity.pop_front();
  activity.push_back({{++last_sequence, std::move(time), std::move(text)}, std::move(transient_key)});
}

}  // namespace

void record_activity(std::string text) {
  auto time = local_time_now();
  std::lock_guard lock(activity_mutex);
  append_entry(std::move(time), std::move(text), {});
}

void record_transient_activity(const std::string &key, std::string text) {
  auto time = local_time_now();
  std::lock_guard lock(activity_mutex);
  if (!key.empty() && !activity.empty() && activity.back().transient_key == key) {
    // A fresh sequence keeps the record ordered and lets readers polling
    // activity_since() see the update.
    activity.back().entry = {++last_sequence, std::move(time), std::move(text)};
    return;
  }
  append_entry(std::move(time), std::move(text), key);
}

std::vector<ActivityEntry> activity_since(std::uint64_t after_sequence) {
  std::lock_guard lock(activity_mutex);
  std::vector<ActivityEntry> entries;
  for (const auto &stored : activity) {
    if (stored.entry.sequence > after_sequence) entries.push_back(stored.entry);
  }
  return entries;
}

}  // namespace tts_host
