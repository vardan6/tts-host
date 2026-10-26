#include "tts_host/profile_resolution.hpp"

#include <stdexcept>
#include <utility>

#include "tts_host/language_selection.hpp"

namespace tts_host {

namespace {

ProfileView read_profile(const std::string &name, const nlohmann::json &profile) {
  ProfileView view;
  view.name = name;
  view.model = profile.at("model").get<std::string>();
  if (profile.contains("fallbackProfile")) view.fallback_profile = profile.at("fallbackProfile").get<std::string>();
  view.eager = profile.value("loadPolicy", "onDemand") == "eager";
  if (profile.contains("idleUnloadSeconds"))
    view.idle_unload = std::chrono::seconds(profile.at("idleUnloadSeconds").get<int>());
  return view;
}

RunnerSelection profile_selection(const ProfileView &profile, const ModelResolver &resolve_model) {
  auto selected = resolve_model(profile.model);
  selected.idle_override = profile.idle_unload;
  return selected;
}

}  // namespace

ProfileView profile_view(const ConfigDocument &config, const std::string &name) {
  return read_profile(name, config.value.at("profiles").at(name));
}

std::vector<ProfileView> profile_views(const ConfigDocument &config) {
  std::vector<ProfileView> views;
  if (!config.value.contains("profiles")) return views;
  for (const auto &entry : config.value.at("profiles").items()) views.push_back(read_profile(entry.key(), entry.value()));
  return views;
}

ProfileSelection resolve_fallback(const std::string &profile_name, const std::string &reason,
                                  const ConfigDocument &config, const ModelResolver &resolve_model) {
  const auto &profiles = config.value.at("profiles");
  const auto fallback_name = profile_view(config, profile_name).fallback_profile.value();
  if (fallback_name == profile_name) {
    throw std::runtime_error("profiles." + profile_name + ".fallbackProfile must name a different profile");
  }
  if (!profiles.contains(fallback_name)) {
    throw std::runtime_error("profiles." + profile_name + ".fallbackProfile names unknown profile: " + fallback_name);
  }

  const auto fallback = profile_view(config, fallback_name);
  ProfileSelection result;
  result.profile_name = profile_name;
  try {
    result.selection = profile_selection(fallback, resolve_model);
  } catch (const std::exception &fallback_error) {
    throw std::runtime_error("Profile " + profile_name + " is unavailable (" + reason + "); fallback profile " +
                             fallback_name + " is also unavailable (" + fallback_error.what() + ")");
  }
  result.fallback_messages.push_back("Profile " + profile_name + " is unavailable (" + reason +
                                     "); using fallback profile " + fallback_name + ", model " + fallback.model);
  return result;
}

ProfileSelection resolve_selection(const SelectionRequest &request, const ConfigDocument &config,
                                   const std::filesystem::path &default_runner, const ModelResolver &resolve_model) {
  ProfileSelection result;
  if (request.runner_override.has_value()) {
    result.selection.runner_path = *request.runner_override;
    return result;
  }
  if (request.model_id.has_value()) {
    result.selection = resolve_model(*request.model_id);
    return result;
  }

  const auto &language_defaults = config.value.at("languageDefaults");
  std::vector<std::string> configured_languages;
  for (const auto &entry : language_defaults.items()) configured_languages.push_back(entry.key());
  const auto language = select_request_language(request.language, request.spoken_text, configured_languages);
  if (!language_defaults.contains(language.tag)) {
    // An explicit tag the user typed is a mistake worth naming; a fallback to
    // English on a config that simply has no "en" entry is not, and keeps the
    // pre-profile stub-runner behavior.
    if (language.source == LanguageSource::Explicit) {
      throw std::runtime_error("languageDefaults has no entry for language: " + language.tag);
    }
    result.selection.runner_path = default_runner;
    return result;
  }
  const auto profile_name = language_defaults.at(language.tag).get<std::string>();
  if (!config.value.at("profiles").contains(profile_name)) {
    throw std::runtime_error("languageDefaults." + language.tag + " names unknown profile: " + profile_name);
  }
  const auto profile = profile_view(config, profile_name);
  result.language_message = "Language: " + language.tag + " (" + describe_language_source(language.source) +
                            "), profile " + profile_name + ", model " + profile.model;
  try {
    result.selection = profile_selection(profile, resolve_model);
    result.profile_name = profile_name;
    result.fallback_profile = profile.fallback_profile.value_or(std::string());
  } catch (const std::exception &primary_error) {
    if (!profile.fallback_profile) {
      throw;
    }
    auto fallback = resolve_fallback(profile_name, primary_error.what(), config, resolve_model);
    result.selection = std::move(fallback.selection);
    result.profile_name = profile_name;
    result.fallback_messages = std::move(fallback.fallback_messages);
  }
  return result;
}

std::optional<RunnerSelection> eager_selection(const ConfigDocument &config, const ModelResolver &resolve_model) {
  // The session manager has one resident slot. Prefer an eager language
  // default, then the first eager profile in deterministic configuration-key
  // order.
  const auto &profiles = config.value.at("profiles");
  for (const auto &entry : config.value.at("languageDefaults").items()) {
    const auto name = entry.value().get<std::string>();
    if (profiles.contains(name) && profile_view(config, name).eager) {
      return profile_selection(profile_view(config, name), resolve_model);
    }
  }
  for (const auto &profile : profile_views(config)) {
    if (profile.eager) return profile_selection(profile, resolve_model);
  }
  return std::nullopt;
}

}  // namespace tts_host
