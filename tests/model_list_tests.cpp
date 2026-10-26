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

// Damaged rows never load; an older catalogue revision offers Update, the
// current one Verify, and a local one neither (Remove is the menu's).
void test_damaged_rows_offer_their_recovery() {
  const auto &catalogue = tts_host::model_catalogue();
  const auto &current_id = catalogue[0].id;
  const auto &stale_id = catalogue[1].id;
  ModelRegistryScan scan;
  scan.damaged_packages.push_back(
      package(current_id, "/user/" + current_id, false, tts_host::catalogue_revision_for(current_id)));
  scan.damaged_packages.push_back(package(stale_id, "/user/" + stale_id, false, std::string(64, '0')));
  scan.damaged_packages.push_back(package("imported", "/user/imported", false, std::nullopt));

  const auto rows = tts_host::build_model_list(scan, "", {}, fake_size);
  for (const auto &id : {current_id, stale_id, std::string("imported")}) {
    const auto row = row_for(rows, id);
    require(row.damaged && !row.installed && row.status == "damaged", "a damaged package loads or hides its damage");
  }
  require(!row_for(rows, current_id).update_available, "a damaged current revision offers an update");
  require(row_for(rows, stale_id).update_available, "a damaged older revision offers no update");
  require(!row_for(rows, "imported").update_available, "a damaged import offers an update");
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

using tts_host::ModelActionKind;

bool applies(const ModelListRow &row, ModelActionKind kind) { return tts_host::model_action(row, kind).applies; }

std::filesystem::path target(const ModelListRow &row, ModelActionKind kind) {
  return tts_host::model_action(row, kind).target;
}

// Each row carries every action, applicable or not, and only Load and Open
// folder leave the registry alone.
void test_rows_carry_every_action() {
  const auto rows = tts_host::build_model_list({}, "", {}, fake_size);
  for (const auto &row : rows) {
    require(row.actions.size() == 9, "a row carries one action per kind");
    for (std::size_t index = 0; index < row.actions.size(); ++index) {
      const auto &action = row.actions[index];
      require(action.kind == static_cast<ModelActionKind>(index), "actions follow the kind order");
      require(action.changes_registry ==
                  (action.kind != ModelActionKind::load && action.kind != ModelActionKind::open_folder),
              "only Load and Open folder leave the registry alone");
    }
  }
}

void test_installed_row_actions() {
  const auto &catalogue = tts_host::model_catalogue();
  const auto &id = catalogue[0].id;
  const auto &stale_id = catalogue[1].id;
  ModelRegistryScan scan;
  scan.discovered_packages.push_back(package(id, "/user/" + id, false, tts_host::catalogue_revision_for(id)));
  scan.previous_packages.push_back(package(id, "/user/.previous/" + id + "@old", false, std::string(64, '1')));
  scan.superseded_packages.push_back(package(id, "/other/" + id, false, std::nullopt));
  scan.superseded_packages.push_back(package(id, "/bundled/" + id, true, std::nullopt));
  // An update of a bundled copy: the bundled one is its previous version.
  scan.discovered_packages.push_back(package(stale_id, "/user/" + stale_id, false, std::string(64, '0')));
  scan.superseded_packages.push_back(package(stale_id, "/bundled/" + stale_id, true, std::nullopt));
  // A kept version in another directory is deletable but not revertible.
  scan.discovered_packages.push_back(package("imported", "/user/imported", false, std::nullopt));
  scan.previous_packages.push_back(package("imported", "/other/.previous/imported@local", false, std::nullopt));

  const auto rows = tts_host::build_model_list(scan, "", {}, fake_size);
  const auto current = row_for(rows, id);
  require(applies(current, ModelActionKind::load) && target(current, ModelActionKind::load) == "/user/" + id,
          "an installed package loads");
  require(!applies(current, ModelActionKind::download) && !applies(current, ModelActionKind::update),
          "a current package is neither downloaded nor updated");
  require(applies(current, ModelActionKind::revert) && target(current, ModelActionKind::revert) == "/user/" + id,
          "a kept version beside the package reverts it");
  require(target(current, ModelActionKind::delete_previous) == "/user/.previous/" + id + "@old",
          "Delete previous names the kept version");
  require(target(current, ModelActionKind::remove_superseded) == "/other/" + id,
          "Remove superseded names the per-user copy, never the bundled one");
  require(applies(current, ModelActionKind::remove) && !tts_host::model_action(current, ModelActionKind::remove).reason,
          "a per-user package is removable");
  require(applies(current, ModelActionKind::verify), "a current catalogue package is verifiable");
  require(target(current, ModelActionKind::open_folder) == "/user/" + id, "Open folder opens the package");

  const auto stale = row_for(rows, stale_id);
  require(applies(stale, ModelActionKind::update) && target(stale, ModelActionKind::update) == "/user/" + stale_id,
          "an older revision updates in place");
  require(applies(stale, ModelActionKind::revert), "a superseded bundled copy is the version to revert to");
  require(!applies(stale, ModelActionKind::verify), "an older revision updates instead of verifying");
  require(!applies(stale, ModelActionKind::remove_superseded), "a bundled superseded copy is read-only");

  const auto imported = row_for(rows, "imported");
  require(!applies(imported, ModelActionKind::revert), "a kept version in another directory is not reverted to");
  require(applies(imported, ModelActionKind::delete_previous), "any kept version is deletable");
  require(!applies(imported, ModelActionKind::verify) && !applies(imported, ModelActionKind::update),
          "an import has no catalogue maintenance");
  require(applies(imported, ModelActionKind::remove), "an import is removable");
}

void test_bundled_row_actions() {
  const auto &id = tts_host::model_catalogue()[0].id;
  ModelRegistryScan scan;
  scan.discovered_packages.push_back(package(id, "/bundled/" + id, true, tts_host::catalogue_revision_for(id)));
  const auto row = row_for(tts_host::build_model_list(scan, "", {}, fake_size), id);
  const auto &remove = tts_host::model_action(row, ModelActionKind::remove);
  require(!remove.applies && remove.reason == "Bundled with the app", "a bundled package says why it stays");
  require(!applies(row, ModelActionKind::revert), "a bundled package has nothing to revert to");
  require(applies(row, ModelActionKind::verify), "a bundled package is verified, reported only");
}

void test_damaged_row_actions() {
  const auto &catalogue = tts_host::model_catalogue();
  const auto &current_id = catalogue[0].id;
  const auto &stale_id = catalogue[1].id;
  ModelRegistryScan scan;
  // Damaged in both tiers: maintenance acts on the copy scan order puts first.
  scan.damaged_packages.push_back(
      package(current_id, "/user/" + current_id, false, tts_host::catalogue_revision_for(current_id)));
  scan.damaged_packages.push_back(
      package(current_id, "/bundled/" + current_id, true, tts_host::catalogue_revision_for(current_id)));
  scan.damaged_packages.push_back(package(stale_id, "/bundled/" + stale_id, true, std::string(64, '0')));

  const auto rows = tts_host::build_model_list(scan, "", {}, fake_size);
  const auto current = row_for(rows, current_id);
  require(!applies(current, ModelActionKind::load) && !applies(current, ModelActionKind::download),
          "a damaged package neither loads nor downloads");
  require(applies(current, ModelActionKind::verify) && applies(current, ModelActionKind::remove),
          "a damaged current per-user copy offers Verify and Remove");
  for (const auto kind : {ModelActionKind::verify, ModelActionKind::remove, ModelActionKind::open_folder}) {
    require(target(current, kind) == "/user/" + current_id, "maintenance targets the copy tier precedence picks");
  }

  const auto stale = row_for(rows, stale_id);
  require(applies(stale, ModelActionKind::update) && !applies(stale, ModelActionKind::verify),
          "a damaged older revision updates");
  require(!applies(stale, ModelActionKind::remove) && applies(stale, ModelActionKind::open_folder),
          "a damaged bundled copy is read-only but can be opened");
}

void test_uninstalled_row_actions() {
  const auto &catalogue = tts_host::model_catalogue();
  ModelRegistryScan scan;
  scan.previous_packages.push_back(package("kept", "/user/.previous/kept", false, std::nullopt));
  const auto rows = tts_host::build_model_list(scan, "", {}, fake_size);

  const auto offered = row_for(rows, catalogue[0].id);
  require(applies(offered, ModelActionKind::download) && target(offered, ModelActionKind::download).empty(),
          "a catalogue entry downloads");
  for (const auto &action : offered.actions) {
    require(action.kind == ModelActionKind::download || !action.applies,
            "a not-installed entry offers only Download");
  }

  const auto kept = row_for(rows, "kept");
  require(applies(kept, ModelActionKind::delete_previous) && !applies(kept, ModelActionKind::download),
          "a previous-only row deletes its kept version");
  require(target(kept, ModelActionKind::open_folder) == "/user/.previous/kept",
          "a previous-only row opens the kept version");
  require(!applies(kept, ModelActionKind::remove) && !applies(kept, ModelActionKind::revert),
          "a previous-only row has nothing installed to remove or revert");
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
    test_damaged_rows_offer_their_recovery();
    test_previous_versions_stay_reachable();
    test_rows_carry_every_action();
    test_installed_row_actions();
    test_bundled_row_actions();
    test_damaged_row_actions();
    test_uninstalled_row_actions();
    test_details_describe_the_selected_model();
    test_folder_size_on_disk(argv[1]);
  } catch (const std::exception &error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
  }
  std::cout << "model list tests passed\n";
  return 0;
}
