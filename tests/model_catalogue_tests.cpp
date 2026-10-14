#include "tts_host/model_catalogue.hpp"

#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

tts_host::CatalogueEntry valid_entry() {
  return tts_host::CatalogueEntry{
      "demo-model",
      "Demo Model",
      "kokoro-onnx",
      {"en"},
      tts_host::CatalogueLicense{"Apache-2.0", "https://example.invalid/licence", false},
      {
          tts_host::CatalogueFile{
              "model.onnx", "model", "https://example.invalid/model.onnx",
              "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef", 2048},
      },
  };
}

void expect_valid(const std::vector<tts_host::CatalogueEntry> &entries,
                  const std::string &message) {
  try {
    tts_host::validate_catalogue(entries);
  } catch (const std::exception &error) {
    throw std::runtime_error(message + "\n  rejected with: " + error.what());
  }
}

void expect_rejected(const tts_host::CatalogueEntry &entry, const std::string &expected_fragment,
                     const std::string &message) {
  try {
    tts_host::validate_catalogue({entry});
  } catch (const std::exception &error) {
    const std::string reason = error.what();
    if (reason.find(expected_fragment) == std::string::npos) {
      throw std::runtime_error(message + "\n  expected reason containing: " + expected_fragment +
                               "\n  actual reason:              " + reason);
    }
    return;
  }
  throw std::runtime_error(message + "\n  the entry was accepted");
}

void the_compiled_in_catalogue_is_well_formed() {
  const auto &entries = tts_host::model_catalogue();
  expect_valid(entries, "the catalogue shipped in the binary failed its own validation");
  if (entries.empty()) {
    throw std::runtime_error("the compiled-in catalogue offers nothing to download");
  }
}

void a_complete_entry_is_accepted() {
  expect_valid({valid_entry()}, "a complete entry was rejected");
}

void incomplete_entries_are_rejected_by_field() {
  auto missing_name = valid_entry();
  missing_name.display_name.clear();
  expect_rejected(missing_name, "displayName is empty", "an entry with no display name shipped");

  auto no_languages = valid_entry();
  no_languages.languages.clear();
  expect_rejected(no_languages, "declares no languages", "an entry with no languages shipped");

  auto no_files = valid_entry();
  no_files.files.clear();
  expect_rejected(no_files, "declares no files", "an entry with nothing to download shipped");
}

// Licence disclosure before download is a product requirement, so an entry that
// has no terms to disclose must not reach a user at all.
void an_entry_without_a_licence_is_rejected() {
  auto unlicensed = valid_entry();
  unlicensed.license.name.clear();
  expect_rejected(unlicensed, "licence name is empty", "an entry with no licence shipped");
}

void a_duplicate_id_is_rejected() {
  const auto entry = valid_entry();
  try {
    tts_host::validate_catalogue({entry, entry});
  } catch (const std::exception &) {
    return;
  }
  throw std::runtime_error("the same id twice in one catalogue was accepted");
}

void plain_http_is_rejected() {
  auto insecure = valid_entry();
  insecure.files.front().url = "http://example.invalid/model.onnx";
  expect_rejected(insecure, "not fetched over HTTPS", "a plain-HTTP download shipped");

  auto no_url = valid_entry();
  no_url.files.front().url.clear();
  expect_rejected(no_url, "not fetched over HTTPS", "an entry with no URL shipped");
}

void an_unpinned_or_malformed_checksum_is_rejected() {
  auto unpinned = valid_entry();
  unpinned.files.front().sha256.clear();
  expect_rejected(unpinned, "SHA-256", "a file with no pinned checksum shipped");

  auto too_short = valid_entry();
  too_short.files.front().sha256 = "0123456789abcdef";
  expect_rejected(too_short, "SHA-256", "a truncated checksum shipped");

  auto uppercase = valid_entry();
  uppercase.files.front().sha256 =
      "0123456789ABCDEF0123456789abcdef0123456789abcdef0123456789abcdef";
  expect_rejected(uppercase, "SHA-256", "a non-lowercase checksum shipped");

  auto zero_size = valid_entry();
  zero_size.files.front().size_bytes = 0;
  expect_rejected(zero_size, "zero download size", "a file with no declared size shipped");
}

// A downloaded file must land inside its own package directory, the same
// containment rule the registry applies to manifest file paths.
void a_file_path_escaping_the_package_is_rejected() {
  const std::vector<std::string> escaping_paths = {
      "../model.onnx", "voices/../../model.onnx", "/etc/model.onnx", "C:\\model.onnx", ""};
  for (const auto &path : escaping_paths) {
    auto escaping = valid_entry();
    escaping.files.front().relative_path = path;
    expect_rejected(escaping, "escapes the package root",
                    "a file path escaping the package root shipped: " + path);
  }
}

// Download has to synthesize model.json itself (the remote host serves only
// weight files), so every file needs a manifest key, and schemas/model.schema.json
// requires that one of them be "model".
void manifest_keys_are_required_and_unique() {
  auto no_key = valid_entry();
  no_key.files.front().manifest_key.clear();
  expect_rejected(no_key, "declares no manifest key", "a file with no manifest key shipped");

  auto duplicate_key = valid_entry();
  duplicate_key.files.push_back(duplicate_key.files.front());
  duplicate_key.files.back().relative_path = "model2.onnx";
  expect_rejected(duplicate_key, "appears more than once",
                  "two files sharing a manifest key shipped");

  auto no_model_key = valid_entry();
  no_model_key.files.front().manifest_key = "weights";
  expect_rejected(no_model_key, "no file declares the required manifest key \"model\"",
                  "an entry with no \"model\" manifest key shipped");
}

void an_installed_entry_is_recognized_by_id() {
  const auto entry = valid_entry();

  tts_host::ModelRegistryScan empty_scan;
  if (tts_host::catalogue_entry_installed(entry, empty_scan)) {
    throw std::runtime_error("an entry counted as installed against an empty registry");
  }

  tts_host::ModelRegistryScan other_package;
  other_package.discovered_packages.push_back(tts_host::ModelPackageCandidate{
      "some-other-model", "Other", "kokoro-onnx", {"en"}, "/models/other", "/models/other/model.json",
      {}});
  if (tts_host::catalogue_entry_installed(entry, other_package)) {
    throw std::runtime_error("an unrelated installed package counted as this entry");
  }

  auto installed = other_package;
  installed.discovered_packages.push_back(tts_host::ModelPackageCandidate{
      entry.id, entry.display_name, entry.engine, {"en"}, "/models/demo", "/models/demo/model.json",
      {}});
  if (!tts_host::catalogue_entry_installed(entry, installed)) {
    throw std::runtime_error("a package already in the registry was still offered as missing");
  }
}

void the_licence_notice_badges_non_commercial_terms() {
  auto permissive = valid_entry();
  const auto permissive_notice = tts_host::describe_catalogue_license(permissive);
  if (permissive_notice.find("NON-COMMERCIAL") != std::string::npos) {
    throw std::runtime_error("a permissive licence was badged non-commercial: " +
                             permissive_notice);
  }
  if (permissive_notice.find("Apache-2.0") == std::string::npos ||
      permissive_notice.find(permissive.license.url) == std::string::npos) {
    throw std::runtime_error("the licence notice dropped its name or URL: " + permissive_notice);
  }

  auto restricted = valid_entry();
  restricted.license = tts_host::CatalogueLicense{"CC-BY-NC-4.0", "https://example.invalid/nc",
                                                  true};
  const auto restricted_notice = tts_host::describe_catalogue_license(restricted);
  if (restricted_notice.find("NON-COMMERCIAL") == std::string::npos) {
    throw std::runtime_error("non-commercial terms were shown without a badge: " +
                             restricted_notice);
  }
}

void the_download_size_is_summed_and_readable() {
  auto entry = valid_entry();
  entry.files.push_back(tts_host::CatalogueFile{
      "voices/voice.bin", "voice", "https://example.invalid/voice.bin",
      "fedcba9876543210fedcba9876543210fedcba9876543210fedcba9876543210", 1024});
  if (tts_host::catalogue_entry_size_bytes(entry) != 3072) {
    throw std::runtime_error("the entry's download size did not sum its files");
  }

  if (tts_host::describe_download_size(3072) != "3 KB") {
    throw std::runtime_error("a small download was not reported in KB: " +
                             tts_host::describe_download_size(3072));
  }
  if (tts_host::describe_download_size(325532232) != "310.4 MB" &&
      tts_host::describe_download_size(325532232) != "310.5 MB") {
    throw std::runtime_error("a large download was not reported in MB: " +
                             tts_host::describe_download_size(325532232));
  }
}

}  // namespace

int main() {
  try {
    the_compiled_in_catalogue_is_well_formed();
    a_complete_entry_is_accepted();
    incomplete_entries_are_rejected_by_field();
    an_entry_without_a_licence_is_rejected();
    a_duplicate_id_is_rejected();
    plain_http_is_rejected();
    an_unpinned_or_malformed_checksum_is_rejected();
    a_file_path_escaping_the_package_is_rejected();
    manifest_keys_are_required_and_unique();
    an_installed_entry_is_recognized_by_id();
    the_licence_notice_badges_non_commercial_terms();
    the_download_size_is_summed_and_readable();
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
