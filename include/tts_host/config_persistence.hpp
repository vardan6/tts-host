#pragma once

#include <functional>
#include "tts_host/config_loader.hpp"

namespace tts_host {

// Optional I/O overrides support deterministic disk-failure regressions.
struct ConfigSaveOperations {
  std::function<void(const std::filesystem::path &, const std::string &)> write;
  std::function<void(const std::filesystem::path &, const std::filesystem::path &)> replace;
};

// Validate before writing, atomically replace beside the original, and retain
// the previous schema-valid document at <config-path>.last-valid.
// A failure leaves the destination intact; callers keep their last saved value.
void save_config_document(const ConfigDocument &document,
                          const ConfigSaveOperations &operations = {});

}  // namespace tts_host
