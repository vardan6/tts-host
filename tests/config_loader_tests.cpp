#include "tts_host/config_loader.hpp"

#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void require(bool condition, const std::string &message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

std::optional<tts_host::CliOptions> parse(std::vector<std::string> args, std::string &error_message) {
  std::vector<char *> argv;
  argv.push_back(const_cast<char *>("tts-host"));
  for (auto &arg : args) {
    argv.push_back(arg.data());
  }
  return tts_host::parse_cli(static_cast<int>(argv.size()), argv.data(), error_message);
}

// docs/requirements/product.md, "Initial interfaces": "Command-line client
// accepting direct text, standard input, and clipboard text."
void stdin_flag_is_accepted_as_a_text_source() {
  std::string error_message;
  const auto options = parse({"--headless", "--stdin", "--out", "out.wav"}, error_message);
  require(options.has_value(), "parsing --stdin --out failed: " + error_message);
  require(options->use_stdin_text, "--stdin did not set use_stdin_text");
  require(!options->synthesize_text.has_value(), "--stdin should not set synthesize_text");
  require(!options->use_clipboard_text, "--stdin should not set use_clipboard_text");
}

void clipboard_flag_is_accepted_as_a_text_source() {
  std::string error_message;
  const auto options = parse({"--headless", "--clipboard", "--out", "out.wav"}, error_message);
  require(options.has_value(), "parsing --clipboard --out failed: " + error_message);
  require(options->use_clipboard_text, "--clipboard did not set use_clipboard_text");
  require(!options->use_stdin_text, "--clipboard should not set use_stdin_text");
}

void text_sources_are_mutually_exclusive() {
  std::string error_message;
  const auto stdin_and_clipboard =
      parse({"--headless", "--stdin", "--clipboard", "--out", "out.wav"}, error_message);
  require(!stdin_and_clipboard.has_value(), "--stdin and --clipboard together should have been rejected");

  const auto synthesize_and_stdin =
      parse({"--headless", "--synthesize", "hi", "--stdin", "--out", "out.wav"}, error_message);
  require(!synthesize_and_stdin.has_value(),
          "--synthesize and --stdin together should have been rejected");
}

void a_text_source_requires_out() {
  std::string error_message;
  const auto stdin_without_out = parse({"--headless", "--stdin"}, error_message);
  require(!stdin_without_out.has_value(), "--stdin without --out should have been rejected");

  const auto out_without_source = parse({"--headless", "--out", "out.wav"}, error_message);
  require(!out_without_source.has_value(), "--out without a text source should have been rejected");
}

void model_id_requires_a_text_source() {
  std::string error_message;
  const auto model_with_stdin =
      parse({"--headless", "--model", "demo", "--stdin", "--out", "out.wav"}, error_message);
  require(model_with_stdin.has_value(), "--model with --stdin should have been accepted: " + error_message);

  const auto model_without_source = parse({"--headless", "--model", "demo"}, error_message);
  require(!model_without_source.has_value(), "--model without a text source should have been rejected");
}

}  // namespace

int main() {
  try {
    stdin_flag_is_accepted_as_a_text_source();
    clipboard_flag_is_accepted_as_a_text_source();
    text_sources_are_mutually_exclusive();
    a_text_source_requires_out();
    model_id_requires_a_text_source();
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
