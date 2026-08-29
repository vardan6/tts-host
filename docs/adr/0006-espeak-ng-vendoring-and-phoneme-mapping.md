# ADR 0006 — espeak-ng vendoring mechanics and phoneme mapping source

- Status: accepted
- Date: 2026-08-30

## Decision

Building on the isolation decision in `docs/adr/0005-espeak-ng-licensing.md`:

1. **Windows (first-class target):** vendor espeak-ng by fetching the official
   `espeak-ng.msi` release asset via CMake `FetchContent` and extracting it
   with 7-Zip (`7z x espeak-ng.msi`) at build time — verified working: 7z reads
   the MSI's embedded CAB directly and needs no Windows Installer service, no
   `msiexec`, and no install/registry side effects, so the same step works
   whether the build runs on Windows or WSL/Linux. The extraction is flat (443
   files, no subdirectories) and contains `espeak_ng.exe` (note the
   underscore, not a hyphen), `libespeak_ng.dll`, and per-language data files
   (`phondata`, `phontab`, `phonindex`, `en_dict`, `mb_us1`, etc.) with no
   `espeak-ng-data/` wrapper folder already present. The CMake step must
   therefore synthesize that layout itself: copy `espeak_ng.exe` and
   `libespeak_ng.dll` next to `tts-host-kokoro-onnx-runner.exe` (the same
   post-build placement already used for `onnxruntime.dll`), and copy every
   other extracted file into an `espeak-ng-data/` subdirectory beside them.
   Invoke with `--path=<that directory>` — espeak-ng's documented lookup
   order is `ESPEAK_DATA_PATH` env var, then `$HOME/espeak-ng-data`, then
   `/usr/share/espeak-ng-data`, then `--path=<parent-of-espeak-ng-data>` —
   `--path` is used because it needs no environment mutation and has no
   ambiguity about whether the value is the data directory itself or its
   parent.
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

- 7-Zip extracts MSI contents without invoking Windows Installer, so the
  vendoring step has no install/uninstall registry footprint and is not
  restricted to running on Windows — useful since this project's CI and much
  of its development happen on Linux/WSL.
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

- **Extract the MSI via `msiexec /a` (administrative install).** Requires
  running on Windows and invokes the actual Windows Installer service for what
  is just a file-extraction step; 7-Zip reads the embedded CAB directly with
  no such dependency and works from any host OS running the CMake configure
  step.
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
