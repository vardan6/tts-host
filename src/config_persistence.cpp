#include "tts_host/config_persistence.hpp"

#include <atomic>
#include <fstream>
#include <stdexcept>
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace tts_host {
namespace {

void write_file(const std::filesystem::path &path, const std::string &contents) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  output.exceptions(std::ios::failbit | std::ios::badbit);
  output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
  output.flush();
  output.close();
}

void replace_file(const std::filesystem::path &source, const std::filesystem::path &target) {
#ifdef _WIN32
  if (!MoveFileExW(source.c_str(), target.c_str(),
                   MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    throw std::runtime_error("Unable to replace configuration: Windows error " +
                             std::to_string(GetLastError()));
  }
#else
  std::filesystem::rename(source, target);
#endif
}

struct TemporaryFile {
  std::filesystem::path path;
  ~TemporaryFile() {
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
  }
};

std::filesystem::path temporary_path(const std::filesystem::path &target) {
  static std::atomic_uint64_t sequence{0};
#ifdef _WIN32
  const auto process = GetCurrentProcessId();
#else
  const auto process = getpid();
#endif
  auto path = target;
  path += ".tmp-" + std::to_string(process) + "-" + std::to_string(sequence++);
  return path;
}

}  // namespace

void save_config_document(const ConfigDocument &document, const ConfigSaveOperations &operations) {
  validate_config(document);
  const auto write = operations.write ? operations.write : write_file;
  const auto replace = operations.replace ? operations.replace : replace_file;
  const auto &destination = document.paths.config_path;
  const std::string contents = document.value.dump(2) + '\n';
  TemporaryFile staged{temporary_path(destination)};
  write(staged.path, contents);

  // Read/validate the disk document, rather than backing up the edited value.
  // If another editor left invalid JSON, keep any existing last-valid copy.
  std::ifstream input(destination, std::ios::binary);
  if (!input) throw std::runtime_error("Unable to read configuration before saving");
  nlohmann::json previous;
  bool valid = false;
  try {
    input >> previous;
    validate_config({previous, document.paths});
    valid = true;
  } catch (const std::exception &) {
    // Validation failure must never overwrite a usable retained copy.
  }
  input.close();
  if (valid) {
    auto backup = destination;
    backup += ".last-valid";
    TemporaryFile staged_backup{temporary_path(backup)};
    write(staged_backup.path, previous.dump(2) + '\n');
    replace(staged_backup.path, backup);
  }
  replace(staged.path, destination);
}

}  // namespace tts_host
