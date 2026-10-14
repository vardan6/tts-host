#include "tts_host/config_watch.hpp"

#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

using Outcome = tts_host::ConfigReload::Outcome;

void require(bool condition, const std::string &message) {
  if (!condition) throw std::runtime_error(message);
}

std::string read(const std::filesystem::path &path) {
  std::ifstream input(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

struct Directory {
  std::filesystem::path path;
  ~Directory() { std::error_code ignored; std::filesystem::remove_all(path, ignored); }
};

// Each write gets a distinct modification time so the test does not depend on
// the file system's timestamp resolution.
void write(const std::filesystem::path &path, const std::string &text) {
  static int generation = 0;
  {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.exceptions(std::ios::failbit | std::ios::badbit);
    output << text;
  }
  std::filesystem::last_write_time(
      path, std::filesystem::file_time_type::clock::now() + std::chrono::seconds(++generation));
}

// One poll notices the file; the next, seeing it unchanged, judges it.
tts_host::ConfigReload settle(tts_host::ConfigFileWatcher &watcher, tts_host::ConfigDocument &document) {
  const auto first = watcher.poll(document);
  require(first.outcome == Outcome::Unchanged, "a just-changed file was judged before it settled");
  return watcher.poll(document);
}

}  // namespace

int main(int argc, char **argv) {
  try {
    require(argc == 3, "expected config and schema paths");
    Directory directory{std::filesystem::temp_directory_path() /
        ("tts-config-watch-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))};
    std::filesystem::create_directories(directory.path);
    const auto path = directory.path / "config.json";
    auto original = nlohmann::json::parse(read(argv[1]));
    original["$schema"] = std::filesystem::absolute(argv[2]).generic_string();
    write(path, original.dump(2));
    auto document = tts_host::reload_config({path, {}, directory.path});
    tts_host::ConfigFileWatcher watcher;

    // The startup file and a reformatted save of the same content (what
    // Settings writes) are not changes.
    require(settle(watcher, document).outcome == Outcome::Unchanged, "startup file reported as a change");
    write(path, original.dump(4));
    require(settle(watcher, document).outcome == Outcome::Unchanged, "same content reported as a change");
    require(watcher.poll(document).outcome == Outcome::Unchanged, "settled file judged twice");

    // A save still in progress is not read; the settled version is applied.
    auto edited = original;
    edited["audio"]["playbackSkipSeconds"] = 9;
    write(path, "{ \"audio\": ");
    require(watcher.poll(document).outcome == Outcome::Unchanged, "partial save was judged");
    edited["server"]["port"] = 7999;
    write(path, edited.dump(2));
    auto reload = settle(watcher, document);
    require(reload.outcome == Outcome::Applied, "valid external edit was not applied");
    require(document.value == edited, "applied edit did not replace the document");
    require(reload.restart_required == std::vector<std::string>{"/server/port"},
            "server.port was not reported as restart-only");

    // Invalid JSON and schema violations keep the last valid document, report
    // the failing location, and are reported once.
    const auto valid = document.value;
    write(path, "{ not json");
    reload = settle(watcher, document);
    require(reload.outcome == Outcome::Rejected && reload.error.find("Invalid config JSON") != std::string::npos,
            "invalid JSON was not rejected: " + reload.error);
    require(document.value == valid, "invalid JSON replaced the last valid document");
    require(watcher.poll(document).outcome == Outcome::Unchanged, "invalid edit reported twice");

    auto out_of_range = valid;
    out_of_range["audio"]["playbackSkipSeconds"] = -1;
    write(path, out_of_range.dump(2));
    reload = settle(watcher, document);
    require(reload.outcome == Outcome::Rejected &&
                reload.error.find("/audio/playbackSkipSeconds") != std::string::npos,
            "schema violation did not name its JSON path: " + reload.error);
    require(document.value == valid, "schema-invalid edit replaced the last valid document");

    // A missing file keeps the document; restoring it with new values applies.
    std::filesystem::remove(path);
    reload = settle(watcher, document);
    require(reload.outcome == Outcome::Rejected && document.value == valid, "missing file not handled");
    edited["audio"]["playbackSkipSeconds"] = 11;
    write(path, edited.dump(2));
    reload = settle(watcher, document);
    require(reload.outcome == Outcome::Applied && reload.restart_required.empty() &&
                document.value.at("audio").at("playbackSkipSeconds") == 11,
            "restored file was not applied");

    std::cout << "Configuration watch regressions passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
