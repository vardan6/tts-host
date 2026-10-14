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

## The tray thread owns the editable config; the speech worker reads snapshots

In tray mode, `main` hands `run_tray_icon` a mutable `ConfigDocument` that
Settings edits in place. Only the tray thread may touch it. The speech worker
(`TraySpeechScheduler` in `main.cpp`) never reads it: each request takes
`ConfigSnapshot::current()` once (`config_snapshot.hpp`) and uses that immutable
document for the whole utterance, so a Settings edit can neither race the
worker nor mix old and new values within one request.

Publication happens when Settings closes, or when the tray's
`ConfigFileWatcher` (`config_watch.hpp`) applies a settled external edit; both go
through the tray's `publish_config` callback. The watcher pauses while Settings
is open, because Settings edits the same document; its own saves compare equal
afterward and are not treated as changes. That callback is also where `apply_playback_settings` pushes the default
speech speed and seek interval into the running `PlaybackController`. A new
live setting that the worker or controller needs must be read from the
published document there, not from the tray's copy. Changes made while
Settings is still open reach speech only after it closes.

## Import repoints a copied manifest's `$schema`

`import_model_package` (`model_import.cpp`) copies a package directory into a
registry directory, where the manifest sits at a different depth than it did in
its source tree — so a relative `$schema` would no longer resolve. Import
rewrites it to the installation's own `schemas/model.schema.json`. The copied
`model.json` is therefore not byte-identical to the source.

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
disk as complete and skips fetching it, but still verifies its checksum immediately
after checking that file's size, before proceeding to the next file. A file
smaller than pinned resumes via HTTP Range from that byte offset; a file that
fails checksum verification after a full download is deleted outright (not
resumed), since a corrupt byte's position is unknown and the safe recovery is
a full refetch.

## The utterance stream holds the COM MTA across playback threads

`WasapiPlaybackStream` (`playback_sink.cpp`) is opened on one short-lived
playback thread and fed and drained from later ones, each of which joins the
MTA and leaves it on exit. The stream calls `CoIncrementMTAUsage` before its
first activation and releases it in its destructor, so its client survives
between those threads. Play and drain calls are never concurrent; the pipeline
joins one before launching the next, and the stream itself takes no lock.
Pause between sentences, while no playback call is active, lets at most the
queued buffer (`audio.bufferMilliseconds`) finish before the next
call stops the client.
