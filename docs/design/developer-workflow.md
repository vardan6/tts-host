# Developer build workflow

## Incremental Windows build and run

CMake owns dependency tracking. `compile-win.ps1` configures the Windows tree
when it is missing, then invokes `cmake --build build --config Debug --parallel`,
`ctest --test-dir build -C Debug --output-on-failure`, and finally launches
`build/Debug/tts-host.exe` with the gitignored `config.development.json` at the
repo root. Create that document from the example when absent; otherwise retain
the local document. Before launch, set `startup.launchOnLogin` to `false` and
create the first-run startup-choice marker in the launch's data directory, so
the development executable is never registered to run at login and never asks
(see [login startup](architecture.md#login-startup)). Keeping the
config beside the example preserves relative schema and model paths.
It should not compare source timestamps itself or terminate an existing host.
`cmake --build` delegates to
the selected generator and rebuilds changed sources and their dependents
([CMake build manual, 3.28](https://cmake.org/cmake/help/v3.28/manual/cmake.1.html#build-a-project)).

Windows uses the `build/` binary tree and the Visual Studio 2022 generator.
WSL/Linux uses `build-linux/`, so the two environments never reuse CMake's
generator or compiler state. The Windows FetchContent base is
`cache/fetchcontent/windows-vs2022/`, separate from either binary tree.

## Clean and fresh semantics

Keep three operations distinct:

- **Incremental:** reuse the configured binary tree; CMake rebuilds changed
  inputs and required dependents.
- **Clean:** invoke the generated `clean` target. This removes build outputs,
  but retains the configured generator, CMake cache, and fetched dependencies.
- **Fresh:** `fresh-win.ps1` removes `build/`, configures it again, then builds
  all targets and runs CTest. Keep `build-linux/` and the dependency cache unless
  the user explicitly passes `-ResetDependencyCache`.

The Windows generator is Visual Studio, so a handwritten `Makefile` or direct
`make clean` command is the wrong abstraction for that build tree.
`cmake --build build --config Debug --target clean` selects the generated clean
operation for the active generator; `--clean-first` is available when cleaning
and rebuilding in one command. The separate WSL/Linux tree may use Makefiles or
Ninja without changing the Windows workflow.
