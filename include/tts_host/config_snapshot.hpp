#pragma once

#include <memory>
#include <mutex>

#include "tts_host/config_loader.hpp"
#include "tts_host/playback_controller.hpp"

namespace tts_host {

// The tray thread owns the mutable ConfigDocument that Settings edits; the
// speech worker must never read it. Instead each request takes current() once
// and keeps that immutable document for its whole lifetime, so a Settings
// change can neither race the worker nor give one utterance mixed values
// (docs/design/architecture.md#live-reload). publish() swaps in a copy for
// the next request; in-flight holders keep theirs.
class ConfigSnapshot {
 public:
  explicit ConfigSnapshot(ConfigDocument initial);

  std::shared_ptr<const ConfigDocument> current() const;
  void publish(ConfigDocument document);

 private:
  mutable std::mutex mutex_;
  std::shared_ptr<const ConfigDocument> current_;
};

// Pushes the live playback settings a published document carries into the
// running controller: the default speed for the next utterance (the active
// utterance's speed belongs to Now Playing), the seek interval both seek
// buttons use from their next press, and the sentence pause and lookahead the
// pipeline reads at its next sentence boundary
// (docs/requirements/product.md#configuration-and-controls).
void apply_playback_settings(const ConfigDocument &document, PlaybackController &controller);

}  // namespace tts_host
