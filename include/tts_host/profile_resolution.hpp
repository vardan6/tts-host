#pragma once

#include <chrono>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "tts_host/config_loader.hpp"
#include "tts_host/runner_selection.hpp"

namespace tts_host {

// One configured profile; the only reader of its keys and their defaults
// (docs/design/architecture.md#profile-resolution).
struct ProfileView {
  std::string name;
  std::string model;
  std::optional<std::string> fallback_profile;
  // loadPolicy "eager"; the default is "onDemand".
  bool eager = false;
  std::optional<std::chrono::seconds> idle_unload;
};

// Throws when `name` is not a configured profile.
ProfileView profile_view(const ConfigDocument &config, const std::string &name);

// Every configured profile, in configuration-key order; none when the
// document has no profiles.
std::vector<ProfileView> profile_views(const ConfigDocument &config);

// Turns a model id into its runner selection; resolve_model_runner_selection
// in the Host. Throws when the model or its runner is unavailable.
using ModelResolver = std::function<RunnerSelection(const std::string &model_id)>;

// What one speech request asks for. A --runner override wins over --model,
// which wins over the language's profile.
struct SelectionRequest {
  std::optional<std::filesystem::path> runner_override;
  std::optional<std::string> model_id;
  // An explicit language tag, else the spoken text's script picks one.
  std::optional<std::string> language;
  std::string spoken_text;
};

struct ProfileSelection {
  RunnerSelection selection;
  // The profile that chose the model; empty for --model and --runner.
  std::string profile_name;
  // The fallback still available if the runner rejects its load; empty when
  // none is configured or the fallback was already taken.
  std::string fallback_profile;
  // "Language: ..., profile ..., model ..." for the request's output; empty
  // when no profile was involved.
  std::string language_message;
  // Why a fallback replaced the profile's model, for the output and the
  // Activity log.
  std::vector<std::string> fallback_messages;
};

// Language to languageDefaults profile to model, with `fallbackProfile` when
// that model cannot be resolved (docs/design/architecture.md#configuration).
// A language without a languageDefaults entry speaks with `default_runner`
// unless it was explicit, which is an error.
ProfileSelection resolve_selection(const SelectionRequest &request, const ConfigDocument &config,
                                   const std::filesystem::path &default_runner, const ModelResolver &resolve_model);

// Replaces `profile_name`'s model, unavailable for `reason`, with its
// fallbackProfile's. Throws when the fallback is invalid or unavailable too.
ProfileSelection resolve_fallback(const std::string &profile_name, const std::string &reason,
                                  const ConfigDocument &config, const ModelResolver &resolve_model);

// The selection to load before the first read: an eager language default,
// else the first eager profile in configuration-key order; none when no
// profile is eager.
std::optional<RunnerSelection> eager_selection(const ConfigDocument &config, const ModelResolver &resolve_model);

}  // namespace tts_host
