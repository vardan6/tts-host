# Plan review — TTS Host initial plan

- **Scope:** full plan reviewed as one initial version, not a diff.
  `docs/requirements/product.md`, `docs/design/architecture.md`, `roadmap.md`,
  `activeContext.md`. No ADRs exist yet.
- **Reviewer:** Claude Opus 5 (Anthropic)
- **Date:** 2026-08-27
- **Commit reviewed:** `64fe3c1`

| # | Finding | Why it matters | Severity | Risk | Value | Effort |
|---|---------|----------------|----------|------|-------|--------|
| 1 | Slice 1 bundles project bootstrap, schema validation, model registry, manifest validation, an engine adapter, a runner process, the undecided runner protocol, and WAV output into one "atomic" slice | Violates the one-atomic-slice rule in `AGENTS.md`; a greenfield C++/Qt/Boost slice this wide has no honest done-line and will stall | high | high | high | med |
| 2 | The runner protocol is deferred to "the first vertical slice" (`architecture.md:23`) and listed as open (`roadmap.md:35`), but slice 1's text never mentions choosing it | An unnamed architectural decision is buried inside the first slice's critical path; slice 1 cannot ship without it | high | high | high | low |
| 3 | No authentication design exists, yet `config.json` already ships `"authentication": "automatic"` (`architecture.md:109`) and slice 6 promises "safe authenticated loopback access" | A config key with no defined semantics, and a later slice depending on a scheme no doc specifies. Loopback binding alone does not isolate other local processes | high | high | high | med |
| 4 | No language-detection requirement anywhere, but `languageDefaults` (`architecture.md:129`) and slice 5 (read arbitrary selected text) both presuppose the language is known | Selection reading has no defined way to pick EN vs RU vs HY for arbitrary text; this silently blocks slice 5 | high | high | high | med |
| 5 | `languageDefaults.hy` maps to `"armenian"`, which is not one of the defined profiles (`fast`, `quality`) in the same example (`architecture.md:117-132`) | The canonical config example is internally invalid and will be copied verbatim by an implementer | med | low | med | low |
| 6 | No ADRs exist, though the plan makes at least five durable decisions (C++ host, Qt 6, out-of-process runners, JSON-canonical config, data-only manifests) | `AGENTS.md` routes rationale to `docs/adr/`; today it lives in a design section marked "pending acceptance" and has no home once accepted | med | med | high | low |
| 7 | `roadmap.md:6` commits slice 1 to "a native C++ Windows development build" while `activeContext.md:6` says accepting the C++ host is still the next step | Requirements/design/roadmap disagree on whether a decision is made; the roadmap presupposes its outcome | med | med | med | low |
| 8 | "OpenAI-compatible speech endpoint" (`product.md:80`) and "streaming with cancellation" (`product.md:81`) are not reconciled anywhere | OpenAI's speech endpoint is request/response with no cancellation contract; this almost certainly needs two surfaces, which changes the API design and slice 1's output | med | med | high | med |
| 9 | The bake-off (slice 2) needs load/unload, switching, and VRAM measurement across four models — exactly what slice 3 builds | Sequencing forces throwaway tooling or manual measurement in slice 2; neither is stated as acceptable | med | med | med | low |
| 10 | Slice 2 names metrics (time-to-first-audio, RTF, RAM, VRAM) but sets no thresholds, and `product.md` has no measurable acceptance criteria | A bake-off with no pass/fail bar cannot conclude; "select fast and quality defaults" becomes a taste call | med | med | high | low |
| 11 | Model download requires checksums (`product.md:68`) but no signature or trust root is defined, and the catalog's origin is unstated | Checksums prove integrity, not authenticity. This is the only path where the app fetches remote bytes, and the rest of the security stance is otherwise strong | med | med | med | med |
| 12 | `voices/` appears in the package layout (`architecture.md:150,154`) and voices appear in listings and API info, but no doc defines what a voice *is* | Kokoro voice packs, Qwen speaker prompts, and LuxTTS cloning are structurally different; `model.json` cannot be schema'd without this | med | med | high | med |
| 13 | Armenian is a product requirement, but the only offline option (MMS) is non-commercial (`architecture.md:194`), and slice 7 may resolve Armenian toward cloud Gemini | Local-first is the product's first sentence; Armenian may end up cloud-only or licence-restricted. The tradeoff is unstated | med | med | med | low |
| 14 | Qt licensing is flagged as a live LGPL/commercial risk (`architecture.md:51-55`) and listed as open (`roadmap.md:36`), yet slice 1 builds a Qt host | Discovering a compliance blocker after the UI layer exists means reworking it. The decision is cheap now and expensive later | med | high | med | low |
| 15 | Gemini TTS is designed in (`architecture.md:196`) with no backing requirement — `product.md` never mentions remote providers | Design is ahead of requirements; no doc states whether remote providers are in scope for v1 | low | low | med | low |
| 16 | `config.json` uses `"$schema": "./schemas/..."` (`architecture.md:104`) but installed mode stores config in per-user appdata | The relative schema path cannot resolve in installed mode, so editor validation breaks exactly where non-developers need it | low | low | med | low |
| 17 | `schemaVersion: 1` exists for both config and manifests, but no migration behaviour is defined | "Versioned" without a stated upgrade/refuse/backup rule leaves the first schema bump undefined | low | low | med | low |
| 18 | No update mechanism is defined; only install/uninstall (`product.md:22`) | A zip-distributed desktop app needs a stated update story, even if the answer is "manual replace" | low | low | med | low |
| 19 | UI accessibility is unspecified, though screen-reader users are an obvious audience for a read-aloud tool | A tray UI that a screen reader cannot drive excludes a core user group; cheap to require now, expensive to retrofit | enhancement | med | high | med |
| 20 | Adopt JSON-RPC 2.0 over stdio with LSP-style framing as the runner protocol instead of designing one | Resolves finding 2 with an off-the-shelf, versioned, well-tooled choice that matches "JSON control messages over standard streams" already proposed | enhancement | low | high | low |
| 21 | Defer the Qt commitment: ship slice 1 as a headless service plus CLI, and choose the UI toolkit at slice 4 | Removes the licensing blocker (14) from the critical path, shrinks slice 1 (1), and keeps the cross-platform decision open until there is something to put a UI on | enhancement | low | high | low |
| 22 | Track `config.example.json` and both JSON Schemas in-repo and validate the examples in CI | The two example documents in `architecture.md` are already drifting (5, 16); machine-checking them prevents the class | enhancement | low | med | low |

## Details

### 1 — Slice 1 scope

`roadmap.md:6-9` requires, in one slice: a native C++ Windows build with no external
runtime, validated JSON config loading, discovery of a packaged model, text→WAV
synthesis, and actionable discovery/configuration errors. On a greenfield C++20 /
CMake / Qt / Boost codebase, each of the middle three is independently a slice.
Suggested split: (1a) host process starts, loads and schema-validates `config.json`,
reports errors; (1b) registry discovers and validates one `model.json`, listing it;
(1c) runner protocol chosen and one runner synthesizes to WAV.

### 2 — Runner protocol

`architecture.md:21-23` says the protocol "should be versioned and transport neutral"
and that JSON-over-stdio or a loopback socket are "both suitable; choose one during
the first vertical slice." `roadmap.md:35` lists it under open decisions. Slice 1 as
written cannot be completed without resolving it, but never says so. Either name the
decision inside the slice or promote it to a pre-slice ADR. See finding 20.

### 3 — Authentication

`product.md:75` binds to loopback by default and warns against LAN exposure — that is
the only stated control. But `architecture.md:109` already commits a config key
`"authentication": "automatic"` with no definition of token generation, storage,
rotation, or how the CLI and browser extension obtain a credential. Slice 6 then
promises "safe authenticated loopback access." On Windows, any local user process can
reach a loopback port, so this is the actual security boundary for a service that
speaks and holds model files. Worth an ADR before slice 1 freezes the config schema.

### 4 — Language detection

`architecture.md:129-133` defines per-language defaults, and `product.md:34-35`
requires English quality not to regress from RU/HY support. Slice 5 reads whatever the
user has selected in OneNote or a browser. Nothing states how the language of that text
is determined — explicit per-request parameter, detection library, or per-profile
default. This is the difference between "reads Russian aloud" and "reads Russian text
with an English voice." Cheapest resolution: require an explicit `language` API
parameter, with a stated fallback, and defer detection to a later slice.

### 8 — API shape

OpenAI's `/v1/audio/speech` returns a complete audio body; it has no cancellation verb
and no incremental protocol beyond chunked transfer. `product.md:81` wants low-latency
streaming *and* cancellation, which implies a second surface — the WebSocket capability
Boost.Beast is chosen for (`architecture.md:53`). Recommend stating explicitly: one
OpenAI-compatible compatibility endpoint, one native streaming endpoint, and which one
slice 1 delivers.

### 13 — Armenian

`product.md:34` makes Armenian a supported language. The only offline candidate is MMS
with a non-commercial licence, and `roadmap.md:24-26` defers Armenian to slice 7 where
a cloud provider may be chosen. If Armenian ends up Gemini-only, the product's
local-first claim (`product.md:5`) does not hold for one of its three languages. State
the intended outcome now: acceptable, or a blocker for release.

### 21 — Deferring Qt

The runner boundary already isolates inference, so slice 1's real content is a service,
a registry, and a runner — none of which need a GUI toolkit. Shipping slice 1 headless
with the CLI (`product.md:82`) defers Qt's LGPL/commercial question to slice 4, keeps
the macOS/Linux toolkit decision open, and cuts the first slice roughly in half. The
cost is that the tray UI arrives later, which the roadmap already sequences at slice 4
anyway. This does not contradict a recorded decision — `architecture.md:32` marks the
host stack "pending acceptance."
