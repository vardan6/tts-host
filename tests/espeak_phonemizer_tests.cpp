#include "tts_host/espeak_phonemizer.hpp"
#include "tts_host/runner_protocol.hpp"

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const std::string &message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

int fake_espeak_ng(int argc, char **argv) {
  require(argc == 7, "espeak-ng received an unexpected number of arguments");
  require(std::string(argv[1]) == "-q" && std::string(argv[2]) == "--ipa=3" &&
              std::string(argv[3]) == "-v" && std::string(argv[4]) == "en-us" &&
              std::string(argv[5]) == "--" && std::string(argv[6]) == "Hello; $(not-a-command)",
          "espeak-ng invocation arguments were not preserved");
  std::cout << "h\u0259l\u02c8\u0259\u028a\n";
  return 0;
}

}  // namespace

int main(int argc, char **argv) {
  try {
    if (argc > 1 && std::string(argv[1]) == "-q") {
      return fake_espeak_ng(argc, argv);
    }

    const auto ipa = tts_host::phonemize_with_espeak_ng(
        std::filesystem::absolute(argv[0]), "en-us", "Hello; $(not-a-command)");
    require(ipa == "h\u0259l\u02c8\u0259\u028a", "IPA output was not captured and normalized");

    try {
      static_cast<void>(tts_host::phonemize_with_espeak_ng("does-not-exist", "en-us", "hello"));
      throw std::runtime_error("missing espeak-ng executable did not fail");
    } catch (const tts_host::RunnerProtocolError &error) {
      require(std::string(error.what()).find("exited with code") != std::string::npos,
              "missing espeak-ng error was not actionable");
    }
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
