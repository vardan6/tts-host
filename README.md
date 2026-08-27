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

There is no installer or CMake install target yet. Run the built executables
directly from `build/` (or from `build/Debug/` on Windows).

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
