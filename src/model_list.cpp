#include "tts_host/model_list.hpp"

#include <algorithm>
#include <system_error>

#include "tts_host/model_catalogue.hpp"
#include "tts_host/model_update.hpp"
#include "tts_host/path_encoding.hpp"

namespace tts_host {

namespace {

const CatalogueEntry *catalogue_entry_for(const std::string &id) {
  const auto &entries = model_catalogue();
  const auto found =
      std::find_if(entries.begin(), entries.end(), [&](const CatalogueEntry &entry) { return entry.id == id; });
  return found == entries.end() ? nullptr : &*found;
}

const ModelPackageCandidate *find_package(const std::vector<ModelPackageCandidate> &packages, const std::string &id) {
  const auto found = std::find_if(packages.begin(), packages.end(),
                                  [&](const ModelPackageCandidate &package) { return package.id == id; });
  return found == packages.end() ? nullptr : &*found;
}

bool has_row(const std::vector<ModelListRow> &rows, const std::string &id) {
  return std::any_of(rows.begin(), rows.end(), [&](const ModelListRow &row) { return row.id == id; });
}

// The rules behind the buttons and More menu; model_update throws on the same
// predicates, so the menu and the operation cannot disagree.
std::vector<ModelAction> row_actions(const ModelListRow &row, const ModelRegistryScan &scan) {
  const auto *installed = find_package(scan.discovered_packages, row.id);
  const auto *maintained = maintained_package(row.id, scan);
  const auto *previous = find_package(scan.previous_packages, row.id);
  const ModelPackageCandidate *superseded = nullptr;
  for (const auto &package : scan.superseded_packages) {
    if (package.id == row.id && !package.bundled) {
      superseded = &package;
      break;
    }
  }
  const auto folder = [](const ModelPackageCandidate *package) {
    return package != nullptr ? package->package_path : std::filesystem::path();
  };
  const bool bundled = maintained != nullptr && maintained->bundled;
  // Verify compares against the catalogue revision; an older one is updated instead.
  const bool verifiable =
      maintained != nullptr && row.in_catalogue && package_status(*maintained) == PackageStatus::current;
  return {
      {ModelActionKind::load, installed != nullptr, folder(installed), std::nullopt, false},
      {ModelActionKind::download, !row.installed && !row.damaged && row.in_catalogue, {}, std::nullopt, true},
      {ModelActionKind::update, row.update_available, folder(maintained), std::nullopt, true},
      {ModelActionKind::revert, revert_source(row.id, scan) != nullptr, folder(installed), std::nullopt, true},
      {ModelActionKind::delete_previous, previous != nullptr, folder(previous), std::nullopt, true},
      {ModelActionKind::remove_superseded, superseded != nullptr, folder(superseded), std::nullopt, true},
      {ModelActionKind::remove, maintained != nullptr && !bundled, folder(maintained),
       bundled ? std::optional<std::string>("Bundled with the app") : std::nullopt, true},
      {ModelActionKind::verify, verifiable, folder(maintained), std::nullopt, true},
      // A row with only a kept previous version opens that copy.
      {ModelActionKind::open_folder, maintained != nullptr || previous != nullptr,
       maintained != nullptr ? folder(maintained) : folder(previous), std::nullopt, false},
  };
}

}  // namespace

std::vector<ModelListRow> build_model_list(const ModelRegistryScan &scan, const std::string &loaded_id,
                                           const std::map<std::string, std::string> &load_failures,
                                           const FolderSize &folder_size) {
  std::vector<ModelListRow> rows;
  const auto finish = [&](ModelListRow &row) {
    row.in_catalogue = catalogue_entry_for(row.id) != nullptr;
    row.loaded = !loaded_id.empty() && row.id == loaded_id;
    if (const auto failure = load_failures.find(row.id); failure != load_failures.end())
      row.unavailable_reason = failure->second;
    row.actions = row_actions(row, scan);
    rows.push_back(std::move(row));
  };

  for (const auto &package : scan.discovered_packages) {
    ModelListRow row;
    row.id = package.id;
    row.display_name = package.display_name;
    row.source = package.bundled ? "bundled" : "per-user";
    const auto status = package_status(package);
    row.status = describe_package_status(status);
    row.update_available = status == PackageStatus::update_available;
    row.size_bytes = folder_size(package.package_path);
    row.installed = true;
    finish(row);
  }
  for (const auto &package : scan.damaged_packages) {
    if (has_row(rows, package.id)) continue;
    ModelListRow row;
    row.id = package.id;
    row.display_name = package.display_name;
    row.source = package.bundled ? "bundled" : "per-user";
    row.status = "damaged";
    // An older revision is recovered by updating it; Verify needs the current one.
    row.update_available = package_status(package) == PackageStatus::update_available;
    row.size_bytes = folder_size(package.package_path);
    row.damaged = true;
    finish(row);
  }
  for (const auto &entry : model_catalogue()) {
    if (has_row(rows, entry.id)) continue;
    ModelListRow row;
    row.id = entry.id;
    row.display_name = entry.display_name;
    row.source = "catalogue";
    row.status = "not installed";
    row.size_bytes = catalogue_entry_size_bytes(entry);
    finish(row);
  }
  // A Remove… that kept the previous version leaves nothing installed; the
  // row keeps that copy reachable for Delete previous and Open folder.
  for (const auto &package : scan.previous_packages) {
    if (has_row(rows, package.id)) {
      for (auto &row : rows) {
        if (row.id == package.id && !row.installed && row.status == "not installed")
          row.status = "not installed, previous version kept";
      }
      continue;
    }
    ModelListRow row;
    row.id = package.id;
    row.display_name = package.display_name;
    row.source = "per-user";
    row.status = "previous version only";
    row.size_bytes = folder_size(package.package_path);
    finish(row);
  }
  return rows;
}

const ModelAction &model_action(const ModelListRow &row, ModelActionKind kind) {
  return row.actions.at(static_cast<std::size_t>(kind));
}

std::string describe_model_details(const ModelListRow &row, const ModelRegistryScan &scan) {
  std::string text = row.display_name + " (" + row.id + ")\n";
  text += "Source: " + row.source + ", " + row.status + "\n";
  text += std::string(row.installed ? "Size on disk: " : row.source == "catalogue" ? "Download size: " : "Size: ") +
          describe_download_size(row.size_bytes) + "\n";
  text += std::string("Loaded: ") + (row.loaded ? "yes" : "no") + "\n";
  if (row.unavailable_reason) text += "Unavailable: " + *row.unavailable_reason + "\n";

  const auto *installed = find_package(scan.discovered_packages, row.id);
  if (installed == nullptr && row.damaged) installed = find_package(scan.damaged_packages, row.id);
  if (installed != nullptr && row.damaged) {
    for (const auto &unsupported : scan.unsupported_entries) {
      if (unsupported.path == installed->package_path) text += "Damaged: " + unsupported.reason + "\n";
    }
  }
  const auto *entry = catalogue_entry_for(row.id);
  const auto *described = installed != nullptr ? installed : find_package(scan.previous_packages, row.id);
  if (described != nullptr) {
    text += "Engine: " + described->engine + "\n";
    std::string languages;
    for (const auto &language : described->languages) languages += (languages.empty() ? "" : ", ") + language;
    if (!languages.empty()) text += "Languages: " + languages + "\n";
    if (installed != nullptr) text += "Folder: " + path_to_utf8(installed->package_path) + "\n";
    const auto license = described->manifest.find("license");
    if (license != described->manifest.end()) {
      text += "Licence: " + license->value("name", std::string()) + "\n" + license->value("url", std::string()) + "\n";
    }
  } else if (entry != nullptr) {
    text += "Engine: " + entry->engine + "\n";
    std::string languages;
    for (const auto &language : entry->languages) languages += (languages.empty() ? "" : ", ") + language;
    if (!languages.empty()) text += "Languages: " + languages + "\n";
    text += "Licence: " + describe_catalogue_license(*entry) + "\n" + entry->license.url + "\n";
  }

  std::string superseded;
  for (const auto &package : scan.superseded_packages) {
    if (package.id != row.id) continue;
    superseded += std::string("  ") + (package.bundled ? "[bundled] " : "[per-user] ") +
                  path_to_utf8(package.package_path) + "\n";
  }
  if (!superseded.empty()) text += "Superseded copies (not loaded):\n" + superseded;
  std::string previous;
  for (const auto &package : scan.previous_packages) {
    if (package.id == row.id) previous += "  " + path_to_utf8(package.package_path) + "\n";
  }
  if (!previous.empty()) text += "Previous version (Revert or Delete previous in More):\n" + previous;
  if (!text.empty() && text.back() == '\n') text.pop_back();
  return text;
}

std::string describe_unsupported_entries(const ModelRegistryScan &scan) {
  if (scan.unsupported_entries.empty()) return {};
  std::string text = "Unsupported packages:";
  for (const auto &entry : scan.unsupported_entries) {
    text += "\n" + path_to_utf8(entry.path) + "\n  Reason: " + entry.reason;
  }
  return text;
}

std::uintmax_t folder_size_on_disk(const std::filesystem::path &folder) {
  std::uintmax_t total = 0;
  std::error_code ec;
  for (auto item = std::filesystem::recursive_directory_iterator(folder, ec);
       !ec && item != std::filesystem::recursive_directory_iterator(); item.increment(ec)) {
    if (item->is_regular_file(ec)) total += item->file_size(ec);
  }
  return total;
}

}  // namespace tts_host
