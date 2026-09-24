#include "tts_host/audio_history.hpp"

#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void require(bool condition, const std::string &message) {
  if (!condition) throw std::runtime_error(message);
}

void retains_pcm_and_reports_spool_exhaustion() {
  tts_host::AudioHistory history(5, 2);
  const auto offset = history.append({1, 2, 3});
  require(offset == 0, "first retained PCM chunk should start at byte zero");
  history.append({4, 5});
  require(history.read(1, 4) == std::vector<std::uint8_t>({2, 3, 4, 5}),
          "read crossing the RAM cache boundary returned incorrect PCM");
  try {
    history.append({6});
  } catch (const std::runtime_error &error) {
    require(std::string(error.what()).find("retention limit") != std::string::npos,
            "spool exhaustion did not provide an actionable retention error");
    return;
  }
  throw std::runtime_error("audio spool accepted data beyond its configured limit");
}
}  // namespace

int main() {
  try {
    retains_pcm_and_reports_spool_exhaustion();
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
  return 0;
}
