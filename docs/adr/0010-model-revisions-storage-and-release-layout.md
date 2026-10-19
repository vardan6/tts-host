# ADR 0010 — Model revisions, two-tier storage, and per-runner release layout

- Status: accepted
- Date: 2026-10-17

## Decision

**Revision.** A package's content revision is a fingerprint over its files:
SHA-256 of the sorted list of `relative path` + file SHA-256, including the
host-written files (`model.json` excluded). The host computes it from the
compiled catalogue entry and writes it into `model.json` when installing.
Bundled packages are built from the same entry and so carry the same value.
Locally imported packages have no revision: Settings shows them as local and
never offers them an update.

**Storage.** Windows installs per user into `%LOCALAPPDATA%\Programs\TTS Host`
with no elevation. In installed mode the bundled models stay in the
application's `models/`, which the Host never writes. Application upgrades
replace them. Catalogue downloads, updates, and imports go to the per-user
`%LOCALAPPDATA%\TTS Host\models` (the platform equivalent elsewhere). Portable
mode has only the `models/` beside the executable, and updates replace packages
there.

**Precedence.** When one ID exists in both tiers, the copy whose revision
matches the compiled catalogue wins. If neither matches, the per-user copy wins.
The losing copy is reported as superseded and Settings offers to remove it. The
same ID twice within one tier is an error; the copy found later is reported
unsupported.

**Previous version.** A committed update moves the old package folder, not a
copy, to `models/.previous/<id>@<revision>/` in the writable tier. One previous
version per ID is kept, and Settings offers Revert and Delete. A bundled package
that an update supersedes stays where it is as its previous version: reverting
removes the per-user copy. There is no separate downloads archive.

**Release layout.** Each runner lives in its own folder with its private
libraries beside it: `runners/kokoro-onnx/` (runner and ONNX Runtime) and
`runners/qwen3-gguf/` (runner, llama.cpp, ggml, CUDA libraries). Windows finds
the libraries through the executable-directory DLL search, and Linux through
rpath `$ORIGIN`. This replaces the compiled-in `TTS_QWEN_RUNTIME_DIR` and the
process-wide `AddDllDirectory` setup
([Qwen review finding 7](../reviews/code-review-2026-10-16-qwen-evaluation.md#triage--2026-10-16)).

**Acceleration.** The release ships the CUDA llama.cpp build only. Without a
usable NVIDIA GPU, Qwen is listed as unavailable with an actionable reason and
Kokoro still works. The NVIDIA redistributable notice ships in `licenses/`.

## Why

The user accepted these options on 2026-10-17. A fingerprint cannot drift from
the files and needs no manual version bump. Keeping bundled models read-only
means an application upgrade never overwrites a newer update. Precedence by
catalogue match keeps a stale earlier update from shadowing a newer bundled
copy. Moving rather than copying the old folder gives rollback without
doubling multi-gigabyte packages. Libraries beside each runner keep ONNX
Runtime and the CUDA/ggml stack apart without runtime search-path code.

## Rejected

- **Hand-set version labels or counters in the catalogue:** someone can forget
  to change them.
- **Matching local imports to catalogue entries by checksum:** extra complexity
  for an edge case.
- **Per-machine `Program Files` install:** needs elevation and makes the
  application folder unwritable, which helps nothing here.
- **Copying bundled models into per-user storage on first run:** stores Qwen
  twice. **Installing bundled models straight into per-user storage:** an
  application upgrade would overwrite a newer update with the older bundled
  copy.
- **Per-user copy always wins:** a stale earlier update would shadow a newer
  bundled package.
- **Delete the old version on update / keep a downloads archive:** the user
  wants rollback, and an archive doubles disk use.
- **Flat layout (current CPack output)** and **`runners/` plus shared
  `runtimes/<name>/`** (the
  [release packaging audit](../reviews/2026-08-31-release-packaging-audit.md#recommended-release-architecture)
  recommendation): the first mixes every runtime library in one folder, and the
  second still needs library search-path setup at runtime.
- **CPU or Vulkan llama.cpp fallback:** a second runner build and a larger
  download, for an RTF far above 1.0.

## Consequences

- The host's runner resolution changes from flat `tts-host-<engine>-runner`
  beside the executable to `runners/<engine>/`. Folders take the engine ID
  (`kokoro-onnx`, not `kokoro`); amended 2026-10-19 with the user. The CPack layout and the
  development build output must follow it.
- In installed mode the registry scans two directories with defined precedence,
  and catalogue installs target the per-user tier.
- One Windows package carries the CUDA libraries. It is not split into CPU and
  NVIDIA variants unless its size proves unreasonable, as the
  [distribution requirements](../requirements/product.md#distribution-and-usability)
  allow.
