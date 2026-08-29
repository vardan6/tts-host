#include "tts_host/config_loader.hpp"

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <vector>

#include <nlohmann/json-schema.hpp>

namespace tts_host {
namespace {

using json = nlohmann::json;
using nlohmann::json_schema::json_validator;

class ValidationErrorHandler final : public nlohmann::json_schema::basic_error_handler {
 public:
  void error(const json::json_pointer &pointer, const json &instance,
             const std::string &message) override {
    basic_error_handler::error(pointer, instance, message);
    issues.emplace_back(ConfigError{pointer.to_string(), message});
  }

  std::vector<ConfigError> issues;
};

[[noreturn]] void throw_config_error(std::string json_path, std::string message) {
  throw std::runtime_error("Configuration error at " + std::move(json_path) + ": " +
                           std::move(message));
}

json load_json_file(const std::filesystem::path &path, std::string_view label) {
  std::ifstream input(path);
  if (!input) {
    throw std::runtime_error("Unable to open " + std::string(label) + ": " + path.string());
  }

  try {
    return json::parse(input);
  } catch (const json::parse_error &error) {
    throw std::runtime_error("Invalid " + std::string(label) + " JSON in " + path.string() +
                             ": " + error.what());
  }
}

std::filesystem::path executable_dir(const std::filesystem::path &argv0) {
  if (argv0.empty()) {
    return std::filesystem::current_path();
  }

  std::error_code ec;
  const auto absolute = std::filesystem::absolute(argv0, ec);
  if (ec) {
    return std::filesystem::current_path();
  }
  return absolute.parent_path();
}

std::filesystem::path home_dir() {
#ifdef _WIN32
  if (const char *profile = std::getenv("USERPROFILE")) {
    return std::filesystem::path(profile);
  }
  if (const char *drive = std::getenv("HOMEDRIVE")) {
    if (const char *path = std::getenv("HOMEPATH")) {
      return std::filesystem::path(std::string(drive) + path);
    }
  }
#else
  if (const char *home = std::getenv("HOME")) {
    return std::filesystem::path(home);
  }
#endif
  throw std::runtime_error("Unable to resolve the current user's home directory");
}

std::filesystem::path installed_config_dir() {
#ifdef _WIN32
  if (const char *local_app_data = std::getenv("LOCALAPPDATA")) {
    return std::filesystem::path(local_app_data) / "TTS Host";
  }
  return home_dir() / "AppData" / "Local" / "TTS Host";
#elif defined(__APPLE__)
  return home_dir() / "Library" / "Application Support" / "TTS Host";
#else
  if (const char *xdg_config_home = std::getenv("XDG_CONFIG_HOME")) {
    return std::filesystem::path(xdg_config_home) / "tts-host";
  }
  return home_dir() / ".config" / "tts-host";
#endif
}

std::filesystem::path resolve_data_dir(const CliOptions &options,
                                       const std::filesystem::path &argv0) {
  if (options.data_dir_override.has_value()) {
    return std::filesystem::absolute(*options.data_dir_override);
  }

  const auto exe_dir = executable_dir(argv0);
  if (std::filesystem::exists(exe_dir / "portable.marker")) {
    return exe_dir;
  }

  return installed_config_dir();
}

std::filesystem::path resolve_config_path(const CliOptions &options,
                                          const std::filesystem::path &argv0) {
  if (options.config_path_override.has_value()) {
    return std::filesystem::absolute(*options.config_path_override);
  }

  return resolve_data_dir(options, argv0) / "config.json";
}

std::filesystem::path resolve_schema_path(const json &config,
                                          const std::filesystem::path &config_path) {
  const auto schema_it = config.find("$schema");
  if (schema_it != config.end() && schema_it->is_string()) {
    const auto candidate = std::filesystem::path(schema_it->get<std::string>());
    if (candidate.is_absolute()) {
      return candidate;
    }
    return config_path.parent_path() / candidate;
  }

  return config_path.parent_path() / "schemas" / "config.schema.json";
}

void enforce_supported_schema_version(const json &config) {
  const auto schema_version = config.find("schemaVersion");
  if (schema_version != config.end() && schema_version->is_number_integer() &&
      schema_version->get<int>() != 1) {
    throw_config_error("/schemaVersion",
                       "unsupported schema version " +
                           std::to_string(schema_version->get<int>()) + "; expected 1");
  }
}

void validate_against_schema(const json &schema, const json &config) {
  json_validator validator(nullptr, nlohmann::json_schema::default_string_format_check);
  validator.set_root_schema(schema);

  ValidationErrorHandler error_handler;
  validator.validate(config, error_handler);
  if (!error_handler.issues.empty()) {
    const auto &issue = error_handler.issues.front();
    throw_config_error(issue.json_path.empty() ? "/" : issue.json_path, issue.message);
  }
}

}  // namespace

std::optional<CliOptions> parse_cli(int argc, char **argv, std::string &error_message) {
  CliOptions options;

  for (int index = 1; index < argc; ++index) {
    const std::string_view argument(argv[index]);
    if (argument == "--headless") {
      options.headless = true;
      continue;
    }

    if (argument == "--list-models") {
      options.list_models = true;
      continue;
    }

    if (argument == "--config") {
      if (index + 1 >= argc) {
        error_message = "--config requires a path";
        return std::nullopt;
      }
      options.config_path_override = argv[++index];
      continue;
    }

    if (argument == "--data-dir") {
      if (index + 1 >= argc) {
        error_message = "--data-dir requires a path";
        return std::nullopt;
      }
      options.data_dir_override = argv[++index];
      continue;
    }

    if (argument == "--synthesize") {
      if (index + 1 >= argc) {
        error_message = "--synthesize requires text";
        return std::nullopt;
      }
      options.synthesize_text = argv[++index];
      continue;
    }

    if (argument == "--out") {
      if (index + 1 >= argc) {
        error_message = "--out requires a path";
        return std::nullopt;
      }
      options.output_path = argv[++index];
      continue;
    }

    if (argument == "--runner") {
      if (index + 1 >= argc) {
        error_message = "--runner requires a path";
        return std::nullopt;
      }
      options.runner_path_override = argv[++index];
      continue;
    }

    if (argument == "--model") {
      if (index + 1 >= argc) {
        error_message = "--model requires a model id";
        return std::nullopt;
      }
      options.model_id = argv[++index];
      continue;
    }

    if (argument == "--help" || argument == "-h") {
      error_message =
          "Usage: tts-host --headless [--list-models] [--config <path>] [--data-dir <path>]\n"
          "                 [--synthesize <text> --out <path.wav> [--runner <path> | --model <id>]]\n"
          "  --headless          start without UI\n"
          "  --list-models       print discovered and unsupported model packages\n"
          "  --config <path>     load a specific config.json\n"
          "  --data-dir <path>   override the portable or installed data directory\n"
          "  --synthesize <text> synthesize text through a runner process\n"
          "  --out <path>        WAV file to write the synthesized audio to\n"
          "  --runner <path>     runner executable to launch (default: the in-repo stub runner)\n"
          "  --model <id>        registry model id; selects the runner for its declared engine";
      return std::nullopt;
    }

    error_message = "Unknown argument: " + std::string(argument);
    return std::nullopt;
  }

  if (!options.headless) {
    error_message = "Only --headless is implemented in this slice";
    return std::nullopt;
  }

  if (options.synthesize_text.has_value() != options.output_path.has_value()) {
    error_message = "--synthesize and --out must be used together";
    return std::nullopt;
  }

  if (options.runner_path_override.has_value() && options.model_id.has_value()) {
    error_message = "--runner and --model are mutually exclusive";
    return std::nullopt;
  }

  if (options.model_id.has_value() && !options.synthesize_text.has_value()) {
    error_message = "--model requires --synthesize and --out";
    return std::nullopt;
  }

  return options;
}

ConfigDocument load_config(const CliOptions &options, const std::filesystem::path &argv0) {
  const auto config_path = resolve_config_path(options, argv0);
  const auto config = load_json_file(config_path, "config");
  enforce_supported_schema_version(config);

  const auto schema_path = resolve_schema_path(config, config_path);
  const auto schema = load_json_file(schema_path, "schema");
  validate_against_schema(schema, config);

  return ConfigDocument{config, ResolvedPaths{config_path, schema_path}};
}

}  // namespace tts_host
