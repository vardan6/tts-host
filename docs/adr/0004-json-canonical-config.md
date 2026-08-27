# ADR 0004 — JSON is the canonical configuration; the UI is a form over it

- Status: accepted
- Date: 2026-08-27

## Decision

`config.json` is the single source of truth for configuration. A versioned
`config.schema.json` ships with the application and validates it. The settings
window edits that document through the host and maintains no independent
settings store.

Model packages are data only: a `model.json` manifest validated against
`model.schema.json`. Adding a model package never executes code supplied by that
package. Engine adapters are trusted application components, installed
separately from models.

Configuration reloads live on save. See `docs/design/architecture.md` for the
reload rules and the restart-requiring keys.

## Why

Two sources of truth for settings is a defect generator: the file and the UI
drift, and every setting needs migration logic in both directions. One document
means anything achievable in the UI is achievable in the file, so scripts, the
extension, and support instructions all work against the same thing.

Data-only manifests are the security boundary that matters most in practice. The
common failure mode for local ML tooling is a downloaded model package gaining
code execution simply by being placed in a scanned directory. A manifest plus a
registered engine adapter closes that path: unknown weights stay visible as
unsupported rather than being guessed at or loaded with remote code.

Schema versioning is declared now because the first version bump is the moment
it is too late to design. Unsupported versions are rejected with an actionable
error and the file is not rewritten.

## Rejected

- **Settings database with JSON export.** Makes the file secondary and lets the
  two drift.
- **TOML or YAML.** JSON has schema tooling, editor validation via `$schema`,
  and is what the API already speaks.
- **Sniffing model format from file extension.** An extension identifies neither
  architecture, voices, languages, nor device support, and guessing means
  loading untrusted artifacts.

## Enforcement

`config.example.json`, `config.schema.json`, and `model.schema.json` are tracked
in-repo, and CI validates the example configuration and at least one example
manifest against them. The two illustrative documents in `architecture.md` had
already drifted from each other before any code existed; machine-checking them
prevents that class of error.
