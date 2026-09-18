# Implementation notes

Durable invariants, contracts, and gotchas that are not obvious from the code
and have no better home in `docs/requirements/`, `docs/design/`, or
`docs/adr/`. One entry per gotcha; keep it short and cite the code.

## Win32 controls need an explicit font

Controls created with `CreateWindowExW` inherit no font. Windows falls back to
the legacy bitmap `SYSTEM_FONT` — bold, oversized, and wider than Segoe UI —
which silently overflows any fixed pixel layout: captions clip mid-word and
multi-word labels lose their second line. Nothing errors; it just looks wrong.

Every control added to the settings window must receive the shell UI font.
`settings_window.cpp` does this in one pass with `EnumChildWindows` +
`apply_shell_ui_font` after all controls exist, using `shell_ui_font()`
(`SPI_GETNONCLIENTMETRICS` → `lfMessageFont`). Adding a control needs no extra
work, but adding one *after* that call, or in a new window, does.

The layout is fixed-pixel and not DPI-aware. Coordinates assume the shell font
at 96 DPI; the label column is sized to the longest caption. Changing a caption
can therefore clip it even with the correct font.

## Import repoints a copied manifest's `$schema`

`import_model_package` (`model_import.cpp`) copies a package directory into a
registry directory, where the manifest sits at a different depth than it did in
its source tree — so a relative `$schema` would no longer resolve. Import
rewrites it to the installation's own `schemas/model.schema.json`. The copied
`model.json` is therefore not byte-identical to the source.

## `unload_if_idle` measures time since load, not since last use

`ModelSessionManager::unload_if_idle` (`model_session.cpp`) compares
`modelRegistry.idleUnloadSeconds` against the time the model was **loaded**,
not the time it was last used. Nothing resets the clock on synthesis, because
no long-lived consumer of the resident session exists yet — the settings window
is currently the only process that loads a model, and CLI synthesis spawns its
own runner. A model in active use will still unload on schedule.

Revisit when the local API server or a synthesizing tray session becomes a
resident consumer; that is the point at which "idle" must start meaning "idle".

## The Kokoro catalogue entry duplicates the weights fetcher

`model_catalogue.cpp`'s Kokoro-82M entry carries the same URLs and SHA-256
checksums as `tools/fetch_kokoro_weights.py`. They are independent copies and
will drift. Changing the pinned artifacts in one place means changing both.

Kokoro also appears in the download catalogue despite being bundled in the
release zip; the catalogue entry exists for restoring a deleted package or
installing into a second registry directory, not for first-run speech.

## Catalogue download's SHA-256 is hand-rolled, and resume trusts size alone

`sha256_hex_of_file` (`catalogue_download.cpp`) is a self-contained SHA-256
rather than a vendored crypto library, so checksum verification stays testable
on Linux/WSL even though the fetch itself (`fetch_url_to_file`, WinHTTP) is
Windows-only. Verified against the FIPS test vector for `"abc"` in
`catalogue_download_tests.cpp`.

`download_catalogue_entry` treats a file already at its pinned `size_bytes` on
disk as complete and skips fetching it — it does not re-verify the checksum
until after every file in the entry has reached its expected size. A file
smaller than pinned resumes via HTTP Range from that byte offset; a file that
fails checksum verification after a full download is deleted outright (not
resumed), since a corrupt byte's position is unknown and the safe recovery is
a full refetch.
