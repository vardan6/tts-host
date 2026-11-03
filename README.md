# TTS Host

TTS Host is a local, offline text-to-speech host. It speaks arbitrary text with
real Kokoro-82M weights through an ONNX runner process, plays it through your
sound card or writes it to a WAV file, and manages installed model packages from
a native Windows tray icon and settings window. Nothing leaves the machine and
no cloud service is involved.

**Maturity.** Alpha. Speech, the desktop UI, model management, and the offline
Windows installer work and passed Windows acceptance, but no release has been
published yet. Playback and the UI are Windows-only. See "What works today"
below for the exact line, and `roadmap.md` for the sequence.

## Install (Windows)

Download from the
[GitHub Releases](https://github.com/vardan6/tts-host/releases) page and run it.
It needs no administrator rights and no network: everything it needs, models
included, is in the download.

- **Which file.** `tts-host-<version>-setup.exe` (Kokoro, English, any 64-bit
  Windows PC, about 0.4 GB) is the recommended download, and
  `tts-host-<version>-portable.zip` is the same without installing. Choose
  `tts-host-<version>-nvidia-setup.exe` (about 2.1 GB) only to try Qwen3-TTS on
  an NVIDIA GPU. It also installs over the standard version.
- **"Windows protected your PC".** Alpha builds are not code-signed. Click
  **More info**, then **Run anyway**.
- **Problems.** Report them in
  [GitHub Issues](https://github.com/vardan6/tts-host/issues).
- **Start at sign-in.** The installer offers it, ticked by default. You can
  change it later in Settings.
- **Use it.** TTS Host runs in the tray. Select text in any application and
  press **Ctrl+F8** to hear it. If an application does not expose its
  selection, TTS Host says so and asks you to copy the text. Right-click the tray icon for Settings and
  Now Playing. Settings can download, import, verify, and remove models.
- **Browser.** To read web pages in Chrome or Edge, open the extensions page,
  turn on **Developer mode**, choose **Load unpacked**, and pick the `extension`
  folder in the install folder (`%LOCALAPPDATA%\Programs\TTS Host\extension`).
  See "Browser playback controls" below.
- **Your data.** Configuration and added models live in
  `%LOCALAPPDATA%\TTS Host` and are kept on upgrade and uninstall.

## Quick start (development, Windows)

```powershell
git clone <this repo> && cd tts
.\compile-win.ps1
.\.venv-windows\Scripts\python tools\fetch_kokoro_weights.py
.\build\Debug\tts-host.exe --headless --synthesize "Hello from TTS Host." --play --config config.example.json
```

`compile-win.ps1` configures the Windows tree if needed, incrementally builds
all targets, runs CTest, and launches the desktop app with a separate, gitignored
`config.development.json`. It preserves other local settings and disables login
startup for every developer launch. Use it after code
changes to recompile, retest, and relaunch. CMake dependencies are kept in the
gitignored `cache/fetchcontent/windows-vs2022/` directory. For a full
from-scratch build, use `fresh-win.ps1`. The weights fetch is a one-time ~330 MB
download into `models/kokoro-en-v1/`. First-time toolchain setup is in
"Windows setup" below.

Then run it as a desktop app — no flags gives you the tray icon:

```powershell
.\build\Debug\tts-host.exe --config config.development.json
```

## Prerequisites

- CMake 3.28 or newer
- A C++20 compiler (Visual Studio 2022 Build Tools on Windows, or GCC/Clang
  on Linux)
- Internet access on the first configure, so CMake can download its JSON
  dependencies and the vendored espeak-ng and ONNX Runtime artifacts
- `espeak-ng` on Linux (`sudo apt-get install espeak-ng`, or run
  `./scripts/setup-dev-env.sh`) — required for phonemization

### Windows setup

The full Visual Studio IDE is not required for either tool below — only the
command-line/build-tools components.

**CMake.** If `cmake` is not recognized, install it and open a **new**
terminal window afterward (PATH changes do not apply to already-open shells):

```powershell
winget install --id Kitware.CMake --exact --silent --accept-package-agreements --accept-source-agreements
```

Verify with `cmake --version`. If it's still not found, rerun the installer
and make sure "Add CMake to system PATH" is selected, or add
`C:\Program Files\CMake\bin` to your PATH manually.

**C++ compiler.** Install the MSVC Build Tools with the C++ workload in one
step. Let the installer window run to completion — don't close it early:

```powershell
winget install --id Microsoft.VisualStudio.2022.BuildTools --force --override "--wait --add Microsoft.VisualStudio.Workload.VCTools --add Microsoft.VisualStudio.Component.VC.Tools.x86.x64 --add Microsoft.VisualStudio.Component.Windows10SDK --includeRecommended"
```

`wget`/`curl` alone won't work here: the Build Tools bootstrapper resolves
component packages from Microsoft's servers at install time, which `winget`
handles but a plain file download does not.

For a fully headless install (no UI at all — useful for CI or to avoid an
interactive dialog), use `--quiet` instead:

```powershell
winget install --id Microsoft.VisualStudio.2022.BuildTools --force --override "--quiet --wait --add Microsoft.VisualStudio.Workload.VCTools --add Microsoft.VisualStudio.Component.VC.Tools.x86.x64 --add Microsoft.VisualStudio.Component.Windows10SDK --includeRecommended"
```

Either way the install can take several minutes with `--wait` blocking until
it's done. Avoid `--passive` — it shows a dismissible UI that can exit before
the workload finishes registering, leaving a Build Tools *instance* installed
but without the C++ toolset (`cl.exe`) inside it.

After it finishes, verify in two steps, since finding an instance does not
by itself mean the C++ toolset installed:

```powershell
"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -all -products * -property installationPath
```

This must print an install path — if it prints nothing, no Build Tools
instance exists at all and the install failed outright. Then confirm the C++
toolset itself is present under that path (a `VC\Tools\MSVC\<version>\bin`
directory). If the instance exists but that directory doesn't, the VCTools
component didn't install — rerun the `winget install ... --override` command
above and let it run to completion.

**Generator.** From a plain `cmd.exe`/PowerShell (not a Developer Command
Prompt), CMake's default generator is NMake, which fails with
`CMAKE_CXX_COMPILER not set` because `nmake`/`cl` aren't on PATH. Always pass
the Visual Studio generator explicitly, as shown in "Build and test" below,
so configuring works from any shell. `compile-win.ps1` already does this.

Windows uses `build/`; WSL/Linux uses `build-linux/`. Keep these binary trees
separate because CMake records the generator, compiler, and source path in its
cache. `fresh-win.ps1` removes only the native Windows `build/` tree.

## Build and test

From the repository root, configure, build, and run the native tests.

Linux/macOS:

```sh
cmake -S . -B build-linux
cmake --build build-linux --parallel
ctest --test-dir build-linux --output-on-failure
```

Windows (after the setup above): the Visual Studio generator is
multi-configuration, so pass a configuration explicitly to both build and
test. The quick edit/retest/relaunch loop is `.\compile-win.ps1`; CMake
reconfigures only when the Windows build tree has not been configured yet.

```powershell
$fetchContentDir = Join-Path $PWD "cache\fetchcontent\windows-vs2022"
cmake -S . -B build -G "Visual Studio 17 2022" "-DFETCHCONTENT_BASE_DIR=$fetchContentDir"
cmake --build build --config Debug --parallel
ctest --test-dir build -C Debug --output-on-failure
```

`compile-win.ps1` runs these actions in order and launches the desktop app after
they succeed:

```powershell
cmake --build build --config Debug --parallel
ctest --test-dir build -C Debug --output-on-failure
.\build\Debug\tts-host.exe --config config.development.json
```

Close a running TTS Host from its tray menu before rebuilding or launching it
again.

For a generated clean that keeps the CMake configuration and downloaded
dependencies, use the generator-independent CMake target:

```powershell
cmake --build build --config Debug --target clean
```

The next build recompiles the cleaned targets. To clean and rebuild in one
command while keeping the configured build tree, add `--clean-first`:

```powershell
cmake --build build --config Debug --clean-first --parallel
```

Use `fresh-win.ps1` for a from-scratch configure, build, and test pass. It
deletes only `build/` and preserves the separate FetchContent cache. Pass
`-ResetDependencyCache` only when you also want to discard and redownload those
dependencies.

## Get a model

The host ships no weights in the repository. `tools/fetch_kokoro_weights.py`
downloads the real, full-precision Kokoro-82M ONNX model and one voice
embedding (Apache-2.0, from `onnx-community/Kokoro-82M-v1.0-ONNX` on Hugging
Face) into `models/kokoro-en-v1/`, alongside a generated `model.json` manifest.
The manifest records the package's content revision, so Settings shows it as
`current` and offers catalogue updates; `--no-download` rewrites the manifest
for an existing download (for example one fetched before revisions existed).
That is real product data (~330 MB), not a test fixture — `models/` is
gitignored and CTest never depends on it.

```powershell
.\scripts\setup-dev-env.ps1
.\.venv-windows\Scripts\python tools\fetch_kokoro_weights.py
```

```sh
./scripts/setup-dev-env.sh
.venv-linux/bin/python tools/fetch_kokoro_weights.py
```

This is the only step that needs Python, and only because it's a one-time
fetch — `tts-host` itself never invokes Python. You can also install a model
package you already have with `--import-model` or the settings window's
Import… button.

To see what the built-in download catalogue offers and what you already have:

```sh
./build-linux/tts-host --headless --list-catalogue --config config.example.json
```

The catalogue is compiled into the binary with each file's HTTPS URL, pinned
SHA-256, licence, and size. It offers Kokoro and the Qwen3-TTS 1.7B Base
package (Q4_K_M backbone, Q8_0 mmproj, and the public-domain LJ Speech clip
LJ001-0001 as its reference voice). The settings window downloads entries on
Windows; other platforms list them only — use the fetch script above for
Kokoro, and the Qwen evaluation steps below for Qwen.

When an installed catalogue model shows `update available`, the settings
window's Update button (or `--update-model <id>`) downloads the catalogue
revision into staging and swaps it in only after it verifies. The replaced
version is kept, one per model, under `.previous/` in the model directory:
Revert (`--revert-model <id>`) swaps back and Delete previous
(`--delete-previous-model <id>`) removes it. Remove superseded
(`--remove-superseded-model <id>`) deletes per-user copies that a newer
bundled copy shadows. `--verify-model <id>` hashes an installed catalogue
model against the catalogue and exits non-zero on any difference, without
repairing. Stop speech before updating or reverting the model
that is speaking.

## Qwen evaluation

The optional `qwen3-gguf` runner runs Qwen Base GGUF packages with CUDA. The
build copies the pinned runtime beside it into `runners/qwen3-gguf/`, where it
loads it from. On Windows, prepare the runtime once (Python is a development
tool only); configure finds it at `cache/qwen-evaluation/b11029/runtime` when
`TTS_HOST_QWEN_RUNTIME_DIR` is unset, and otherwise reports the runner disabled:

```powershell
python tools/fetch_qwen_evaluation_runtime.py --cache cache/qwen-evaluation/b11029 --out cache/qwen-evaluation/b11029/runtime
cmake -S . -B build
cmake --build build --config Debug --parallel
```

Building `tts-host` also builds its runners. Runtime files are copied only when
they differ, so rebuilding while a runner has them loaded succeeds.

Package local Qwen Base backbone/mmproj files and a reference recording whose
use is permitted. Include a text file recording that reference's provenance
and licence; the model's Apache-2.0 licence does not grant rights to an arbitrary
recording:

```powershell
python tools/package_qwen_evaluation.py --model path/to/model.gguf --mmproj path/to/mmproj.gguf --voice path/to/reference.wav --voice-license path/to/reference-license.txt --out path/to/models/qwen-evaluation --id qwen-evaluation
```

Point a separate evaluation config's `modelRegistry.directories` at that
`models` directory, set `startup.launchOnLogin` to `false`, then use `--model qwen-evaluation` with the
normal `--synthesize`, `--out`, and `--stats` options. The package has one
reference voice; `--language` on the packaging tool selects its primary `en`
or `ru` language. Speech speed 0.5–2.0 is applied by a pitch-preserving time
stretch in the runner.

The opt-in real-model check verifies resident reuse, speed scaling, over-context
rejection without restart, unload/reload, cancellation, and subsequent synthesis, writing
a WAV without playing it:

```powershell
./build/Debug/tts-host-qwen-integration-tests.exe ./build/Debug/tts-host-qwen3-gguf-runner.exe path/to/models/qwen-evaluation/model.json qwen-check.wav
```

Peak VRAM is reported as **unavailable**, not measured zero. Runner first-PCM
latency and complete-sentence audible latency differ; making Qwen a profile
default still needs the full latency, throughput, memory, and listening checks
(ADR 0009). Linux developers
can point the same CMake option at a matching b11029 shared-library runtime;
its CUDA libraries must be resolvable by the system loader.

## Speak something

All CLI usage requires `--headless`. Pick a text source
(`--synthesize`/`--stdin`/`--clipboard`) and a sink (`--out`/`--play`).

```sh
# Write a WAV file
./build-linux/tts-host --headless --synthesize "Hello there." --out hello.wav --config config.example.json

# Play through the configured output device (Windows only)
./build-linux/tts-host --headless --synthesize "Hello there." --play --config config.example.json

# Read a file, or whatever's on the clipboard
cat notes.md | ./build-linux/tts-host --headless --stdin --play --config config.example.json
./build-linux/tts-host --headless --clipboard --play --config config.example.json
```

Long text is split into sentence-scale chunks and played back as each chunk is
ready, with the next chunk synthesized while the current one plays. Markdown
and HTML markup is stripped before synthesis.

Useful additions:

- `--model <id>` — speak with a specific registry model instead of the
  configured default; the runner for its declared engine is selected for you.
- `--language <tag>` — name the `languageDefaults` key whose profile should
  speak. Without it the language is detected from the text's script (Latin →
  `en`, Cyrillic → `ru`, Armenian → `hy`), falling back to `en`.
- A profile can set `fallbackProfile`. The shipped English configuration tries
  `quality` first and uses the CPU-only `fast` profile (Kokoro) when its model
  or runner is unavailable.
- `--stats` — print peak RSS, peak VRAM, time to first chunk, and sample count
  after synthesis.
- `--list-models` — print discovered and unsupported model packages, each with
  an actionable reason.
- `--import-model <path>` — copy a model package directory into the first
  configured `modelRegistry.directories` entry, refusing anything the registry
  would reject.
- `--config <path>` / `--data-dir <path>` — point at a specific `config.json`
  or override the portable/installed data directory.
- `--help` — the authoritative flag list.

## Desktop app (Windows)

Run `tts-host.exe` with no `--headless` and it starts a tray icon; right-click
it for the context menu, including **Settings…** and **Quit**. `--settings`
opens the settings window directly. CLI flags can't be combined with either
mode — the host tells you so rather than ignoring them.

The settings window edits `config.json` in place and covers:

- output device — every active WASAPI render endpoint plus "System Default"
- server host and port (restart required; the API server itself isn't built yet)
- the default English profile, chosen from your `profiles` keys (restart
  required)
- installed models with their licence and licence URL, plus any unsupported
  package path and why it was rejected
- **Load** / **Unload** for the selected model, with a status line. One model
  is resident at a time, in a live runner process owned by that window.
- **Import…** — a folder picker running the same validation as
  `--import-model`
- automatic unload after `modelRegistry.idleUnloadSeconds`, and automatic
  refresh when `modelRegistry.watchForChanges` is on and a package appears in a
  watched directory

Note that a model loaded from the settings window is resident only for that
process — CLI synthesis in another terminal still launches its own runner.

## Browser playback controls

Load `extension/` as an unpacked Chrome extension and allow its exact
`chrome-extension://<id>` origin in `server.allowedOrigins` in the Host config.
Restart the Host after changing the allowlist. Use the extension options to set
its port to match `server.port` (default 7861).

The toolbar reads selected page text. Right-click a page for **Read article with
TTS Host** or **TTS Host playback controls**. Playback controls are also available
by right-clicking the extension icon or opening the link in extension settings.
They show Host playback state and played/generated seconds. Pause, Resume, and
Stop control the current read; Stop also clears queued reads. Closing the
control page leaves speech running. On connection failure, the page disables
controls and retries automatically.

## Configuration

`config.example.json` at the repository root is a working starting point and is
also what ships as `config.json` in the release zip. It is validated against
`schemas/config.schema.json` on load, and errors are reported by JSON path with
the last valid document kept. Keys that matter today: `audio.outputDevice`,
`modelRegistry.directories` / `watchForChanges` / `idleUnloadSeconds`,
`profiles`, and `languageDefaults`. `server.*` is reserved for the local API
server and has no effect yet.

## Package a release

A release is cut by one script, on Windows, from a clean `main` that matches
`origin`:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/release.ps1 -Version 0.1.0-alpha.1 -Variants standard -DryRun
powershell -ExecutionPolicy Bypass -File scripts/release.ps1 -Version 0.1.0-alpha.1 -Variants standard
```

It needs Visual Studio 2022, [Inno Setup 6](https://jrsoftware.org/isinfo.php)
(`winget install JRSoftware.InnoSetup`), a logged-in `gh`, and the
hand-written notes `docs/releases/v<version>.md`. It builds in a temporary
worktree, writes and commits the version there, runs every test, builds each
variant in `-Variants` (`standard`, `nvidia`, or both; standard is always
required), and refuses any artifact of 2 GiB or more. `-DryRun` stops there,
with the artifacts in `build-release/dist/dry-run/` and this checkout
untouched; rerun it as often as needed. A real run then tags the commit,
pushes the commit and tag together, and creates a draft GitHub pre-release with
the artifacts. You test the draft and publish it. The procedure and its
conditions are in the script's comments.

Each variant is built by `scripts/release-win.ps1 -Variant standard|nvidia`,
which `release.ps1` calls and which also runs on its own:

- **standard:** `tts-host.exe`, `runners/kokoro-onnx/` (runner, ONNX Runtime,
  espeak-ng), and `kokoro-en-v1`. Needs no Qwen asset and no NVIDIA GPU.
  Produces `tts-host-<version>-setup.exe` and `tts-host-<version>-portable.zip`.
- **nvidia:** the same plus `runners/qwen3-gguf/` (runner, llama.cpp, ggml,
  CUDA runtime) and `qwen3-tts-1.7b`. Produces
  `tts-host-<version>-nvidia-setup.exe`; its zip is over GitHub's 2 GiB limit
  and is not published.

The script configures `build-release/<variant>` with `TTS_HOST_RELEASE=ON` and
`TTS_HOST_RELEASE_VARIANT`, stages the variant's models once into
`cache/release-models` through the download catalogue, and packages them into
`build-release/<variant>/dist/`. A staged package is reused only while
`--verify-model <id>` finds it at the compiled catalogue's revision with every
file hash matching; otherwise it is downloaded again. Packaging fails if any
runner, library, licence, or model file of the variant is missing, if a
bundled package differs from the catalogue, or if the standard tree contains
any Qwen file. Then it extracts the zip into a temporary folder and has each of
the variant's models synthesize a sentence there, and compiles the installer
from the same tree. Both installers share one `AppId`, so either upgrades the
other. `-SkipVerify` skips the extracted-copy check and `-SkipInstaller` the
installer.

The installer needs no administrator rights. It installs into
`%LOCALAPPDATA%\Programs\TTS Host`, creates `%LOCALAPPDATA%\TTS Host\config.json`
on first install, and offers start at sign-in, pre-ticked. Upgrades and
uninstall keep everything in `%LOCALAPPDATA%\TTS Host`, including downloaded and
updated models.

A plain `cmake --build <dir> --target package` still zips whatever the build
has, with Kokoro from `models/kokoro-en-v1/` if present. That is a development
convenience, not a release.

## Other targets

- **Default (`all`)** — builds `tts-host`, every runner, and the test binaries.
  This is what `cmake --build build-linux ...` or `cmake --build build ...`
  does with no `--target`.
- **`list-models`** — rebuilds only `tts-host` and runs it against the registry
  test fixture, for a fast manual edit/build/run loop without a full CTest pass:

  ```sh
  cmake --build build-linux --target list-models               # Linux/macOS
  cmake --build build --config Debug --target list-models     # Windows
  ```

## Runners

`tts-host` never does inference itself. It launches a runner subprocess per
engine, named `tts-host-<engine>-runner`, and speaks a JSON-RPC control channel
(`Content-Length` framing) plus a framed binary audio channel to it — see
`docs/adr/0002-runner-protocol.md`.

- `tts-host-kokoro-onnx-runner` — the real engine: ONNX Runtime, espeak-ng
  phonemization mapped to Kokoro's vocabulary, `initialize`/`load`/`synthesize`/
  `unload`/`stats`.
- `tts-host-stub-runner` — a test fixture that emits deterministic audio, used
  by CTest and as the default when no model or runner is given.

## Dev-only Python tooling

Beyond the weights fetch above, `tools/` holds scripts that regenerate
checked-in test fixtures (for example the placeholder `.onnx` model used to
test-drive ONNX Runtime linking). This tooling is **never** a dependency of the
product: `tts-host`, its runners, and the released zip are native binaries and
never invoke Python. Regenerating fixtures is optional and only needed if
you're changing the fixture itself.

```sh
./scripts/setup-dev-env.sh
.venv-linux/bin/python tools/generate_kokoro_runner_fixtures.py
```

```powershell
.\scripts\setup-dev-env.ps1
.\.venv-windows\Scripts\python tools\generate_kokoro_runner_fixtures.py
```

Each platform gets its own venv directory (`.venv-linux/` or
`.venv-windows/`, both gitignored) so a WSL run and a native Windows run
never write into the same files. Their CMake trees are separate too:
`build-linux/` for WSL/Linux and `build/` for Windows.

## What works today

Working end to end:

- Config loading, schema validation with JSON-path errors, and reload on save
- Model-package discovery, manifest validation, path-safety checks, import, and
  directory watching
- Runner protocol: JSON-RPC control channel, framed audio channel,
  `initialize`/`load`/`synthesize`/`unload`/`stats`
- Real Kokoro-82M synthesis of arbitrary English text via espeak-ng
  phonemization
- Markdown and HTML normalization, sentence-scale chunking with lookahead
  overlap
- Text from `--synthesize`, stdin, or the clipboard; output to WAV or the
  speakers
- Language selection from an explicit tag or the text's script
- Windows tray icon and settings window: output device, server host/port,
  default profile, installed-model status and licences, load/unload, import,
  idle unload
- The curated download catalogue: download with progress and checksum
  verification, manual updates with Revert, verify and repair, and removal
- Qwen3-TTS 1.7B through llama.cpp on NVIDIA GPUs (bundled, not a default —
  ADR 0009), with speech-speed control
- The global read-selection hotkey (Ctrl+F8 by default), reading the UI
  Automation selection or the clipboard per `selection.capturePolicy`. New reads interrupt by default and queueing is opt-in
- Start at sign-in, the Now Playing window, and Pause/Resume/Stop
- Browser selection/article submission through the loopback API, plus browser
  playback state and Pause/Resume/Stop controls through WebSocket
- The two-model release zip and the per-user offline Windows installer

Not built yet:

- Synthetic Copy fallback for applications without UI Automation selection
  (those ask for a manual copy)
- Playback and the desktop UI on Linux and macOS — both are Windows-only, and
  other platforms fail with a clear not-implemented error
- Russian and Armenian models; promoting Qwen3-TTS to a fast/quality default

## Documentation

- `roadmap.md` — the implementation sequence, slice by slice
- `docs/requirements/product.md` — finished behavior, constraints, acceptance
  criteria, non-goals
- `docs/design/architecture.md` — architecture, protocols, tradeoffs
- `docs/adr/` — durable decisions and their rationale

## Licence

See `LICENSE`. Model weights carry their own licences, reported by
`--list-models`, `--list-catalogue`, and the settings window.
