#include "tts_host/model_list.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

#include <nlohmann/json.hpp>

#include "tts_host/model_catalogue.hpp"

namespace {

using json = nlohmann::json;
using tts_host::ModelListRow;
using tts_host::ModelPackageCandidate;
using tts_host::ModelRegistryScan;

void require(bool condition, const std::string &message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

bool contains(const std::string &text, const std::string &part) { return text.find(part) != std::string::npos; }

ModelPackageCandidate package(const std::string &id, const std::filesystem::path &path, bool bundled,
                              std::optional<std::string> revision) {
  ModelPackageCandidate candidate;
  candidate.id = id;
  candidate.display_name = "Package " + id;
  candidate.engine = "stub";
  candidate.languages = {"en"};
  candidate.package_path = path;
  candidate.manifest = json{{"license", {{"name", "Apache-2.0"}, {"url", "https://example.com/" + id}}}};
  candidate.revision = std::move(revision);
  candidate.bundled = bundled;
  return candidate;
}

ModelListRow row_for(const std::vector<ModelListRow> &rows, const std::string &id) {
  for (const auto &row : rows) {
    if (row.id == id) return row;
  }
  throw std::runtime_error("no row for " + id);
}

std::size_t count_rows(const std::vector<ModelListRow> &rows, const std::string &id) {
  std::size_t count = 0;
  for (const auto &row : rows) count += row.id == id ? 1 : 0;
  return count;
}

// Sizes come from the path so each row's source of size is checkable.
std::uintmax_t fake_size(const std::filesystem::path &path) { return path.string().size(); }

void test_empty_scan_offers_every_catalogue_entry() {
  const auto rows = tts_host::build_model_list({}, "", {}, fake_size);
  const auto &catalogue = tts_host::model_catalogue();
  require(rows.size() == catalogue.size(), "an empty scan lists exactly the catalogue");
  for (std::size_t index = 0; index < catalogue.size(); ++index) {
    const auto &row = rows[index];
    require(row.id == catalogue[index].id, "catalogue rows keep catalogue order");
    require(row.source == "catalogue" && row.status == "not installed", "an offered entry is not installed");
    require(!row.installed && row.in_catalogue && !row.loaded, "an offered entry is a catalogue row");
    require(row.size_bytes == tts_host::catalogue_entry_size_bytes(catalogue[index]),
            "an offered entry shows its download size");
  }
}

void test_installed_rows_replace_catalogue_rows() {
  const auto &catalogue = tts_host::model_catalogue();
  require(catalogue.size() >= 2, "the catalogue carries at least two entries");
  const auto &current_id = catalogue[0].id;
  const auto &stale_id = catalogue[1].id;

  ModelRegistryScan scan;
  scan.discovered_packages.push_back(
      package(current_id, "/bundled/" + current_id, true, tts_host::catalogue_revision_for(current_id)));
  scan.discovered_packages.push_back(package(stale_id, "/user/" + stale_id, false, std::string(64, '0')));
  scan.discovered_packages.push_back(package("imported", "/user/imported", false, std::nullopt));

  const auto rows = tts_host::build_model_list(scan, stale_id, {{current_id, "no usable GPU"}}, fake_size);
  require(rows.size() == catalogue.size() + 1, "installed catalogue ids are not listed twice");
  require(rows[0].id == current_id && rows[2].id == "imported", "installed packages come first, in scan order");
  require(count_rows(rows, current_id) == 1 && count_rows(rows, stale_id) == 1, "one row per id");

  const auto current = row_for(rows, current_id);
  require(current.source == "bundled" && current.status == "current", "a bundled catalogue revision is current");
  require(current.installed && current.in_catalogue && !current.update_available, "current row flags");
  require(current.size_bytes == fake_size("/bundled/" + current_id), "installed size is the folder size");
  require(current.unavailable_reason == "no usable GPU", "a rejected load shows its reason");
  require(!current.loaded, "only the resident model is loaded");

  const auto stale = row_for(rows, stale_id);
  require(stale.source == "per-user" && stale.status == "update available" && stale.update_available,
          "an older catalogue revision offers an update");
  require(stale.loaded && !stale.unavailable_reason, "the resident model is loaded");

  const auto imported = row_for(rows, "imported");
  require(imported.source == "per-user" && imported.status == "local", "an import is local");
  require(!imported.in_catalogue && !imported.update_available, "an import has no catalogue actions");
}

void test_previous_versions_stay_reachable() {
  const auto &catalogue_id = tts_host::model_catalogue()[0].id;
  ModelRegistryScan scan;
  scan.previous_packages.push_back(package("kept", "/user/.previous/kept", false, std::nullopt));
  scan.previous_packages.push_back(package(catalogue_id, "/user/.previous/" + catalogue_id, false, std::nullopt));

  const auto rows = tts_host::build_model_list(scan, "", {}, fake_size);
  const auto kept = row_for(rows, "kept");
  require(rows.back().id == "kept", "previous-only ids follow the catalogue rows");
  require(kept.source == "per-user" && kept.status == "previous version only" && !kept.installed,
          "a removed import with a kept previous version is listed");
  require(kept.size_bytes == fake_size("/user/.previous/kept"), "a previous-only row shows the kept size");

  const auto offered = row_for(rows, catalogue_id);
  require(count_rows(rows, catalogue_id) == 1, "a kept catalogue id is listed once");
  require(offered.source == "catalogue" && offered.status == "not installed, previous version kept",
          "a removed catalogue model with a kept version stays downloadable");
}

void test_details_describe_the_selected_model() {
  const auto &catalogue = tts_host::model_catalogue();
  const auto &id = catalogue[0].id;
  ModelRegistryScan scan;
  scan.discovered_packages.push_back(package(id, "/user/" + id, false, std::nullopt));
  scan.superseded_packages.push_back(package(id, "/bundled/" + id, true, std::nullopt));
  scan.previous_packages.push_back(package(id, "/user/.previous/" + id, false, std::nullopt));
  scan.unsupported_entries.push_back({"/user/broken", "model.json is missing"});

  const auto rows = tts_host::build_model_list(scan, id, {{id, "runner rejected the load"}}, fake_size);
  const auto details = tts_host::describe_model_details(row_for(rows, id), scan);
  for (const auto *part : {"Package ", "Source: per-user, local", "Size on disk: ", "Loaded: yes",
                           "Unavailable: runner rejected the load", "Engine: stub", "Folder: /user/",
                           "Licence: Apache-2.0", "Superseded copies", "[bundled] /bundled/",
                           "Previous version", "/user/.previous/"}) {
    require(contains(details, part), std::string("installed details carry ") + part);
  }
  require(!contains(details, "Unsupported"), "row details leave unsupported folders to their own section");

  const auto offered_details = tts_host::describe_model_details(row_for(rows, catalogue[1].id), scan);
  require(contains(offered_details, "Download size: ") && contains(offered_details, "Licence: ") &&
              contains(offered_details, catalogue[1].license.url) && !contains(offered_details, "Folder:"),
          "catalogue details show the download size and licence before download");

  const auto unsupported = tts_host::describe_unsupported_entries(scan);
  require(contains(unsupported, "/user/broken") && contains(unsupported, "model.json is missing"),
          "unsupported folders are listed with their reason");
  require(tts_host::describe_unsupported_entries({}).empty(), "no unsupported folders, no section");
}

void test_folder_size_on_disk(const std::filesystem::path &work) {
  std::filesystem::remove_all(work);
  std::filesystem::create_directories(work / "nested");
  std::ofstream(work / "a.bin", std::ios::binary) << "12345";
  std::ofstream(work / "nested" / "b.bin", std::ios::binary) << "123";
  require(tts_host::folder_size_on_disk(work) == 8, "folder size sums nested files");
  require(tts_host::folder_size_on_disk(work / "missing") == 0, "a missing folder has no size");
  std::filesystem::remove_all(work);
}

}  // namespace

int main(int argc, char **argv) {
  if (argc != 2) {
    std::cerr << "usage: tts-host-model-list-tests <work-directory>\n";
    return 2;
  }
  try {
    test_empty_scan_offers_every_catalogue_entry();
    test_installed_rows_replace_catalogue_rows();
    test_previous_versions_stay_reachable();
    test_details_describe_the_selected_model();
    test_folder_size_on_disk(argv[1]);
  } catch (const std::exception &error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
  }
  std::cout << "model list tests passed\n";
  return 0;
}
