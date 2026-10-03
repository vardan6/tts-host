#include "tts_host/selection_hotkey_state.hpp"

#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char *message) {
  if (!condition) throw std::runtime_error(message);
}
}

int main() {
  try {
    tts_host::SelectionHotkeyState state;
    int registrations = 0;
    int releases = 0;
    bool available = true;
    const auto acquire = [&] { ++registrations; return available; };
    const auto release = [&] { ++releases; };
    state.restore(acquire);
    require(state.registered() && registrations == 1, "startup must register saved chord");
    state.restore(acquire);
    require(registrations == 1, "must not double-register an owned chord");
    state.toggle(acquire, release);
    require(!state.registered() && releases == 1, "suspend must release chord");
    state.release(release);  // Settings opens while suspended.
    state.restore(acquire); // Settings closes with a potentially edited binding.
    require(!state.registered() && registrations == 1 && releases == 1,
            "Settings must preserve user suspension");
    state.toggle(acquire, release);
    require(state.registered() && registrations == 2, "resume must register current chord");
    state.release(release); // Settings releases before probing the saved binding.
    available = false;
    state.restore(acquire);
    require(!state.registered() && registrations == 3 && releases == 2,
            "failed registration must leave menu unchecked");
    available = true;
    state.toggle(acquire, release);
    require(state.registered() && registrations == 4, "conflict must permit retry");
    state.release(release);
    state.release(release);
    require(!state.registered() && releases == 3, "shutdown must release once");

    tts_host::SelectionHotkeyState restarted;
    restarted.restore(acquire);
    require(restarted.registered(), "runtime suspension must not survive restart");
    tts_host::SelectionHotkeyState unconfigured;
    available = false; // Empty/invalid binding is rejected by registration adapter.
    unconfigured.restore(acquire);
    unconfigured.release(release);
    require(!unconfigured.registered() && releases == 3,
            "absent binding must not own or release a chord");
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
