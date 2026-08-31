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
  std::optional<std::filesystem::path> config_path_override;
  std::optional<std::filesystem::path> data_dir_override;
  std::optional<std::string> synthesize_text;
  bool use_stdin_text = false;
  bool use_clipboard_text = false;
  std::optional<std::filesystem::path> output_path;
  bool play_audio = false;
  std::optional<std::filesystem::path> runner_path_override;
  std::optional<std::string> model_id;
  bool report_stats = false;
};

struct ResolvedPaths {
  std::filesystem::path config_path;
  std::filesystem::path schema_path;
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
ConfigDocument load_config(const CliOptions &options, const std::filesystem::path &argv0);

}  // namespace tts_host
