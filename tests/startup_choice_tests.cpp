#include "tts_host/startup_choice.hpp"

#include <filesystem>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const char *message) {
  if (!condition) throw std::runtime_error(message);
}

}  // namespace

// docs/design/architecture.md#login-startup: ask once per data directory,
// pre-ticked from startup.launchOnLogin; the marker is written whatever the
// answer, and only yes registers.
int main() {
  const auto root = std::filesystem::temp_directory_path() /
                    ("tts-startup-choice-" + std::to_string(std::random_device{}()));
  struct Cleanup {
    std::filesystem::path path;
    ~Cleanup() { std::error_code ignored; std::filesystem::remove_all(path, ignored); }
  } cleanup{root};
  try {
    using tts_host::ask_startup_choice_once;
    int asks = 0;
    int registrations = 0;
    bool offered_default = false;
    auto answer = [&](bool value) {
      return [&, value](bool pre_ticked) { ++asks; offered_default = pre_ticked; return value; };
    };
    auto record = [&] { ++registrations; };

    // Missing data directory: asked with the configured default, then created.
    const auto accepted = root / "accepted";
    require(ask_startup_choice_once(accepted, true, answer(true), record), "first run did not ask");
    require(asks == 1 && offered_default, "question was not pre-ticked from launchOnLogin");
    require(registrations == 1, "yes did not register");
    require(std::filesystem::exists(accepted / tts_host::kStartupChoiceMarkerName),
            "yes did not write the marker");
    require(!ask_startup_choice_once(accepted, true, answer(true), record) && asks == 1 &&
                registrations == 1,
            "a recorded choice must never ask or register again");

    const auto declined = root / "declined";
    require(ask_startup_choice_once(declined, false, answer(false), record),
            "a new data directory did not ask");
    require(asks == 2 && !offered_default, "launchOnLogin false must offer an unticked box");
    require(registrations == 1, "no must not register");
    require(std::filesystem::exists(declined / tts_host::kStartupChoiceMarkerName),
            "no must still write the marker");

    // A registration failure surfaces, but the answer stays recorded.
    const auto failing = root / "failing";
    bool threw = false;
    try {
      ask_startup_choice_once(failing, true, answer(true),
                              [] { throw std::runtime_error("registry unavailable"); });
    } catch (const std::runtime_error &) {
      threw = true;
    }
    require(threw, "registration failure was swallowed");
    require(std::filesystem::exists(failing / tts_host::kStartupChoiceMarkerName),
            "registration failure must not ask again next launch");
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
  std::cout << "Startup choice tests passed\n";
  return 0;
}
