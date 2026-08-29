# TTS Host

TTS Host is an in-progress local text-to-speech host. The currently runnable
work covers configuration validation, model-package discovery, and the runner
control-channel handshake and deterministic stub-runner audio. It does not yet
launch a runner from the host, return WAV data, or play audio.

## Prerequisites

- CMake 3.28 or newer
- A C++20 compiler (Visual Studio 2022 Build Tools on Windows, or GCC/Clang
  on Linux)
- Internet access for CMake to download its JSON dependencies on the first
  configure

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
so configuring works from any shell.

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
test:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022"
cmake --build build --config Debug --parallel
ctest --test-dir build -C Debug --output-on-failure
```

## Targets

Beyond the default build (which builds every executable and test above), a
few named CMake targets exist for convenience:

- **Default (`all`)** — the main compile command; builds `tts-host`, every
  runner, and the test binaries. This is what `cmake --build build ...` above
  already does with no `--target` given.
- **`list-models`** — rebuilds only `tts-host` (skipping the rest) and runs it
  against the registry test fixture, for a fast manual edit/build/run loop
  without a full CTest pass:

  ```sh
  cmake --build build --target list-models          # Linux/macOS
  cmake --build build --config Debug --target list-models   # Windows
  ```

- **`package`** — assembles the self-contained, zip-distributable release
  described in `docs/design/architecture.md`'s "Distribution" section
  (`tts-host`, the Kokoro runner and its ONNX Runtime library, `schemas/`, an
  example `config.json`, `portable.marker`, and the bundled Kokoro model if
  `models/kokoro-en-v1/` exists locally — see "Fetching real Kokoro-82M
  weights" below). Build normally first, then package; CPack's `package`
  target does not rebuild for you:

  ```sh
  cmake --build build --parallel
  cmake --build build --target package               # Linux/macOS
  cmake --build build --config Debug --target package # Windows
  ```

  The zip lands in `build/dist/`.

## Try the host

The example config is ready to use from the repository root.

Linux/macOS:

```sh
./build/tts-host --headless --config config.example.json
```

Windows:

```powershell
.\build\Debug\tts-host.exe --headless --config config.example.json
```

The command confirms the resolved config and schema paths. It intentionally
does not speak yet.

To exercise model-package discovery with the included test fixtures:

Linux/macOS:

```sh
./build/tts-host --headless --list-models --config tests/fixtures/registry/config.json
```

Windows:

```powershell
.\build\Debug\tts-host.exe --headless --list-models --config tests\fixtures\registry\config.json
```

The output lists two accepted example packages and several deliberately invalid
fixtures, demonstrating the validation errors the host reports.

## Dev-only Python tooling

A few developer scripts under `tools/` regenerate checked-in test fixtures
(for example the placeholder `.onnx` model used to test-drive ONNX Runtime
linking). This tooling is **never** a dependency of the product: `tts-host`,
its runners, and the released package/zip are native binaries and never
invoke Python. Regenerating fixtures is optional and only needed if you're
changing the fixture itself.

Linux/macOS/WSL:

```sh
./scripts/setup-dev-env.sh
.venv-linux/bin/python tools/generate_kokoro_runner_fixtures.py
```

Windows:

```powershell
.\scripts\setup-dev-env.ps1
.\.venv-windows\Scripts\python tools\generate_kokoro_runner_fixtures.py
```

Each platform gets its own venv directory (`.venv-linux/` or
`.venv-windows/`, both gitignored) so a WSL run and a native Windows run
never write into the same files — the same reason `build/` can't be shared
between them.

### Fetching real Kokoro-82M weights

`tools/fetch_kokoro_weights.py` downloads the real, full-precision Kokoro-82M
ONNX model and one voice embedding (Apache-2.0, from
`onnx-community/Kokoro-82M-v1.0-ONNX` on Hugging Face) into
`models/kokoro-en-v1/`, alongside a generated `model.json` manifest. This is
real product data (~330 MB), not a checked-in test fixture — `models/` is
gitignored, and CTest never depends on it. Run it once to try the Kokoro
runner against real weights:

```sh
.venv-linux/bin/python tools/fetch_kokoro_weights.py
./build/tts-host --headless --model kokoro-en-v1 --synthesize "hello" --out hello.wav --config config.example.json
```

The runner still feeds a fixed, hardcoded phoneme sequence regardless of the
requested text — arbitrary-text phonemization via espeak-ng is a separate,
not-yet-implemented slice (see `roadmap.md`).

## Runner control protocol

`tts-host-stub-runner` is a test fixture for the next audio pipeline stages. It
reads length-framed JSON-RPC control messages from standard input. It responds
to `initialize` and `synthesize`; synthesis writes a deterministic four-sample,
24 kHz mono `pcm_s16le` stream to the inherited audio endpoint. It is built and
tested automatically by the commands above.

## Current scope

Implemented:

- `--headless`, `--config`, `--data-dir`, and `--list-models` CLI parsing
- Config-schema validation with JSON-path errors
- Model manifest discovery and path-safety checks
- JSON-RPC `Content-Length` control framing
- Typed `initialize` handshake and stub-runner capabilities

Not implemented yet:

- Starting and supervising runner processes from the host
- Framed audio transport, WAV output, or any model inference
- Desktop UI, playback, browser extension, and installers
