#include "tts_host/settings_window.hpp"

#include <stdexcept>

#ifdef _WIN32
#include <windows.h>
#include <shobjidl.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

#include "tts_host/catalogue_download.hpp"
#include "tts_host/model_catalogue.hpp"
#include "tts_host/model_import.hpp"
#include "tts_host/model_registry.hpp"
#include "tts_host/model_session.hpp"
#include "tts_host/playback_sink.hpp"

namespace tts_host {
namespace {

constexpr int kOutputDeviceComboId = 101;
constexpr int kServerHostEditId = 102;
constexpr int kServerPortEditId = 103;
constexpr int kDefaultProfileComboId = 104;
constexpr int kModelComboId = 105;
constexpr int kLoadModelButtonId = 106;
constexpr int kUnloadModelButtonId = 107;
constexpr int kImportModelButtonId = 108;
constexpr int kCatalogueComboId = 109;
constexpr int kDownloadModelButtonId = 110;

// Polls ModelSessionManager::unload_if_idle against modelRegistry.idleUnloadSeconds
// (docs/design/architecture.md#desktop-integration). The settings window is the
// only place a model is loaded today, so its own message loop is the idle clock
// (tts_host/model_session.hpp); a 5 s poll trades timeout precision the config
// does not promise for not waking this process needlessly.
constexpr UINT_PTR kIdleTimerId = 1;
constexpr UINT kIdleTimerIntervalMs = 5000;

// Polls modelRegistry.directories for changes when modelRegistry.watchForChanges
// is true (docs/design/architecture.md#desktop-integration), so an install
// dropped into a registry directory by something other than Import… (a synced
// folder, a manual copy) still shows up without restarting. Same interval and
// mechanism as the idle timer: the settings window already owns a live message
// loop, so a second WM_TIMER poll avoids a second OS-specific watch API
// (ReadDirectoryChangesW) for one more low-frequency check.
constexpr UINT_PTR kDirectoryWatchTimerId = 2;
constexpr UINT kDirectoryWatchTimerIntervalMs = 5000;

// Owns the mutable working copy of the config the window edits; the ConfigDocument
// passed to run_settings_window is const, so control handlers write here instead.
// It also owns the resident model session, so closing the window unloads
// whatever the user loaded from it.
struct SettingsState {
  SettingsState(const ConfigDocument &config, const std::filesystem::path &runner_directory)
      : document(config), sessions(runner_directory) {}

  ConfigDocument document;
  ModelSessionManager sessions;
  // Model ids in combo-box order; the combo itself shows display names.
  std::vector<std::string> model_ids;
  // Catalogue entry ids in combo-box order, mirroring model_ids above.
  std::vector<std::string> catalogue_ids;
  HWND model_status_label = nullptr;
  HWND installed_models_display = nullptr;
  // The scan the model views currently reflect, so the directory-watch timer
  // can tell a real change from scan_model_registry re-validating the same
  // packages on every poll.
  ModelRegistryScan last_scan;
};

void refresh_model_views(SettingsState &state, HWND window);

std::wstring utf8_to_wide(const std::string &text) {
  if (text.empty()) {
    return std::wstring();
  }
  const int required = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
  std::wstring wide;
  wide.resize(static_cast<std::size_t>(required) - 1);
  MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, wide.data(), required);
  return wide;
}

std::string wide_to_utf8(const wchar_t *wide_text) {
  const int required = WideCharToMultiByte(CP_UTF8, 0, wide_text, -1, nullptr, 0, nullptr, nullptr);
  std::string text;
  if (required > 1) {
    text.resize(static_cast<std::size_t>(required) - 1);
    WideCharToMultiByte(CP_UTF8, 0, wide_text, -1, text.data(), required, nullptr, nullptr);
  }
  return text;
}

// Writes the settings window's edits back to the config file
// (docs/design/architecture.md#desktop-integration's "Live reload" -- the
// host-side file watcher that applies these live is a separate, not-yet-built
// piece).
void save_config(const SettingsState &state) {
  std::ofstream output(state.document.paths.config_path);
  if (!output) {
    throw std::runtime_error("failed to write " + state.document.paths.config_path.string());
  }
  output << state.document.value.dump(2) << '\n';
}

// Fills the combo box with "System Default" plus every enumerated output
// device, keeping the currently configured device selected even if it is no
// longer attached (so the control reflects the real config value rather than
// silently changing it out from under the user).
void populate_output_device_combo(HWND combo, const std::string &current_value) {
  SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"System Default"));

  std::vector<std::string> devices;
  try {
    devices = list_output_devices();
  } catch (const std::exception &) {
    // Enumeration can fail (no audio subsystem, no devices); the user can
    // still pick "System Default" or keep whatever value was already pinned.
  }

  int current_index = current_value == kSystemDefaultOutputDevice ? 0 : -1;
  int index = 1;
  for (const auto &device : devices) {
    SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(utf8_to_wide(device).c_str()));
    if (device == current_value) {
      current_index = index;
    }
    ++index;
  }

  if (current_index < 0) {
    SendMessageW(combo, CB_ADDSTRING, 0,
                reinterpret_cast<LPARAM>(utf8_to_wide(current_value).c_str()));
    current_index = index;
  }

  SendMessageW(combo, CB_SETCURSEL, current_index, 0);
}

void on_output_device_selected(SettingsState &state, HWND combo) {
  const int index = static_cast<int>(SendMessageW(combo, CB_GETCURSEL, 0, 0));
  if (index < 0) {
    return;
  }
  wchar_t buffer[256];
  SendMessageW(combo, CB_GETLBTEXT, index, reinterpret_cast<LPARAM>(buffer));
  const std::string selected = wide_to_utf8(buffer);
  state.document.value["audio"]["outputDevice"] =
      selected == "System Default" ? kSystemDefaultOutputDevice : selected;
  save_config(state);
}

// Controls created with CreateWindowExW inherit no font, so Windows draws them
// with the ancient bitmap SYSTEM_FONT: bold, oversized, and wide enough to
// overflow the fixed pixel layout below (it clipped the Import… caption and the
// default-profile label). Use the shell's own UI font — Segoe UI on Windows
// 10/11 — which is the metric the layout coordinates assume. The font outlives
// every control that selects it, hence the function-local static.
HFONT shell_ui_font() {
  static const HFONT font = []() -> HFONT {
    NONCLIENTMETRICSW metrics{};
    metrics.cbSize = sizeof(metrics);
    if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0) != 0) {
      if (const HFONT created = CreateFontIndirectW(&metrics.lfMessageFont)) {
        return created;
      }
    }
    return static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
  }();
  return font;
}

BOOL CALLBACK apply_shell_ui_font(HWND child, LPARAM font) {
  SendMessageW(child, WM_SETFONT, static_cast<WPARAM>(font), MAKELPARAM(TRUE, 0));
  return TRUE;
}

// Fills the combo box with every profile name from config's "profiles" map,
// preselecting whichever one languageDefaults.en currently names. English is
// the only language surfaced end to end so far (see roadmap and
// resolve_default_runner_selection in src/main.cpp).
void populate_default_profile_combo(HWND combo, const ConfigDocument &document) {
  const auto &profiles = document.value.at("profiles");
  const std::string current_value = document.value.at("languageDefaults").contains("en")
                                        ? document.value.at("languageDefaults").at("en").get<std::string>()
                                        : std::string();

  int index = 0;
  int current_index = -1;
  for (auto it = profiles.begin(); it != profiles.end(); ++it, ++index) {
    SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(utf8_to_wide(it.key()).c_str()));
    if (it.key() == current_value) {
      current_index = index;
    }
  }

  if (current_index < 0 && !current_value.empty()) {
    SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(utf8_to_wide(current_value).c_str()));
    current_index = index;
  }

  SendMessageW(combo, CB_SETCURSEL, current_index, 0);
}

void on_default_profile_selected(SettingsState &state, HWND combo) {
  const int index = static_cast<int>(SendMessageW(combo, CB_GETCURSEL, 0, 0));
  if (index < 0) {
    return;
  }
  wchar_t buffer[256];
  SendMessageW(combo, CB_GETLBTEXT, index, reinterpret_cast<LPARAM>(buffer));
  state.document.value["languageDefaults"]["en"] = wide_to_utf8(buffer);
  save_config(state);
}

// server.host/server.port cannot be applied live (docs/design/architecture.md#live-reload),
// so these are only written on focus loss, once the user has finished editing, rather than
// on every keystroke like the output-device combo's immediate selection.

void on_server_host_changed(SettingsState &state, HWND edit) {
  wchar_t buffer[256];
  GetWindowTextW(edit, buffer, static_cast<int>(std::size(buffer)));
  const std::string host = wide_to_utf8(buffer);
  if (host.empty()) {
    return;  // schema requires minLength 1; keep the last valid host instead of writing an empty one.
  }
  state.document.value["server"]["host"] = host;
  save_config(state);
}

void on_server_port_changed(SettingsState &state, HWND edit) {
  wchar_t buffer[16];
  GetWindowTextW(edit, buffer, static_cast<int>(std::size(buffer)));
  const std::string text = wide_to_utf8(buffer);
  if (text.empty()) {
    return;
  }
  const int port = _wtoi(buffer);
  if (port < 1 || port > 65535) {
    return;  // out of the schema's range; keep the last valid port instead of writing an invalid one.
  }
  state.document.value["server"]["port"] = port;
  save_config(state);
}

// Fills the combo box with every discovered package and records the matching
// model ids, which is what load() actually takes. Selection defaults to the
// first package so the Load button is meaningful without a pick first.
void populate_model_combo(HWND combo, SettingsState &state, const ModelRegistryScan &scan) {
  for (const auto &package : scan.discovered_packages) {
    const auto label = package.display_name + " (" + package.id + ")";
    SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(utf8_to_wide(label).c_str()));
    state.model_ids.push_back(package.id);
  }
  if (!state.model_ids.empty()) {
    SendMessageW(combo, CB_SETCURSEL, 0, 0);
  }
}

void set_model_status(SettingsState &state, const std::wstring &text) {
  if (state.model_status_label == nullptr) {
    return;
  }
  SetWindowTextW(state.model_status_label, text.c_str());
  // Loading blocks this thread on the runner handshake, so force the label to
  // repaint now rather than after the wait it is announcing.
  UpdateWindow(state.model_status_label);
}

void report_session_status(SettingsState &state) {
  const auto &status = state.sessions.status();
  if (!status.loaded) {
    set_model_status(state, L"No model loaded.");
    return;
  }
  set_model_status(state, L"Loaded: " + utf8_to_wide(status.display_name) + L" (" +
                              utf8_to_wide(status.model_id) + L", " +
                              utf8_to_wide(status.engine) + L")");
}

void on_load_model_clicked(SettingsState &state, HWND combo) {
  const int index = static_cast<int>(SendMessageW(combo, CB_GETCURSEL, 0, 0));
  if (index < 0 || static_cast<std::size_t>(index) >= state.model_ids.size()) {
    set_model_status(state, L"Select a model to load.");
    return;
  }

  const auto &model_id = state.model_ids[static_cast<std::size_t>(index)];
  set_model_status(state, L"Loading " + utf8_to_wide(model_id) + L"…");
  try {
    state.sessions.load(model_id, state.document);
  } catch (const std::exception &error) {
    // A model that will not load is ordinary (missing runner, bad weights);
    // report it in the window instead of taking the process down.
    set_model_status(state, L"Load failed: " + utf8_to_wide(error.what()));
    return;
  }
  report_session_status(state);
}

// Fills the combo box with every compiled-in catalogue entry
// (docs/design/architecture.md#download-catalogue), labelled the same way
// `--list-catalogue` prints them: GET/HAVE, size, and licence, so the choice
// before downloading is visible without a separate details view.
void populate_catalogue_combo(HWND combo, SettingsState &state, const ModelRegistryScan &scan) {
  for (const auto &entry : model_catalogue()) {
    const bool installed = catalogue_entry_installed(entry, scan);
    const auto label = std::string(installed ? "HAVE " : "GET  ") + entry.display_name + " (" +
                        describe_download_size(catalogue_entry_size_bytes(entry)) + ", " +
                        describe_catalogue_license(entry) + ")";
    SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(utf8_to_wide(label).c_str()));
    state.catalogue_ids.push_back(entry.id);
  }
  if (!state.catalogue_ids.empty()) {
    SendMessageW(combo, CB_SETCURSEL, 0, 0);
  }
}

void on_download_model_clicked(SettingsState &state, HWND combo, HWND window) {
  const int index = static_cast<int>(SendMessageW(combo, CB_GETCURSEL, 0, 0));
  if (index < 0 || static_cast<std::size_t>(index) >= state.catalogue_ids.size()) {
    set_model_status(state, L"Select a catalogue entry to download.");
    return;
  }

  const auto &entries = model_catalogue();
  const auto &entry_id = state.catalogue_ids[static_cast<std::size_t>(index)];
  const auto found = std::find_if(entries.begin(), entries.end(),
                                  [&](const auto &entry) { return entry.id == entry_id; });
  if (found == entries.end()) {
    return;
  }
  if (catalogue_entry_installed(*found, state.last_scan)) {
    set_model_status(state, utf8_to_wide(found->id) + L" is already installed.");
    return;
  }

  const auto &directories = state.document.value.at("modelRegistry").at("directories");
  if (directories.empty()) {
    set_model_status(state, L"No model directory is configured to download into.");
    return;
  }
  const auto destination_root = resolve_registry_directory(
      state.document.paths.config_path, directories.front().get<std::string>());

  try {
    set_model_status(state, L"Downloading " + utf8_to_wide(found->display_name) + L"…");
    download_catalogue_entry(
        *found, destination_root, state.document, fetch_url_to_file,
        [&state](const DownloadProgress &progress) {
          set_model_status(state, utf8_to_wide(progress.relative_path) + L": " +
                                      std::to_wstring(progress.bytes_downloaded) + L" / " +
                                      std::to_wstring(progress.bytes_total) + L" bytes");
        });
    refresh_model_views(state, window);
    set_model_status(state, L"Downloaded " + utf8_to_wide(found->display_name));
  } catch (const std::exception &error) {
    // Transport failures and checksum mismatches are ordinary (flaky network,
    // corrupted partial file); report them in the window instead of taking
    // the process down.
    set_model_status(state, L"Download failed: " + utf8_to_wide(error.what()));
  }
}

void on_idle_timer(SettingsState &state) {
  const auto idle_unload_seconds =
      state.document.value["modelRegistry"]["idleUnloadSeconds"].get<int>();
  if (state.sessions.unload_if_idle(std::chrono::seconds(idle_unload_seconds))) {
    report_session_status(state);
  }
}

void on_unload_model_clicked(SettingsState &state) {
  try {
    state.sessions.unload();
  } catch (const std::exception &error) {
    set_model_status(state, L"Unload failed: " + utf8_to_wide(error.what()));
    return;
  }
  report_session_status(state);
}

// Model manifests are validated by scan_model_registry before reaching this
// display, which stays read-only: it shows licence information for every
// discovered package, while the combo box above it is what the user acts on.
std::wstring format_installed_models(const ModelRegistryScan &scan) {
  std::wstring text;
  if (scan.discovered_packages.empty()) {
    text = L"No compatible installed models were found.";
  }
  for (const auto &package : scan.discovered_packages) {
    if (!text.empty()) {
      text += L"\r\n\r\n";
    }
    const auto &license = package.manifest.at("license");
    text += utf8_to_wide(package.display_name);
    text += L" (" + utf8_to_wide(package.id) + L")\r\nLicence: ";
    text += utf8_to_wide(license.at("name").get<std::string>());
    text += L"\r\n";
    text += utf8_to_wide(license.at("url").get<std::string>());
  }

  if (!scan.unsupported_entries.empty()) {
    if (!text.empty()) {
      text += L"\r\n\r\n";
    }
    text += L"Unsupported packages:";
    for (const auto &entry : scan.unsupported_entries) {
      text += L"\r\n";
      text += entry.path.wstring();
      text += L"\r\n  Reason: ";
      text += utf8_to_wide(entry.reason);
    }
  }
  return text;
}

// Folder picker for Import…: the user chooses a package directory (the folder
// holding model.json), not a file, because a model package is a directory
// (docs/design/architecture.md#model-packages-and-discovery). Returns an empty
// string when the dialog is cancelled or unavailable.
std::wstring pick_model_package_folder(HWND owner) {
  std::wstring chosen;
  const HRESULT initialize_result =
      CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
  // RPC_E_CHANGED_MODE means COM is already initialized on this thread in
  // another mode; the dialog still works, but this call must not uninitialize
  // what it did not initialize.
  const bool initialized_here = SUCCEEDED(initialize_result);

  IFileOpenDialog *dialog = nullptr;
  if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                 IID_PPV_ARGS(&dialog)))) {
    DWORD options = 0;
    if (SUCCEEDED(dialog->GetOptions(&options))) {
      dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
    }
    dialog->SetTitle(L"Select a model package folder");
    if (SUCCEEDED(dialog->Show(owner))) {
      IShellItem *item = nullptr;
      if (SUCCEEDED(dialog->GetResult(&item))) {
        PWSTR path = nullptr;
        if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
          chosen = path;
          CoTaskMemFree(path);
        }
        item->Release();
      }
    }
    dialog->Release();
  }

  if (initialized_here) {
    CoUninitialize();
  }
  return chosen;
}

// Re-reads the registry into both model views and records the scan they now
// reflect. Importing and directory watching are what changes what is
// installed while the window is open; the resident session is left alone, so
// a model loaded before either stays loaded.
void refresh_model_views(SettingsState &state, HWND window) {
  state.last_scan = scan_model_registry(state.document);
  const HWND combo = GetDlgItem(window, kModelComboId);
  if (combo != nullptr) {
    SendMessageW(combo, CB_RESETCONTENT, 0, 0);
    state.model_ids.clear();
    populate_model_combo(combo, state, state.last_scan);
  }
  if (state.installed_models_display != nullptr) {
    SetWindowTextW(state.installed_models_display, format_installed_models(state.last_scan).c_str());
  }
  const HWND catalogue_combo = GetDlgItem(window, kCatalogueComboId);
  if (catalogue_combo != nullptr) {
    SendMessageW(catalogue_combo, CB_RESETCONTENT, 0, 0);
    state.catalogue_ids.clear();
    populate_catalogue_combo(catalogue_combo, state, state.last_scan);
  }
}

// Polls modelRegistry.directories when modelRegistry.watchForChanges is true;
// a no-op scan (nothing added, removed, or changed reason) skips the refresh
// so the combo selection and display scroll position are not disturbed on
// every 5 s tick.
void on_directory_watch_timer(SettingsState &state, HWND window) {
  if (!state.document.value["modelRegistry"]["watchForChanges"].get<bool>()) {
    return;
  }
  const auto scan = scan_model_registry(state.document);
  if (registry_scan_changed(state.last_scan, scan)) {
    refresh_model_views(state, window);
  }
}

void on_import_model_clicked(SettingsState &state, HWND window) {
  const auto folder = pick_model_package_folder(window);
  if (folder.empty()) {
    return;  // Cancelled; leave the status line saying whatever it said before.
  }

  set_model_status(state, L"Importing " + folder + L"…");
  try {
    const auto imported = import_model_package(std::filesystem::path(folder), state.document);
    refresh_model_views(state, window);
    set_model_status(state, L"Imported " + utf8_to_wide(imported.display_name) + L" (" +
                                utf8_to_wide(imported.id) + L")");
  } catch (const std::exception &error) {
    // Invalid, incomplete, and duplicate packages are ordinary user mistakes:
    // report the registry's actionable reason in the window.
    set_model_status(state, L"Import failed: " + utf8_to_wide(error.what()));
  }
}

LRESULT CALLBACK settings_window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
  switch (message) {
    // STATIC labels and the read-only licence display send this to ask what
    // brush to paint their background with; without a handler DefWindowProc
    // answers with the white COLOR_WINDOW brush, which visibly mismatches the
    // COLOR_BTNFACE-grey the window itself is painted with below.
    case WM_CTLCOLORSTATIC: {
      const HDC dc = reinterpret_cast<HDC>(wparam);
      SetBkMode(dc, TRANSPARENT);
      return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_BTNFACE));
    }
    case WM_DESTROY:
      KillTimer(window, kIdleTimerId);
      KillTimer(window, kDirectoryWatchTimerId);
      PostQuitMessage(0);
      return 0;
    case WM_TIMER: {
      auto *state = reinterpret_cast<SettingsState *>(GetWindowLongPtrW(window, GWLP_USERDATA));
      if (state == nullptr) {
        return 0;
      }
      if (wparam == kIdleTimerId) {
        on_idle_timer(*state);
      } else if (wparam == kDirectoryWatchTimerId) {
        on_directory_watch_timer(*state, window);
      }
      return 0;
    }
    case WM_COMMAND: {
      auto *state = reinterpret_cast<SettingsState *>(GetWindowLongPtrW(window, GWLP_USERDATA));
      if (state == nullptr) {
        return 0;
      }
      if (LOWORD(wparam) == kOutputDeviceComboId && HIWORD(wparam) == CBN_SELCHANGE) {
        on_output_device_selected(*state, reinterpret_cast<HWND>(lparam));
      } else if (LOWORD(wparam) == kServerHostEditId && HIWORD(wparam) == EN_KILLFOCUS) {
        on_server_host_changed(*state, reinterpret_cast<HWND>(lparam));
      } else if (LOWORD(wparam) == kServerPortEditId && HIWORD(wparam) == EN_KILLFOCUS) {
        on_server_port_changed(*state, reinterpret_cast<HWND>(lparam));
      } else if (LOWORD(wparam) == kDefaultProfileComboId && HIWORD(wparam) == CBN_SELCHANGE) {
        on_default_profile_selected(*state, reinterpret_cast<HWND>(lparam));
      } else if (LOWORD(wparam) == kLoadModelButtonId && HIWORD(wparam) == BN_CLICKED) {
        on_load_model_clicked(*state, GetDlgItem(window, kModelComboId));
      } else if (LOWORD(wparam) == kUnloadModelButtonId && HIWORD(wparam) == BN_CLICKED) {
        on_unload_model_clicked(*state);
      } else if (LOWORD(wparam) == kImportModelButtonId && HIWORD(wparam) == BN_CLICKED) {
        on_import_model_clicked(*state, window);
      } else if (LOWORD(wparam) == kDownloadModelButtonId && HIWORD(wparam) == BN_CLICKED) {
        on_download_model_clicked(*state, GetDlgItem(window, kCatalogueComboId), window);
      }
      return 0;
    }
    default:
      return DefWindowProcW(window, message, wparam, lparam);
  }
}

}  // namespace

void run_settings_window(const ConfigDocument &document,
                         const std::filesystem::path &runner_directory) {
  const wchar_t *kClassName = L"TtsHostSettingsWindow";
  const HINSTANCE instance = GetModuleHandleW(nullptr);

  WNDCLASSW window_class{};
  window_class.lpfnWndProc = settings_window_proc;
  window_class.hInstance = instance;
  window_class.lpszClassName = kClassName;
  // Match the COLOR_BTNFACE grey returned for STATIC controls in
  // WM_CTLCOLORSTATIC above; otherwise the window paints white while labels
  // paint grey, producing a visible checkerboard of background colors.
  window_class.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
  // IDC_ARROW expands through the ANSI-generic MAKEINTRESOURCE macro when
  // UNICODE is not globally defined. This window intentionally calls the
  // explicit wide API, so use the wide resource form as well (32512 is the
  // documented resource id behind IDC_ARROW).
  window_class.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
  // Tolerate re-registration: the tray's Settings… item can invoke this
  // function repeatedly within one process (see tray_icon.cpp), and Windows
  // rejects registering the same class name twice.
  if (!RegisterClassW(&window_class) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
    throw std::runtime_error("failed to register the settings window class");
  }

  SettingsState state(document, runner_directory);

  // A plain visible top-level window with output-device, server host/port,
  // default-English-profile, model load/unload/import, catalogue download,
  // and installed-model licence display controls. Hotkeys arrive in a later
  // slice as additional controls added to this window.
  const HWND window =
      CreateWindowExW(0, kClassName, L"TTS Host Settings", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT,
                      CW_USEDEFAULT, 512, 648, nullptr, nullptr, instance, nullptr);
  if (!window) {
    throw std::runtime_error("failed to create the settings window");
  }
  SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&state));

  // Layout: a 152 px label column at x=16 (wide enough for the longest caption,
  // "Default English profile:", in the shell UI font) and a control column at
  // x=176, both inside a 512 px window's ~496 px client area.
  CreateWindowExW(0, L"STATIC", L"Output device:", WS_CHILD | WS_VISIBLE, 16, 16, 152, 24, window,
                  nullptr, instance, nullptr);
  const HWND device_combo = CreateWindowExW(
      0, L"COMBOBOX", nullptr, WS_CHILD | WS_VISIBLE | WS_VSCROLL | CBS_DROPDOWNLIST, 176, 12, 300,
      200, window, reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kOutputDeviceComboId)), instance,
      nullptr);
  if (!device_combo) {
    throw std::runtime_error("failed to create the output-device control");
  }
  populate_output_device_combo(
      device_combo, state.document.value["audio"]["outputDevice"].get<std::string>());

  CreateWindowExW(0, L"STATIC", L"Server host:", WS_CHILD | WS_VISIBLE, 16, 56, 152, 24, window,
                  nullptr, instance, nullptr);
  const HWND host_edit = CreateWindowExW(
      WS_EX_CLIENTEDGE, L"EDIT",
      utf8_to_wide(state.document.value["server"]["host"].get<std::string>()).c_str(),
      WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 176, 52, 180, 24, window,
      reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kServerHostEditId)), instance, nullptr);
  if (!host_edit) {
    throw std::runtime_error("failed to create the server-host control");
  }

  CreateWindowExW(0, L"STATIC", L"Server port:", WS_CHILD | WS_VISIBLE, 16, 88, 152, 24, window,
                  nullptr, instance, nullptr);
  const HWND port_edit = CreateWindowExW(
      WS_EX_CLIENTEDGE, L"EDIT",
      std::to_wstring(state.document.value["server"]["port"].get<int>()).c_str(),
      WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_NUMBER, 176, 84, 80, 24, window,
      reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kServerPortEditId)), instance, nullptr);
  if (!port_edit) {
    throw std::runtime_error("failed to create the server-port control");
  }

  CreateWindowExW(0, L"STATIC", L"Restart required to take effect.", WS_CHILD | WS_VISIBLE, 176,
                  116, 260, 20, window, nullptr, instance, nullptr);

  CreateWindowExW(0, L"STATIC", L"Default English profile:", WS_CHILD | WS_VISIBLE, 16, 148, 152, 24,
                  window, nullptr, instance, nullptr);
  const HWND default_profile_combo = CreateWindowExW(
      0, L"COMBOBOX", nullptr, WS_CHILD | WS_VISIBLE | WS_VSCROLL | CBS_DROPDOWNLIST, 176, 144, 300,
      200, window, reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kDefaultProfileComboId)), instance,
      nullptr);
  if (!default_profile_combo) {
    throw std::runtime_error("failed to create the default-profile control");
  }
  populate_default_profile_combo(default_profile_combo, document);
  CreateWindowExW(0, L"STATIC", L"Restart required to take effect.", WS_CHILD | WS_VISIBLE, 176, 172,
                  260, 20, window, nullptr, instance, nullptr);

  CreateWindowExW(0, L"STATIC", L"Model:", WS_CHILD | WS_VISIBLE, 16, 204, 152, 24, window, nullptr,
                  instance, nullptr);
  const HWND model_combo = CreateWindowExW(
      0, L"COMBOBOX", nullptr, WS_CHILD | WS_VISIBLE | WS_VSCROLL | CBS_DROPDOWNLIST, 176, 200, 300,
      200, window, reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kModelComboId)), instance, nullptr);
  if (!model_combo) {
    throw std::runtime_error("failed to create the model-selection control");
  }
  state.last_scan = scan_model_registry(document);
  populate_model_combo(model_combo, state, state.last_scan);

  const HWND load_button = CreateWindowExW(
      0, L"BUTTON", L"Load", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 176, 232, 90, 26, window,
      reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kLoadModelButtonId)), instance, nullptr);
  const HWND unload_button = CreateWindowExW(
      0, L"BUTTON", L"Unload", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 276, 232, 90, 26, window,
      reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kUnloadModelButtonId)), instance, nullptr);
  const HWND import_button = CreateWindowExW(
      0, L"BUTTON", L"Import…", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 376, 232, 100, 26, window,
      reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kImportModelButtonId)), instance, nullptr);
  if (!load_button || !unload_button || !import_button) {
    throw std::runtime_error("failed to create the model load/unload/import controls");
  }

  state.model_status_label = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE, 16, 266, 460,
                                             20, window, nullptr, instance, nullptr);
  if (state.model_status_label == nullptr) {
    throw std::runtime_error("failed to create the model status display");
  }
  // A loaded model stays resident only while this window is open, so the
  // window always opens with nothing loaded (see tts_host/model_session.hpp).
  report_session_status(state);

  CreateWindowExW(0, L"STATIC", L"Catalogue:", WS_CHILD | WS_VISIBLE, 16, 298, 152, 24, window, nullptr,
                  instance, nullptr);
  const HWND catalogue_combo = CreateWindowExW(
      0, L"COMBOBOX", nullptr, WS_CHILD | WS_VISIBLE | WS_VSCROLL | CBS_DROPDOWNLIST, 176, 294, 300,
      200, window, reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kCatalogueComboId)), instance,
      nullptr);
  if (!catalogue_combo) {
    throw std::runtime_error("failed to create the catalogue-selection control");
  }
  populate_catalogue_combo(catalogue_combo, state, state.last_scan);

  const HWND download_button = CreateWindowExW(
      0, L"BUTTON", L"Download", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 176, 326, 100, 26, window,
      reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kDownloadModelButtonId)), instance, nullptr);
  if (!download_button) {
    throw std::runtime_error("failed to create the download-model control");
  }

  CreateWindowExW(0, L"STATIC", L"Installed models and licences:", WS_CHILD | WS_VISIBLE, 16, 362,
                  240, 24, window, nullptr, instance, nullptr);
  const auto model_text = format_installed_models(state.last_scan);
  state.installed_models_display = CreateWindowExW(
      WS_EX_CLIENTEDGE, L"EDIT", model_text.c_str(),
      WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_AUTOVSCROLL | ES_MULTILINE | ES_READONLY, 16, 386, 460,
      170, window, nullptr, instance, nullptr);
  if (state.installed_models_display == nullptr) {
    throw std::runtime_error("failed to create the installed-model licence display");
  }

  // Every control above is created without a font, so give them all the shell
  // UI font in one pass now that they exist.
  EnumChildWindows(window, apply_shell_ui_font, reinterpret_cast<LPARAM>(shell_ui_font()));

  SetTimer(window, kIdleTimerId, kIdleTimerIntervalMs, nullptr);
  if (state.document.value["modelRegistry"]["watchForChanges"].get<bool>()) {
    SetTimer(window, kDirectoryWatchTimerId, kDirectoryWatchTimerIntervalMs, nullptr);
  }

  ShowWindow(window, SW_SHOWNORMAL);
  UpdateWindow(window);

  MSG message;
  while (GetMessageW(&message, nullptr, 0, 0) > 0) {
    TranslateMessage(&message);
    DispatchMessageW(&message);
  }
}

}  // namespace tts_host

#else

namespace tts_host {

void run_settings_window(const ConfigDocument &, const std::filesystem::path &) {
  throw std::runtime_error(
      "the settings window is not implemented on this platform yet (Windows only, see "
      "docs/adr/0007-native-ui-per-platform.md)");
}

}  // namespace tts_host

#endif
