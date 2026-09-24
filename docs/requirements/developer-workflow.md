# Developer build workflow requirements

## Windows edit, verify, and run loop

The Windows edit loop must quickly recompile, retest, and relaunch the desktop
app. Repeated builds in the same configured Windows build tree must reuse
CMake's generated dependency graph, rebuilding changed sources and their
dependents while skipping unchanged targets. PowerShell must not implement a
second timestamp or dependency scanner.

`compile-win.ps1` must configure the Windows tree if it does not exist, then run
the following actions in order: build all targets incrementally, run CTest, and
launch `tts-host` with `config.example.json`. It must stop after a failed build
or test and must not terminate an already-running host.

Windows uses `build/`; WSL/Linux uses `build-linux/`. Their generator, compiler,
and CMake cache state must stay isolated.

A generated clean operation must remove build outputs while retaining the
configured Windows tree and downloaded dependencies. `fresh-win.ps1` must
remove only `build/`, configure it again, build all targets, and run CTest. The
FetchContent cache must remain intact unless the user passes
`-ResetDependencyCache` to that fresh operation.

## Acceptance

- Running `compile-win.ps1` twice without source changes does not invoke
  compilation on the second run, still runs CTest, and launches the desktop app
  after successful tests.
- Changing a source or header and rebuilding compiles only the affected target
  dependency graph; changing CMake configuration causes CMake to regenerate.
- The desktop launch happens only after the build and tests pass, and leaves an
  already-running process for the user to close normally.
- The clean command makes the next build recompile generated targets without
  deleting CMake's configured tree or the dependency cache.
- The fresh command reconstructs the Windows tree and verifies the full build
  and CTest suite without touching `build-linux/` or the FetchContent cache,
  unless `-ResetDependencyCache` is passed.
