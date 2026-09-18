# TTS Host

TTS Host is a local, offline text-to-speech host. It speaks arbitrary text with
real Kokoro-82M weights through an ONNX runner process, plays it through your
sound card or writes it to a WAV file, and manages installed model packages from
a native Windows tray icon and settings window. Nothing leaves the machine and
no cloud service is involved.

**Maturity.** Speech works end to end today and the desktop UI is usable, but
this is not a packaged consumer release: playback and the UI are Windows-only,
there is no installer (the release artifact is a zip), and the local HTTP API,
global hotkeys, and browser extension are not built yet. See
"What works today" below for the exact line, and `roadmap.md` for the sequence.

## Quick start (Windows)

```powershell
git clone <this repo> && cd tts
.\compile-win.ps1
.\.venv-windows\Scripts\python tools\fetch_kokoro_weights.py
.\build\Debug\tts-host.exe --headless --synthesize "Hello from TTS Host." --play --config config.example.json
```

`compile-win.ps1` wipes `build/`, configures, builds everything, and runs the
full CTest suite, logging to `logs/`. The weights fetch is a one-time ~330 MB
download into `models/kokoro-en-v1/`. First-time toolchain setup is in
"Windows setup" below.

Then run it as a desktop app — no flags gives you the tray icon:

```powershell
.\build\Debug\tts-host.exe --config config.example.json
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

**Don't mix WSL and native Windows for the same `build/` directory.** CMake
caches the exact source/build paths and toolchain, so configuring from WSL
and then building from `cmd.exe`/PowerShell (or vice versa) fails with
"CMakeCache.txt directory ... is different" or missing-executable errors.
Likewise, changing generators (e.g. after hitting the NMake error above)
fails with "Does not match the generator used previously". In both cases,
delete `build/` and reconfigure from scratch in the environment you intend
to build with:

```powershell
rmdir /s /q build
```

Then reconfigure using the Windows commands in "Build and test" below.

## Build and test

From the repository root, configure, build, and run the native tests.

Linux/macOS:

```sh
cmake -S . -B build
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Windows (after the setup above): the Visual Studio generator is
multi-configuration, so pass a configuration explicitly to both build and
test. `.\compile-win.ps1` runs all three as a clean rebuild.

```powershell
cmake -S . -B build -G "Visual Studio 17 2022"
cmake --build build --config Debug --parallel
ctest --test-dir build -C Debug --output-on-failure
```

## Get a model

The host ships no weights in the repository. `tools/fetch_kokoro_weights.py`
downloads the real, full-precision Kokoro-82M ONNX model and one voice
embedding (Apache-2.0, from `onnx-community/Kokoro-82M-v1.0-ONNX` on Hugging
Face) into `models/kokoro-en-v1/`, alongside a generated `model.json` manifest.
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
./build/tts-host --headless --list-catalogue --config config.example.json
```

The catalogue is compiled into the binary with each file's HTTPS URL, pinned
SHA-256, licence, and size, but downloading from it is not implemented yet —
use the fetch script above.

## Speak something

All CLI usage requires `--headless`. Pick a text source
(`--synthesize`/`--stdin`/`--clipboard`) and a sink (`--out`/`--play`).

```sh
# Write a WAV file
./build/tts-host --headless --synthesize "Hello there." --out hello.wav --config config.example.json

# Play through the configured output device (Windows only)
./build/tts-host --headless --synthesize "Hello there." --play --config config.example.json

# Read a file, or whatever's on the clipboard
cat notes.md | ./build/tts-host --headless --stdin --play --config config.example.json
./build/tts-host --headless --clipboard --play --config config.example.json
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

## Configuration

`config.example.json` at the repository root is a working starting point and is
also what ships as `config.json` in the release zip. It is validated against
`schemas/config.schema.json` on load, and errors are reported by JSON path with
the last valid document kept. Keys that matter today: `audio.outputDevice`,
`modelRegistry.directories` / `watchForChanges` / `idleUnloadSeconds`,
`profiles`, and `languageDefaults`. `server.*` is reserved for the local API
server and has no effect yet.

## Package a release

The `package` target assembles the self-contained, zip-distributable release
described in `docs/design/architecture.md`'s "Distribution" section: `tts-host`,
the Kokoro runner and its ONNX Runtime library, the espeak-ng data, `schemas/`,
`config.json`, a `portable.marker`, and the bundled Kokoro model if
`models/kokoro-en-v1/` exists locally. Build first — CPack's `package` target
does not rebuild for you:

```sh
cmake --build build --parallel
cmake --build build --target package                 # Linux/macOS
cmake --build build --config Debug --target package  # Windows
```

The zip lands in `build/dist/`. There is no installer yet; unzip and run.

## Other targets

- **Default (`all`)** — builds `tts-host`, every runner, and the test binaries.
  This is what `cmake --build build ...` does with no `--target`.
- **`list-models`** — rebuilds only `tts-host` and runs it against the registry
  test fixture, for a fast manual edit/build/run loop without a full CTest pass:

  ```sh
  cmake --build build --target list-models                    # Linux/macOS
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
never write into the same files — the same reason `build/` can't be shared
between them.

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
- The compiled-in download catalogue (listing only)
- Zip release packaging via CPack

Not built yet:

- Downloading from the catalogue (listing works; the fetch does not)
- The local HTTP API server, and with it the browser extension
- Global hotkeys and Windows selection reading
- Interrupt and queue semantics for playback
- Playback and the desktop UI on Linux and macOS — both are Windows-only, and
  other platforms fail with a clear not-implemented error
- An installer, and start-at-login
- Russian and Armenian models; the English model bake-off (Kokoro vs. Qwen3-TTS
  and alternatives) that settles the fast/quality defaults

## Documentation

- `roadmap.md` — the implementation sequence, slice by slice
- `docs/requirements/product.md` — finished behavior, constraints, acceptance
  criteria, non-goals
- `docs/design/architecture.md` — architecture, protocols, tradeoffs
- `docs/adr/` — durable decisions and their rationale

## Licence

See `LICENSE`. Model weights carry their own licences, reported by
`--list-models`, `--list-catalogue`, and the settings window.
