# ADR 0007 — Native platform APIs instead of a cross-platform UI toolkit

- Status: accepted
- Date: 2026-08-30

## Decision

The tray icon and settings window are implemented against each operating
system's native UI APIs directly — Win32/Shell_NotifyIcon on Windows, AppKit
on macOS, GTK on Linux — behind the small per-platform interfaces the host
already uses for audio output, autostart, and data directories (see
`docs/design/architecture.md#host-stack`). No cross-platform UI toolkit (Qt,
wxWidgets, or otherwise) is adopted.

Platforms are implemented one at a time: Windows first, then Linux, then
macOS, matching the platform-strategy sequencing already in
`docs/design/architecture.md#platform-strategy`.

## Why

- **Package footprint.** A toolkit either ships its own widget-rendering
  engine (Qt: tens of MB across dozens of files on every platform) or wraps
  the native toolkit at the cost of one more vendored dependency
  (wxWidgets). Calling the OS API directly ships nothing extra — the same
  principle already applied to audio output (`PlaybackSink` calls WASAPI
  directly, no audio toolkit).
- **Licensing.** Qt 6's LGPL terms require dynamic linking and relinking
  instructions the project would need to maintain indefinitely. Native APIs
  carry no such obligation.
- **Consistency with existing platform-specific boundaries.** Autostart,
  data directories, and audio output are already implemented as small
  per-platform interfaces rather than through a shared toolkit; the tray and
  settings window follow the same pattern instead of introducing a second
  one.

## Rejected

- **Qt 6.** Best-in-class tray (`QSystemTrayIcon`, including Wayland) and
  widget consistency, but the largest footprint of the candidates and LGPL
  compliance overhead for the life of the project.
- **wxWidgets.** Wraps native widgets (lighter than Qt, permissive license),
  but still one more vendored dependency on Windows, and its per-platform
  consistency is weaker than Qt's (GTK tray behavior under Wayland is a
  known pain point; macOS support has historically lagged).

## Cost

Three separate UI implementations instead of one shared codebase — more
total code than a single toolkit would require, accepted in exchange for
zero added package dependencies and no shared-toolkit licensing surface.
Each platform's implementation ships only when that platform's slice is
built; Linux and macOS tray/settings support does not block the Windows
slice.
