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

// --path is only prepended when espeak-ng-data sits beside the executable, so
// the same binary is invoked with and without it depending on the platform and
// build layout (src/espeak_phonemizer.cpp).
int fake_espeak_ng(int argc, char **argv) {
  int first = 1;
  if (std::string(argv[first]).rfind("--path=", 0) == 0) {
    require(std::filesystem::is_directory(std::string(argv[first]).substr(7) + "/espeak-ng-data"),
            "--path did not point at the directory holding espeak-ng-data");
    ++first;
  }
  require(argc == first + 6, "espeak-ng received an unexpected number of arguments");
  require(std::string(argv[first]) == "-q" && std::string(argv[first + 1]) == "--ipa=3" &&
              std::string(argv[first + 2]) == "-v" && std::string(argv[first + 3]) == "en-us" &&
              std::string(argv[first + 4]) == "--" &&
              std::string(argv[first + 5]) == "Hello; $(not-a-command)",
          "espeak-ng invocation arguments were not preserved");
  std::cout << "h\u0259l\u02c8\u0259\u028a\n";
  return 0;
}

}  // namespace

int main(int argc, char **argv) {
  try {
    if (argc > 1 && (std::string(argv[1]) == "-q" ||
                     std::string(argv[1]).rfind("--path=", 0) == 0)) {
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
