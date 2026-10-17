#pragma once

#include <filesystem>
#include <optional>
#include <string>

#include <nlohmann/json.hpp>

namespace tts_host {

struct CliOptions {
  bool headless = false;
  bool settings_window = false;
  bool list_models = false;
  bool list_catalogue = false;
  std::optional<std::filesystem::path> import_model_path;
  std::optional<std::string> download_model_id;
  std::optional<std::filesystem::path> download_destination_dir;
  std::optional<std::filesystem::path> config_path_override;
  std::optional<std::filesystem::path> data_dir_override;
  std::optional<std::string> synthesize_text;
  bool use_stdin_text = false;
  bool use_clipboard_text = false;
  std::optional<std::filesystem::path> output_path;
  bool play_audio = false;
  std::optional<std::filesystem::path> runner_path_override;
  std::optional<std::string> model_id;
  std::optional<std::string> language;
  bool report_stats = false;
};

struct ResolvedPaths {
  std::filesystem::path config_path;
  std::filesystem::path schema_path;
  // Mutable Host state such as the startup-choice marker
  // (docs/design/architecture.md#data-locations).
  std::filesystem::path data_dir;
  // The application's own read-only models/ (ADR 0010 bundled tier); empty
  // when there is none, as in portable mode where models/ is the data dir's.
  std::filesystem::path bundled_models_dir;
};

struct ConfigDocument {
  nlohmann::json value;
  ResolvedPaths paths;
};

struct ConfigError {
  std::string json_path;
  std::string message;
};

std::optional<CliOptions> parse_cli(int argc, char **argv, std::string &error_message);
void validate_config(const ConfigDocument &document);
ConfigDocument load_config(const CliOptions &options, const std::filesystem::path &argv0);
// Re-reads paths.config_path and validates it against the schema it names,
// keeping the startup data directory (a restart-only location).
ConfigDocument reload_config(const ResolvedPaths &paths);

}  // namespace tts_host
