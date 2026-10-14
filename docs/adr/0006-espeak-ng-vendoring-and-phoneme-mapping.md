# ADR 0006 — espeak-ng vendoring mechanics and phoneme mapping source

- Status: accepted
- Date: 2026-08-30
- Amended: 2026-09-06 — point 1's extraction mechanism reversed from 7-Zip to
  `msiexec /a` after the 7-Zip layout was found to crash espeak-ng. The
  original text rested on a false premise; see "Why" and "Rejected".

## Decision

Building on the isolation decision in `docs/adr/0005-espeak-ng-licensing.md`:

1. **Windows (first-class target):** vendor espeak-ng by fetching the official
   `espeak-ng.msi` release asset via CMake `FetchContent` and unpacking it at
   build time with an administrative install:
   `msiexec /a <msi> /qn TARGETDIR=<dir>`. This applies the MSI's `Directory`
   and `File` tables, producing `<dir>/eSpeak NG/` containing `espeak-ng.exe`,
   `libespeak-ng.dll`, and a correctly nested `espeak-ng-data/`
   (`lang/gmw/en`, `voices/!v/...`, `phondata`, `phontab`, `*_dict`). That is
   already the layout the runner needs, so the CMake step is a plain copy of
   the two binaries next to `tts-host-kokoro-onnx-runner.exe` (the same
   post-build placement used for `onnxruntime.dll`) plus a directory copy of
   `espeak-ng-data/` beside them — no reshuffling and no renaming.

   Do **not** extract this MSI with 7-Zip. 7z reads the embedded CAB, but the
   CAB stores all 441 payload files under flat names; the directory structure
   lives only in the MSI tables that 7z does not apply. The result is a heap of
   files with no `lang/` or `voices/` tree, so voice lookup fails and
   espeak-ng terminates with an access violation (`0xC0000005`). 7z also
   mangles `libespeak-ng.dll` to `libespeak_ng.dll` and `espeak-ng.exe` to
   `espeak_ng.exe`, and the hyphenated DLL name is what the executable's
   import table requires. Those underscore spellings are extraction artifacts,
   not the names espeak-ng ships.

   No `--path` argument is passed: on Windows espeak-ng resolves
   `espeak-ng-data/` relative to its own executable, which the post-build
   placement guarantees. (An earlier revision of this ADR specified
   `--path=<dir>`; the implementation never did, and does not need to.)
2. **Linux (dev/CI platform, not yet a release target):** do not vendor.
   Require a system-installed `espeak-ng` on `PATH`, added to
   `scripts/setup-dev-env.sh`. Revisit vendoring for Linux under the
   "Cross-platform releases and installer" roadmap item, once Linux ships a
   self-contained artifact.
3. **Invocation:** one-shot subprocess per synthesis request —
   `espeak-ng -q --ipa=3 -v <voice> -- "<text>"` — stdout captured as the IPA
   phoneme string. No persistent daemon or custom IPC framing; this matches
   the request-scoped, low-frequency nature of phonemization and needs no new
   protocol.
4. **Phoneme mapping:** Kokoro's ONNX model was trained on a custom, sparse
   ~178-symbol phoneme vocabulary, not raw IPA — it silently drops any symbol
   outside that vocabulary rather than erroring. The espeak-ng-to-Kokoro
   translation table is ported (as static C++ data, not vendored code) from
   `hexgrad/misaki`'s `espeak.py` fallback module. Misaki is Apache-2.0, so
   porting its mapping table carries no licensing obligation beyond
   attribution, and reusing a maintained table avoids re-deriving Kokoro's
   undocumented phoneme quirks from scratch.

## Why

- `msiexec /a` is the only unpack method that reproduces espeak-ng's real
  on-disk layout, and that layout is load-bearing: espeak-ng cannot find a
  voice without `espeak-ng-data/lang/<family>/<code>`. An administrative
  install writes to `TARGETDIR` only — it is an extraction, not a real
  install, so it still leaves no registry or uninstall footprint.
- Requiring Windows to configure costs nothing in practice: the espeak-ng
  block is already gated on `WIN32` and only produces artifacts for Windows
  builds, so a Linux/WSL configure never reached it anyway. `msiexec` ships
  with Windows, so this also removes 7-Zip as a build prerequisite.
- espeak-ng's GitHub releases publish only a Windows MSI and an Android APK —
  no Linux binary archive comparable to onnxruntime's per-platform zip/tgz —
  so a single symmetric vendoring mechanism (as used for ONNX Runtime) is not
  available. The product requirements already stage Windows first and treat
  Linux as designed-not-built, so an asymmetric approach follows existing
  product sequencing rather than inventing a new exception.
- Misaki's translation table exists specifically because Kokoro's phoneme
  vocabulary is undocumented upstream and mismatches with plain espeak-ng IPA
  cause silently dropped phonemes. Writing an independent mapping from
  scratch would be re-deriving the same reverse-engineering work Misaki has
  already published under a compatible licence.

## Rejected

- **Extract the MSI with 7-Zip (`7z x espeak-ng.msi`).** This was the original
  decision here, adopted because 7z reads the embedded CAB from any host OS
  with no Windows Installer dependency. It is wrong: 7z cannot reconstruct the
  data directory tree or the real file names, and the resulting build crashes
  at the first phonemize call. The portability it bought was also illusory,
  since the block only ever runs for Windows builds. Reverted 2026-09-06;
  point 1 above records the failure mode in full so this is not re-adopted.
- **Build espeak-ng from source for both platforms.** Would give a uniform
  mechanism, but adds a much heavier build dependency (autotools/meson,
  additional toolchain requirements) for a component that already has an
  official Windows binary; only Linux lacks one, and Linux isn't a release
  target yet.
- **Vendor `misaki` itself (Python) for the phoneme mapping.** Rejected —
  the product requirements forbid end-user Python; only the mapping data is
  needed, not the package.
- **A persistent espeak-ng subprocess with custom framing.** Rejected as
  premature: synthesis requests are not high-frequency enough to justify
  daemon lifecycle management and a new IPC contract when a one-shot
  subprocess per request is simple and already the pattern for the runner
  itself being a separate process.

## Revisit when

Linux becomes a release target (bundle espeak-ng there too), or espeak-ng
publishes an official Linux binary archive, or Kokoro's phoneme vocabulary
becomes documented upstream making the ported Misaki table unnecessary to
maintain independently.
