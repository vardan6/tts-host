# Release packaging audit — 2026-08-31

## Our target

Ship TTS Host as a complete native desktop application on Windows, Linux, and
macOS. Windows remains the first platform polished and released, but its
packaging choices must not block equivalent native releases for Linux and
macOS.

## What we want to achieve

- A user can run a portable application without Python, a Python environment,
  a package manager, or separately installed application dependencies.
- The same complete application can also be delivered through each platform's
  conventional installer experience.
- Each distribution includes every non-OS executable, library, data file,
  runner, model required for first-launch speech, schema, and applicable
  licence/notice material.
- Portable mode keeps mutable state beside the application; installed mode
  keeps mutable state in the appropriate per-user operating-system location.

## What we checked

This is a logical audit of the repository's build, staging, runtime lookup, and
packaging configuration. It did **not** run a build, create a package, or alter
the implementation. Its purpose is to establish what a fresh clone can
produce today and what must change before calling an artifact a complete,
portable, multi-platform release.

Specifically, the audit checked fresh-clone prerequisites, CMake downloads,
build-tree isolation, CPack staging, installed file inventory, runtime lookup,
model inclusion, portable-data resolution, and the Windows/Linux/macOS
branches.

A second, code-level verification pass on the same date re-checked every row of
the table below against the sources rather than against the audit's own
summary. Every row held. That pass also found five further defects and
conflicts, recorded under "Additional findings from verification".

## What we found

The current CMake/CPack work is a useful Windows-first foundation, but it is
not yet a reliable fresh-clone, self-contained, multi-platform release
pipeline. Windows has much of the necessary staging machinery; Linux is still
development-only; macOS is not currently buildable with the selected runtime
archive.

| Earlier claim | Result | Evidence and consequence |
|---|---|---|
| Build dependencies download into `build/_deps`. | Mostly true. | `FetchContent` downloads the JSON libraries and ONNX Runtime into the CMake build tree. On Windows it also downloads and extracts eSpeak-NG there. A fresh build needs CMake 3.28+, a C++ compiler, internet access, and 7-Zip on Windows. |
| Windows runtime dependencies are bundled. | Partly true. | The install rules stage ONNX Runtime, `espeak_ng.exe`, `libespeak_ng.dll`, and the eSpeak-NG data directory. They do not stage the MSVC runtime DLLs or third-party licence material. |
| The `package` target creates a ZIP in `build/dist`. | True. | CPack is configured with its `ZIP` generator and `CPACK_PACKAGE_DIRECTORY` is `${CMAKE_BINARY_DIR}/dist`. The normal release workflow must build first, then invoke `package`. |
| The ZIP contains a working bundled model. | Conditional, with a release-blocking flaw. | `models/kokoro-en-v1/` is installed only when it exists at CMake *configure* time. `tools/fetch_kokoro_weights.py` writes that model into the source checkout, rather than the build directory, and does not verify downloaded hashes. Fetching after configuration requires reconfiguration before packaging. A release can therefore silently omit the first-launch model. |
| The ZIP can synthesize by default. | No. | The packaged configuration names the Kokoro profile, but current runner selection only uses `--model`; it does not apply profiles/defaults. Without `--model kokoro-en-v1`, synthesis selects `tts-host-stub-runner`, which is not installed in the package. |
| Portable mode works with the current ZIP layout. | Yes, for the implemented flat layout. | `portable.marker` makes config/data resolve beside the executable. `config.json` resolves `./models`, and the configuration and model schema paths resolve correctly from the packaged paths. |
| Linux and macOS are covered by the package path. | No. | Linux currently resolves eSpeak-NG from the system `PATH`, so its archive is not self-contained. macOS enters the non-Windows ONNX Runtime branch, which downloads the Linux x64 archive and cannot yield a native macOS artifact. |

## Additional release gaps

- The package does not install the project `LICENSE`, third-party notices,
  model licence material, or eSpeak-NG GPL licence/source-offer material.
  Running eSpeak-NG out of process avoids linking it into the Apache-licensed
  host; it does not remove obligations when distributing the eSpeak executable
  and its data.
- No release pipeline extracts an artifact on a clean target machine and proves
  that the host, runner, model, and their private runtime files work together.
- No installer packaging is implemented. The current artifact is only a
  portable ZIP.
- The architecture document depicts `runtimes/` and `runners/` directories,
  while current code and install rules use a flat directory. The layout must be
  decided deliberately before it expands further.
- Linux and macOS need explicit platform/architecture dependency policies;
  treating every non-Windows platform as Linux x64 is not a viable release
  policy.

## Additional findings from verification

Five items the first pass missed, each confirmed in the sources.

### 1. `portable.marker` is an install rule, contradicting the release-root plan

`CMakeLists.txt:264` touches `portable.marker` during `install`, so *every*
consumer of the release root receives it, including an installer. The
"Portable and installed delivery" section below assumes installed mode simply
omits the marker; with one canonical release root that is impossible as
written. The marker must become a portable-archive packaging step, or the
installer must remove it. This belongs to the layout decision in sequencing
step 1, not after it.

### 2. macOS is broken in three ways, not one

Beyond the Linux archive selection, `BUILD_RPATH "$ORIGIN"`
(`CMakeLists.txt:216-221`, gated on `NOT WIN32`) and `IMPORTED_SONAME
libonnxruntime.so.1` (`CMakeLists.txt:78`) are ELF-only concepts that do
nothing useful on Mach-O. The entire eSpeak-NG block is `if(WIN32)`, so macOS
has no eSpeak path at all — not even Linux's `PATH` fallback. The macOS branch
is absent rather than misconfigured, and sequencing step 7 should be scoped
accordingly.

### 3. The release-assets step is currently Python

`tools/fetch_kokoro_weights.py` is the only producer of the bundled model.
Section 1 below asks for a hash-verified preparation step reachable from a
single fresh-clone release preset, which requires either porting the fetch to
CMake `file(DOWNLOAD ... EXPECTED_HASH)` or accepting Python as a *release
builder's* prerequisite. The script's docstring correctly says the shipped
application never needs Python; that says nothing about whoever cuts the
release. Decide this before costing sequencing step 3.

### 4. `FetchContent_Populate(onnxruntime)` uses the deprecated single-argument form

`CMakeLists.txt:58` and the eSpeak-NG equivalent rely on CMP0169's OLD
behavior, available only because `cmake_minimum_required` is 3.28. Raising the
minimum breaks configuration. No current impact; relevant to the fresh-clone
prerequisite claim.

### 5. Install rules read build outputs rather than declaring sources

`install(FILES $<TARGET_FILE_DIR:...>/onnxruntime.dll ...)` succeeds only
because a `POST_BUILD` copy already placed the file there. This is the same
category as the configure-time `EXISTS` gate on the model — staging that
depends on prior side effects instead of declared inputs. It fails loudly
rather than producing a bad archive, so it is not itself release-blocking, but
section 2's "consume the staging directory, do not reconstruct a file list"
should cover it.

## Triage

Packaging belongs to the later cross-platform release phase, so nothing here
blocks the model-manager phase in progress: there are no must-fix-now items.
`should_fix_before_phase_complete` below means *before the model-manager phase
closes*; release-phase work is `backlog`, which matches the sequence already
recorded in `activeContext.md`.

| # | Finding | Risk | Value | Effort | Priority | Reason |
|---|---------|------|-------|--------|----------|--------|
| 1 | `profiles`/`languageDefaults` are unread (`src/main.cpp:52-84`); default synthesis falls back to `tts-host-stub-runner`, which `install(TARGETS)` never ships | high | high | low | should_fix_before_phase_complete | Not a packaging item — profile/model switching is the current phase's headline. Shipping configuration keys that no code reads is a broken contract today, independent of any release |
| 2 | Model licence display is unimplemented, though `model.json` carries a `license` block | med | high | med | should_fix_before_phase_complete | Named in the current model-manager slice; distinct from bundling licence files into an artifact (row 6) |
| 3 | Model install gated on configure-time `EXISTS` (`CMakeLists.txt:265`) — a release can silently omit the first-launch model | high | high | med | backlog | Release-phase; no release is cut yet. Highest-value packaging item once that phase opens |
| 4 | MSVC runtime DLLs not staged app-locally; no `InstallRequiredSystemLibraries` | high | high | low | backlog | Release-phase. Cheap fix, but only meaningful once a clean-machine test exists to prove it (row 11) |
| 5 | `portable.marker` written by `install(CODE ...)` (`CMakeLists.txt:264`), so an installed artifact is always portable | med | high | low | backlog | Release-phase, but must be settled *inside* the layout decision, not after it. Additional finding 1 |
| 6 | No `LICENSE`, third-party notices, model licence, or eSpeak-NG GPL material in the artifact | high | high | low | backlog | Release-phase. A genuine distribution obligation, but it binds only on the first public artifact |
| 7 | macOS branch non-functional: Linux ONNX archive, ELF-only `$ORIGIN`/`SONAME` (`CMakeLists.txt:78,216-221`), no eSpeak path | high | med | high | backlog | Release-phase and an explicitly later slice. Larger than a wrong URL — recorded so it is not under-scoped. Additional finding 2 |
| 8 | Linux resolves `espeak-ng` from `PATH` (`src/espeak_phonemizer.cpp:208`); archive not self-contained | med | med | med | backlog | Release-phase; deliberate per ADR 0006 for the dev workflow, so this is a scope expansion rather than a defect |
| 9 | Flat install layout diverges from `runtimes/`/`runners/` in `docs/design/architecture.md` | med | high | med | backlog | Already an open question in `activeContext.md` and self-flagged in the architecture document; blocks nothing until the layout slice |
| 10 | `tools/fetch_kokoro_weights.py` writes into the source tree and verifies no hashes | med | med | low | backlog | Dev-only tooling by design. The current phase requires catalogue downloads *with checksums*, so the hashing pattern is built there first |
| 11 | No clean-machine extraction plus real-synthesis validation of an artifact | med | high | high | backlog | Release-phase, and CI is deliberately deferred by the roadmap. Revisit with that deferral, not ahead of it |
| 12 | Release-assets preparation is Python, conflicting with a single fresh-clone CMake release preset | med | med | med | backlog | A decision, not a defect. Additional finding 3 |
| 13 | `FetchContent_Populate` single-argument form deprecated (CMP0169) | low | low | low | backlog | Works only because `cmake_minimum_required` is 3.28; breaks on a minimum bump. Additional finding 4 |
| 14 | No installer packaging; the artifact is a portable ZIP only | low | med | high | invalid | Not a finding — it is the unstarted cross-platform release roadmap phase |
| 15 | `install(FILES $<TARGET_FILE_DIR:...>)` requires a prior build | low | low | low | invalid | Fails loudly rather than producing a bad archive; build-then-`package` is normal CPack usage. Additional finding 5 |

## How we plan to solve it

### Recommended release architecture

Create one canonical **release root**, then derive portable archives and
platform-native installers from that exact same tree.

```text
source checkout
  -> isolated build/<platform>-<architecture>-release/
    -> release-root/
       tts-host
       runners/
       runtimes/
       schemas/
       models/
       licenses/
       config.json
       portable.marker
  -> portable archive
  -> platform installer
```

This makes the portable artifact authoritative and prevents ZIP, installer,
and local-development layouts from drifting apart.

### 1. Keep generated/downloaded release inputs out of the source tree

Replace the present source-root model download with a release-assets step that
writes to the selected build directory, for example
`build/<preset>/release-input/models/`. Maintain an explicit manifest for
every bundled asset with:

- URL;
- exact SHA-256;
- destination path;
- licence identifier/text and required attribution;
- model/package identifier and version.

A release configuration must fail if a required first-launch model is absent or
hash validation fails. It must not conditionally omit the model based on an
`EXISTS` test. The release bootstrap should obtain and verify assets before
CMake configuration, or otherwise use an install/staging mechanism that is
evaluated after preparation rather than at configure time.

This keeps a clone clean, makes releases reproducible, and permits a single
fresh-clone release command/preset without relying on a developer remembering
an ordering rule.

### 2. Build and validate one release root

Use CMake install rules to populate a defined staging directory. Its layout
should follow the eventual contract rather than the current accidental flat
shape:

- host executable at the root (or inside the macOS app bundle);
- engine runners in `runners/`;
- private libraries and helper executables/data in `runtimes/`;
- schemas, default configuration, bundled models, and licences in explicit
  directories.

Update runner and eSpeak lookup to use those relative paths. Model/profile
selection must select the configured default model instead of the test-only
stub runner. The stub runner remains a test fixture, not a required release
file.

The archive generator and every installer must consume this staging directory,
not reconstruct a second file list.

### 3. Bundle all non-OS runtime dependencies per platform

**Windows**

- Bundle ONNX Runtime, eSpeak-NG, its DLL, and all of its required data.
- Bundle the MSVC runtime app-locally. CMake's required-runtime support and a
  native dependency scan should identify the required runtime DLLs.
- Add a clean-Windows smoke test that rejects unresolved non-system DLLs. OS
  DLLs such as `kernel32.dll` remain OS dependencies; application/runtime DLLs
  must be present in the release root.

**Linux**

- Bundle a pinned eSpeak-NG executable and its data rather than invoking the
  system `espeak-ng` from `PATH`.
- Bundle ONNX Runtime and configure relative RPATH/runtime lookup from each
  runner to `runtimes/`.
- State and test a supported glibc/system-library baseline. The kernel, libc,
  graphics stack, and audio server are OS dependencies and should not be
  bundled as application files.

**macOS**

- Select or build an actual macOS archive for each supported architecture
  (Apple Silicon is the product target); never use the Linux archive fallback.
- Bundle ONNX Runtime and eSpeak-NG inside the application/release root and
  use `@loader_path`-relative install names.
- Verify the final artifact with `otool -L`, then code-sign and notarize the
  macOS distribution.

### 4. Include licences and notices in every artifact

Install a `licenses/` directory containing the product Apache-2.0 licence,
third-party notices/licences for statically and dynamically included
dependencies, the bundled model's licence/attribution, and the required
eSpeak-NG GPL distribution material. Confirm the exact corresponding-source
or written-offer approach for the eSpeak-NG binary before release.

### 5. Prove artifacts, not only builds

For every target platform/architecture, CI should:

1. configure from a clean clone using the release preset;
2. prepare and hash-verify release assets;
3. build and stage the release root;
4. create the portable archive;
5. extract it into an empty directory/environment;
6. launch the host and perform a real bundled-model synthesis smoke test;
7. scan for unresolved non-system dependencies and assert the expected release
   inventory, including licences.

The installer test should install the same release root on a clean virtual
machine, launch it, and then verify uninstall behavior. User-generated data
must remain separate from application files unless the user explicitly asks to
remove it.

## Portable and installed delivery

Portable mode and installed mode should share binaries, runners, runtimes, and
models. Only mutable-data location and OS integration differ.

| Platform | Portable artifact | Installed artifact | Mutable data in installed mode |
|---|---|---|---|
| Windows | ZIP | Signed WiX/MSI installer | `%LOCALAPPDATA%/TTS Host` |
| macOS | Signed/notarized `.app` archive or DMG | Signed/notarized DMG; drag to Applications is the conventional installation flow | `~/Library/Application Support/TTS Host` |
| Linux | AppImage (and optionally a plain archive) | Native `.deb`/`.rpm` where supported | XDG config/data directories |

The Windows installer should add shortcuts, an uninstaller, and an opt-in
start-at-login choice. macOS and Linux should use their native integration
mechanisms. `portable.marker` continues to select side-by-side mutable data
for portable use; installed mode omits it and uses the existing platform data
directory logic.

## Suggested sequencing

1. Decide and implement the canonical `release-root` layout and relative
   runner/runtime lookup, including how `portable.marker` is applied to the
   portable archive but not the installed tree (additional finding 1).
2. Make profile/default-model selection work and remove the release dependency
   on the unshipped stub runner. This is model-manager-phase work, not release
   work, and need not wait for step 1.
3. Create the hash-verified build-tree release-assets preparation step and
   make a missing first-launch model a hard release failure. Settle whether
   that step is CMake-native or Python first (additional finding 3).
4. Complete the Windows portable artifact: MSVC app-local runtime, licences,
   clean-machine extraction/synthesis test.
5. Add the Windows installer from the same release root.
6. Add Linux self-contained runtime staging and portable artifact validation.
7. Build the macOS branch: native Apple-Silicon dependency handling, an eSpeak-NG
   path where none exists today, Mach-O install names replacing the ELF-only
   `$ORIGIN`/`SONAME` settings, app bundle, signing, notarization, and
   validation (additional finding 2).

This sequence preserves the current Windows-first focus while preventing a
Windows-only packaging shape from becoming a cross-platform constraint.
