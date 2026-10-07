# ADR 0008 — The operating system owns login-startup state

- Status: accepted
- Date: 2026-10-06

## Decision

Whether the Host starts at login is stored only in the operating system's
standard per-user startup registration, not in `config.json`. Settings reads
that registration each time it opens and writes only that registration.
Ordinary Host launches never create, repoint, or remove it.
`startup.launchOnLogin` is only the default offered by the first-run question
and the installer.

When the user has turned the entry off through the operating system's own
startup page, the Host does not turn it back on. It shows that state and opens
that page instead.

This is a deliberate exception to [ADR 0004](0004-json-canonical-config.md):
the operating system already has a user-visible store for this one choice.

## Why

The operating system exposes its own switch (Task Manager and Settings → Apps →
Startup on Windows, Autostart in KDE System Settings, Login Items on macOS). A
second copy in `config.json` drifts from it, and syncing the copy on launch let
any build or old config repoint or re-enable the user's entry (branch review
finding 5). Microsoft's packaged `StartupTask` and Apple's `SMAppService` both
forbid an application from overriding a user's system-level disable; following
that rule for unpackaged registration gives users one consistent switch.

Starting at login without consent is discouraged on every target platform, so
the default is offered through a visible, pre-ticked choice rather than applied
silently.

## Rejected

- **Config as source of truth, synced on launch.** The current behaviour; causes
  drift, and repoints the entry to whichever build launched last.
- **Writing Windows `StartupApproved` to re-enable.** That store is not a
  documented application interface, and overriding it ignores the user's
  explicit system-level choice.
- **Silent default-on registration.** Replaced by the first-run question.
