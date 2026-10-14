#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace tts_host {

// One user-visible event: a failure, a recovery, or a notable result.
struct ActivityEntry {
  std::uint64_t sequence;
  std::string time;  // local HH:MM:SS when recorded
  std::string text;
};

// Process-wide record of the failures and important messages the Host shows
// anywhere (message boxes, Now Playing, background work), so the Settings
// activity log also shows what happened while it was closed
// (docs/design/architecture.md#desktop-integration). Thread-safe; keeps only
// the most recent entries. Never record selected or spoken text.
void record_activity(std::string text);

// Entries recorded after `after_sequence`, oldest first. Pass 0 for all.
std::vector<ActivityEntry> activity_since(std::uint64_t after_sequence);

}  // namespace tts_host
