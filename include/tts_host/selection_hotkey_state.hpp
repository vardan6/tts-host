#pragma once

#include <functional>

namespace tts_host {

// Runtime registration and user intent are separate: Settings temporarily
// releases the chord, and an OS conflict must remain retryable.
class SelectionHotkeyState {
 public:
  bool registered() const { return registered_; }

  void restore(const std::function<bool()> &register_hotkey) {
    if (!suspended_ && !registered_) registered_ = register_hotkey();
  }

  void release(const std::function<void()> &unregister_hotkey) {
    if (registered_) unregister_hotkey();
    registered_ = false;
  }

  void toggle(const std::function<bool()> &register_hotkey,
              const std::function<void()> &unregister_hotkey) {
    if (registered_) {
      release(unregister_hotkey);
      suspended_ = true;
    } else {
      suspended_ = false;
      restore(register_hotkey);
    }
  }

 private:
  bool registered_ = false;
  bool suspended_ = false;
};

}  // namespace tts_host
