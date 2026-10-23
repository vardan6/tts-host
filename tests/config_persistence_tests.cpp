#include "tts_host/config_persistence.hpp"

#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char *message) {
  if (!condition) throw std::runtime_error(message);
}
std::string read(const std::filesystem::path &path) {
  std::ifstream input(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}
void write(const std::filesystem::path &path, const std::string &text) {
  std::ofstream output(path, std::ios::binary);
  output.exceptions(std::ios::failbit | std::ios::badbit);
  output << text;
  output.close();
}
struct Directory {
  std::filesystem::path path;
  ~Directory() { std::error_code ignored; std::filesystem::remove_all(path, ignored); }
};
}

int main(int argc, char **argv) {
  try {
    require(argc == 3, "expected config and schema paths");
    Directory directory{std::filesystem::temp_directory_path() /
        ("tts-config-save-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))};
    std::filesystem::create_directories(directory.path);
    const auto path = directory.path / "config.json";
    auto original = nlohmann::json::parse(read(argv[1]));
    original["$schema"] = std::filesystem::absolute(argv[2]).generic_string();
    const std::string original_bytes = original.dump(2) + '\n';
    const auto backup = std::filesystem::path(path.string() + ".last-valid");
    tts_host::ConfigDocument document{original, {path, std::filesystem::absolute(argv[2])}};
    // Settings selection must drive normal language/profile routing, survive
    // saving/reopening, and leave unrelated languages and named profiles alone.
    auto selected = tts_host::with_selected_model(document, "qwen3-tts-1.7b-q8", {"en"});
    const auto selected_profile = selected.value.at("languageDefaults").at("en").get<std::string>();
    require(selected.value.at("profiles").at(selected_profile).at("model") == "qwen3-tts-1.7b-q8",
            "Settings choice did not change the model used by English reads");
    require(selected.value.at("profiles").at(selected_profile).at("loadPolicy") == "eager",
            "warm-up could replace the Settings choice with an older eager default");
    for (const auto &entry : original.at("profiles").items())
      require(selected.value.at("profiles").at(entry.key()) == entry.value(), "named profile was changed");
    for (const auto &entry : original.at("languageDefaults").items())
      if (entry.key() != "en")
        require(selected.value.at("languageDefaults").at(entry.key()) == entry.value(),
                "unrelated language default was changed");
    write(path, original_bytes);
    tts_host::save_config_document(selected);
    require(nlohmann::json::parse(read(path)) == selected.value, "model choice did not survive save");
    selected = tts_host::with_selected_model(selected, "kokoro-en-v1", {"en"});
    require(selected.value.at("profiles").at(selected_profile).at("model") == "kokoro-en-v1",
            "the latest model selection did not win");
    require(document.value == original, "candidate creation mutated the saved configuration");
    document.value["audio"]["playbackSkipSeconds"] = 12;
    for (int failure = 0; failure < 5; ++failure) {
      write(path, original_bytes);
      write(backup, original_bytes);
      auto candidate = document;
      tts_host::ConfigSaveOperations operations;
      if (failure == 0) candidate.value["server"]["port"] = 99999;
      if (failure == 1) candidate.value["schemaVersion"] = 99;
      if (failure == 2 || failure == 3) {
        operations.write = [failure](const auto &target, const auto &contents) {
          if (failure == 2 || target.filename().string().find("last-valid") != std::string::npos) {
            write(target, "partial");
            throw std::runtime_error("injected disk-full write");
          }
          write(target, contents);
        };
      }
      if (failure == 4) {
        operations.replace = [path](const auto &source, const auto &target) {
          if (target == path) throw std::runtime_error("injected sharing violation");
          std::filesystem::remove(target);
          std::filesystem::rename(source, target);
        };
      }
      bool rejected = false;
      try { tts_host::save_config_document(candidate, operations); }
      catch (const std::exception &) { rejected = true; }
      require(rejected, "save failure was not reported");
      require(read(path) == original_bytes, "failed save damaged original bytes");
      tts_host::validate_config({nlohmann::json::parse(read(backup)), document.paths});
      for (const auto &entry : std::filesystem::directory_iterator(directory.path)) {
        require(entry.path().filename().string().find(".tmp-") == std::string::npos,
                "failed save leaked temporary file");
      }
    }
    // Successful replacement keeps the previous version, then advances it.
    tts_host::save_config_document(document);
    require(nlohmann::json::parse(read(path)) == document.value, "new config not saved");
    require(nlohmann::json::parse(read(backup)) == original, "previous version not retained");
    const auto first_saved = document.value;
    document.value["audio"]["playbackSkipSeconds"] = 9;
    tts_host::save_config_document(document);
    require(nlohmann::json::parse(read(backup)) == first_saved, "backup did not advance");
    write(path, "invalid external edit");
    tts_host::save_config_document(document);
    require(nlohmann::json::parse(read(backup)) == first_saved, "invalid disk edit replaced valid backup");
    std::cout << "Configuration persistence regressions passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
