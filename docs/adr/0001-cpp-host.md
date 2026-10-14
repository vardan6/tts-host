# ADR 0001 — C++ host in a single process

- Status: accepted
- Date: 2026-08-27

## Decision

The long-lived host is written in C++20 with CMake and ships as one executable,
`tts-host`. That executable owns configuration, discovery, the local API,
scheduling, playback, process supervision, the tray, and the settings window.
`--headless` starts it without any UI and is supported in the first release.

The tray and settings window are the application's own front end and run
in-process. External clients — the CLI, the browser extension, and any later
playback widget — talk to the host over the local HTTP API instead.

## Why

The host never runs model code; the runner boundary (ADR 0002) keeps inference
out of it. What remains is HTTP, JSON, process supervision, audio output, and OS
integration, which C++ does adequately and distributes natively on Windows with
no external runtime.

One process rather than a service plus a UI client:

- One lifecycle. One thing to autostart, one to quit. Split processes make
  "tray alive, service dead" reachable, and every such state is a support
  problem.
- One configuration writer. `config.json` reloads live (see
  `docs/design/architecture.md`); a separate settings process would put two
  writers on one watched file.
- The tray reflects loaded models, device use, and playback directly rather than
  polling its own API.

## Rejected

- **Rust host.** The better language for this on a blank slate. Rejected because
  the Windows tray, audio, and UI-toolkit story is weaker, and the existing
  design is already built around a C++/Qt-class stack.
- **Python host.** Rejected outright: `docs/requirements/product.md` forbids
  requiring a Python interpreter on an end-user machine, and startup cost and
  native distribution both argue against it.
- **C#/.NET.** Strong on Windows tray, audio, and accessibility, weaker on the
  Linux and macOS targets the product keeps in scope.
- **A true Windows Service.** Session 0 isolation means a registered service
  cannot reach the user's audio device or show a tray icon. The product plays
  audio, so "always running" is implemented as a per-user process started at
  login, which is what comparable local-inference applications do.
- **Separate `tts-hostd` and tray binaries.** Cleaner on paper and it would
  dogfood the API, but it buys a second process, a second lifecycle, and the
  two-writer configuration problem for no user-visible gain.

## Cost

A crash in UI code takes the service with it. Acceptable because the
crash-prone part — inference — is already isolated in runner processes.

The UI toolkit is deliberately **not** decided here. The first slices are
headless; the tray and settings window are built against native platform APIs
rather than a shared toolkit — see
[ADR 0007](0007-native-ui-per-platform.md).
