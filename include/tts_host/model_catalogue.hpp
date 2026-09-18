#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "tts_host/model_registry.hpp"

namespace tts_host {

// One downloadable file of a catalogue entry. `relative_path` is where the file
// lands inside the installed package directory, so it matches what the entry's
// generated model.json declares under files.*.
struct CatalogueFile {
  std::string relative_path;
  // The key this file occupies in the generated model.json's "files" object
  // (e.g. "model", "voice") -- see schemas/model.schema.json, which requires a
  // "model" key. Catalogue download has to synthesize this manifest itself
  // since the remote host serves only weight files, not a model.json.
  std::string manifest_key;
  std::string url;
  // Pinned lowercase hex SHA-256 of the fetched bytes. Verification before
  // install is what makes an HTTPS fetch from a third-party host acceptable --
  // see docs/design/architecture.md#download-catalogue.
  std::string sha256;
  std::uint64_t size_bytes = 0;
};

struct CatalogueLicense {
  std::string name;
  std::string url;
  // Non-commercial terms are allowed in the catalogue provided they are badged
  // and shown before the download starts.
  bool non_commercial = false;
};

struct CatalogueEntry {
  std::string id;
  std::string display_name;
  std::string engine;
  std::vector<std::string> languages;
  CatalogueLicense license;
  std::vector<CatalogueFile> files;
};

// The curated catalogue, compiled into the binary rather than fetched at
// runtime, so it introduces no trust root beyond the binary the user already
// chose to run (docs/design/architecture.md#download-catalogue). The cost is
// that it is stale until the next release.
const std::vector<CatalogueEntry> &model_catalogue();

// Total bytes a full install of the entry has to fetch, for the size shown
// before download.
std::uint64_t catalogue_entry_size_bytes(const CatalogueEntry &entry);

// Throws std::runtime_error naming the offending entry and field. Applied to
// the compiled-in catalogue by its own test, so a mistyped checksum or a
// plain-HTTP URL fails the build's test run rather than a user's download.
void validate_catalogue(const std::vector<CatalogueEntry> &entries);

// True when a scan already discovered a package carrying this entry's id, which
// is what distinguishes an installed model from an offered one.
bool catalogue_entry_installed(const CatalogueEntry &entry, const ModelRegistryScan &scan);

// The licence line shown before download, with the non-commercial badge when
// the terms carry one.
std::string describe_catalogue_license(const CatalogueEntry &entry);

// Human-readable download size, e.g. "310.5 MB".
std::string describe_download_size(std::uint64_t bytes);

}  // namespace tts_host
