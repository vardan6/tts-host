#pragma once

#include <cstdint>
#include <optional>
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

// A small text file the host writes into the package itself, such as a
// provenance notice the remote host does not serve. Like CatalogueFile, it
// occupies a key in the generated model.json's "files" object.
struct CatalogueGeneratedFile {
  std::string relative_path;
  std::string manifest_key;
  std::string contents;
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
  std::vector<CatalogueGeneratedFile> generated_files;
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

// The discovered package carrying this entry's id, or null when it is offered.
const ModelPackageCandidate *catalogue_entry_package(const CatalogueEntry &entry,
                                                     const ModelRegistryScan &scan);

// The ADR 0010 content revision of the package this entry installs: SHA-256
// of `<file sha256>  <relative path>\n` lines (sha256sum format) sorted by
// path, over downloaded and host-written files alike, model.json excluded.
// Computed from the pinned checksums, so it cannot drift from the files.
std::string catalogue_entry_revision(const CatalogueEntry &entry);

// The compiled catalogue's revision for a model id, or nothing when the id is
// not in the catalogue. Registry precedence and update status compare against it.
std::optional<std::string> catalogue_revision_for(const std::string &id);

// How an installed package relates to the compiled catalogue
// (docs/design/architecture.md#model-installation-and-updates). A package with
// no revision -- a local import, or an id the catalogue does not carry -- is
// local and never offered an update.
enum class PackageStatus { local, current, update_available };

PackageStatus package_status(const ModelPackageCandidate &package);

// "local", "current", or "update available".
std::string describe_package_status(PackageStatus status);

// The licence line shown before download, with the non-commercial badge when
// the terms carry one.
std::string describe_catalogue_license(const CatalogueEntry &entry);

// Human-readable download size, e.g. "310.5 MB".
std::string describe_download_size(std::uint64_t bytes);

}  // namespace tts_host
