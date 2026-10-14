#include "tts_host/activity_log.hpp"

#include <chrono>
#include <ctime>
#include <deque>
#include <mutex>

namespace tts_host {
namespace {

std::mutex activity_mutex;
std::deque<ActivityEntry> activity;
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

}  // namespace

void record_activity(std::string text) {
  auto time = local_time_now();
  std::lock_guard lock(activity_mutex);
  if (activity.size() == kMaximumActivityEntries) activity.pop_front();
  activity.push_back({++last_sequence, std::move(time), std::move(text)});
}

std::vector<ActivityEntry> activity_since(std::uint64_t after_sequence) {
  std::lock_guard lock(activity_mutex);
  std::vector<ActivityEntry> entries;
  for (const auto &entry : activity) {
    if (entry.sequence > after_sequence) entries.push_back(entry);
  }
  return entries;
}

}  // namespace tts_host
