#include "tts_host/audio_history.hpp"
#include "tts_host/path_encoding.hpp"
#include <chrono>

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
    const auto unrelated = std::filesystem::temp_directory_path() /
        tts_host::path_from_utf8("tts-unrelated-\xd5\x80\xd5\xa1\xd5\xb5-\xd0\xa0\xd1\x83\xd1\x81-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directory(unrelated);
    struct Cleanup {
      std::filesystem::path path;
      ~Cleanup() { std::error_code error; std::filesystem::remove_all(path, error); }
    } cleanup{unrelated};
    tts_host::AudioHistory::clean_abandoned_spools();
    require(std::filesystem::exists(unrelated), "spool cleanup removed an unrelated Unicode directory");
    retains_pcm_and_reports_spool_exhaustion();
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
  return 0;
}
