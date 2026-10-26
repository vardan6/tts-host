#include "tts_host/profile_resolution.hpp"

#include <iostream>
#include <set>
#include <stdexcept>
#include <string>

#include <nlohmann/json.hpp>

namespace {

using json = nlohmann::json;
using namespace std::chrono_literals;

void require(bool condition, const std::string &message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

// Model ids in `missing` are unavailable; the others resolve to a runner and
// a model file named after the id.
tts_host::ModelResolver resolver(std::set<std::string> missing = {"missing", "missing2"}) {
  return [missing](const std::string &model_id) {
    if (missing.count(model_id)) throw std::runtime_error("Unknown model id: " + model_id);
    tts_host::RunnerSelection selection;
    selection.runner_path = "/runners/" + model_id;
    selection.model_path = "/models/" + model_id;
    selection.model_id = model_id;
    return selection;
  };
}

tts_host::ConfigDocument config(json language_defaults = {{"en", "quality"}, {"ru", "russian"}}) {
  tts_host::ConfigDocument document;
  document.value = {
      {"profiles",
       {
           {"fast", {{"model", "en"}, {"loadPolicy", "eager"}}},
           {"quality", {{"model", "missing"}, {"loadPolicy", "onDemand"}, {"fallbackProfile", "fast"}}},
           {"gpu", {{"model", "gpu"}, {"loadPolicy", "onDemand"}, {"fallbackProfile", "fast"},
                    {"idleUnloadSeconds", 30}}},
           {"russian", {{"model", "ru"}, {"loadPolicy", "onDemand"}}},
           {"self", {{"model", "missing"}, {"loadPolicy", "onDemand"}, {"fallbackProfile", "self"}}},
           {"dangling", {{"model", "missing"}, {"loadPolicy", "onDemand"}, {"fallbackProfile", "nowhere"}}},
           {"chained", {{"model", "missing"}, {"loadPolicy", "onDemand"}, {"fallbackProfile", "also"}}},
           {"also", {{"model", "missing2"}, {"loadPolicy", "onDemand"}}},
           {"plain", {{"model", "missing"}, {"loadPolicy", "onDemand"}}},
       }},
      {"languageDefaults", std::move(language_defaults)},
  };
  return document;
}

tts_host::SelectionRequest speak(std::string text, std::optional<std::string> language = std::nullopt) {
  tts_host::SelectionRequest request;
  request.spoken_text = std::move(text);
  request.language = std::move(language);
  return request;
}

std::string failure(const tts_host::SelectionRequest &request, const tts_host::ConfigDocument &document) {
  try {
    tts_host::resolve_selection(request, document, "/runners/default", resolver());
  } catch (const std::exception &error) {
    return error.what();
  }
  throw std::runtime_error("resolution unexpectedly succeeded");
}

void test_profile_view_reads_keys_and_defaults() {
  const auto document = config();
  const auto gpu = tts_host::profile_view(document, "gpu");
  require(gpu.name == "gpu" && gpu.model == "gpu" && gpu.fallback_profile == "fast" && !gpu.eager &&
              gpu.idle_unload == 30s,
          "a profile view reads every key");
  const auto fast = tts_host::profile_view(document, "fast");
  require(fast.eager && !fast.fallback_profile && !fast.idle_unload, "absent keys take their defaults");
  auto unspecified = config();
  unspecified.value["profiles"]["fast"].erase("loadPolicy");
  require(!tts_host::profile_view(unspecified, "fast").eager, "loadPolicy defaults to onDemand");

  const auto views = tts_host::profile_views(document);
  require(views.size() == 9 && views.front().name == "also" && views.back().name == "self",
          "every profile is listed in key order");
  require(tts_host::profile_views({}).empty(), "a document without profiles lists none");
}

void test_overrides_skip_the_profile_chain() {
  auto request = speak("Hello");
  request.runner_override = "/custom/runner";
  request.model_id = "ru";
  const auto runner = tts_host::resolve_selection(request, config(), "/runners/default", resolver());
  require(runner.selection.runner_path == "/custom/runner" && !runner.selection.model_path,
          "--runner names only the runner");
  require(runner.profile_name.empty() && runner.fallback_profile.empty() && runner.language_message.empty(),
          "--runner involves no profile");

  request.runner_override.reset();
  const auto model = tts_host::resolve_selection(request, config(), "/runners/default", resolver());
  require(model.selection.model_id == "ru" && !model.selection.idle_override && model.profile_name.empty() &&
              model.fallback_profile.empty() && model.language_message.empty(),
          "--model resolves the id with no profile or fallback");
}

void test_language_picks_the_profile() {
  const auto russian = tts_host::resolve_selection(speak("Привет, мир"), config(), "/runners/default", resolver());
  require(russian.selection.model_id == "ru" && russian.profile_name == "russian",
          "Cyrillic text routes to the Russian profile");
  require(russian.language_message == "Language: ru (detected from text script), profile russian, model ru",
          "the language line names its source: " + russian.language_message);

  const auto explicit_language =
      tts_host::resolve_selection(speak("Hello", "ru"), config(), "/runners/default", resolver());
  require(explicit_language.language_message == "Language: ru (explicit), profile russian, model ru",
          "an explicit language wins over the script");

  const auto gpu =
      tts_host::resolve_selection(speak("Hello"), config({{"en", "gpu"}}), "/runners/default", resolver());
  require(gpu.selection.model_id == "gpu" && gpu.selection.idle_override == 30s,
          "the profile's idleUnloadSeconds overrides the registry's");
  require(gpu.profile_name == "gpu" && gpu.fallback_profile == "fast" && gpu.fallback_messages.empty(),
          "a resolved profile keeps its fallback for a rejected load");

  const auto unconfigured =
      tts_host::resolve_selection(speak("Hello"), config({{"ru", "russian"}}), "/runners/default", resolver());
  require(unconfigured.selection.runner_path == "/runners/default" && unconfigured.profile_name.empty(),
          "a language with no profile speaks with the default runner");
}

void test_unresolvable_model_takes_the_fallback() {
  const auto choice = tts_host::resolve_selection(speak("Hello"), config(), "/runners/default", resolver());
  require(choice.selection.model_id == "en" && choice.profile_name == "quality" && choice.fallback_profile.empty(),
          "an unresolvable model takes its fallback, and no second one remains");
  require(choice.language_message == "Language: en (detected from text script), profile quality, model missing",
          "the language line names the configured model: " + choice.language_message);
  require(choice.fallback_messages ==
              std::vector<std::string>{"Profile quality is unavailable (Unknown model id: missing); using "
                                       "fallback profile fast, model en"},
          "the fallback is reported for the caller to log");
}

void test_rejected_load_takes_the_fallback() {
  const auto choice = tts_host::resolve_fallback("gpu", "no usable GPU", config(), resolver());
  require(choice.selection.model_id == "en" && !choice.selection.idle_override,
          "the fallback profile's own idle policy applies");
  require(choice.profile_name == "gpu" && choice.fallback_profile.empty(), "a fallback is taken once");
  require(choice.fallback_messages == std::vector<std::string>{"Profile gpu is unavailable (no usable GPU); using "
                                                               "fallback profile fast, model en"},
          "the runner's reason is reported");
}

void test_resolution_errors() {
  require(failure(speak("Hello", "fr"), config()) == "languageDefaults has no entry for language: fr",
          "an explicit language without a profile is an error");
  require(failure(speak("Hello"), config({{"en", "nope"}})) == "languageDefaults.en names unknown profile: nope",
          "an unknown profile name is an error");
  require(failure(speak("Hello"), config({{"en", "plain"}})) == "Unknown model id: missing",
          "without a fallback the model's error stands");
  require(failure(speak("Hello"), config({{"en", "self"}})) ==
              "profiles.self.fallbackProfile must name a different profile",
          "a profile cannot fall back to itself");
  require(failure(speak("Hello"), config({{"en", "dangling"}})) ==
              "profiles.dangling.fallbackProfile names unknown profile: nowhere",
          "a fallback must name a profile");
  require(failure(speak("Hello"), config({{"en", "chained"}})) ==
              "Profile chained is unavailable (Unknown model id: missing); fallback profile also is also "
              "unavailable (Unknown model id: missing2)",
          "an unavailable fallback names both reasons");
}

void test_eager_selection() {
  auto document = config({{"en", "gpu"}, {"ru", "russian"}});
  const auto first_eager = tts_host::eager_selection(document, resolver());
  require(first_eager && first_eager->model_id == "en", "with no eager language default, the first eager profile");

  document.value["profiles"]["russian"]["loadPolicy"] = "eager";
  document.value["profiles"]["russian"]["idleUnloadSeconds"] = 0;
  const auto language_default = tts_host::eager_selection(document, resolver());
  require(language_default && language_default->model_id == "ru" && language_default->idle_override == 0s,
          "an eager language default wins, with its idle override");

  document.value["profiles"]["fast"]["loadPolicy"] = "onDemand";
  document.value["profiles"]["russian"]["loadPolicy"] = "onDemand";
  require(!tts_host::eager_selection(document, resolver()), "no eager profile, nothing to warm");
}

}  // namespace

int main() {
  try {
    test_profile_view_reads_keys_and_defaults();
    test_overrides_skip_the_profile_chain();
    test_language_picks_the_profile();
    test_unresolvable_model_takes_the_fallback();
    test_rejected_load_takes_the_fallback();
    test_resolution_errors();
    test_eager_selection();
  } catch (const std::exception &error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
  }
  std::cout << "profile resolution tests passed\n";
  return 0;
}
