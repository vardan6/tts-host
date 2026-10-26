# Architecture review — 2026-10-25

Deepening opportunities found by `/improve-codebase-architecture` on `main` at
`a47ebe4`. Each candidate moves logic out of a shallow or untestable module
into a deeper one. This document is the canonical record. The
[HTML rendering](architecture-review-2026-10-25.html) is a visual snapshot of
the same content; where they differ, this file wins.

No interfaces are proposed here. Each candidate is grilled before it is
implemented, and the shape that comes out of the grilling goes into
`docs/design/`. Outcome (2026-10-26): every question below is answered in
`docs/design/architecture.md` — action eligibility under model maintenance,
§ Speech queue, and § Profile resolution, which also settles candidate 4.

## Scope and method

- Scope: the hot spots in recent history. `src/settings_window.cpp` was
  touched by 10 commits, `src/model_update.cpp` by 6, `src/main.cpp` by 4, and
  the model session, registry, and list modules by 3 each.
- Vocabulary: module, interface, implementation, depth, seam, adapter,
  leverage, locality. A module is **deep** when its interface is much smaller
  than its implementation. The **deletion test** asks whether deleting a module
  would concentrate complexity (it earns its place) or merely move it (it is
  shallow).
- ADRs were checked for conflicts; none of the candidates contradicts one.

## Ranking

| # | Candidate | Strength | Dependency category |
|---|-----------|----------|---------------------|
| 1 | [Model-action eligibility in the model list module](#1--model-action-eligibility-in-the-model-list-module) | Strong | in-process |
| 2 | [Speech queue module out of main.cpp](#2--speech-queue-module-out-of-maincpp) | Strong | local-substitutable |
| 3 | [One profile resolution module](#3--one-profile-resolution-module) | Worth exploring | in-process |
| 4 | [Typed reads over the config document](#4--typed-reads-over-the-config-document) | Worth exploring | in-process |

Top recommendation: candidate 1. It is the hottest spot, the last review's
findings landed in its untested Win32 code, and the move is small, in-process,
and has an existing test file (`tests/model_list_tests.cpp`) to receive it.

## 1 — Model-action eligibility in the model list module

Files: `src/settings_window.cpp` (`update_model_buttons`,
`on_more_model_actions_clicked`, `on_update_model_clicked`,
`on_remove_model_clicked`), `include/tts_host/model_list.hpp`,
`src/model_list.cpp`, `include/tts_host/model_update.hpp`
(`damaged_package`, the per-action precondition throws).

- **Problem:** the Win32 handlers decide whether Load, Download, Update,
  Revert, Delete previous, Remove superseded, Remove…, Verify and repair…, and
  Open folder apply. They do this by walking the raw `ModelRegistryScan`
  (previous, superseded, damaged, and bundled packages) and the catalogue. That
  code runs only on Windows, has no test, and the
  [model-maintenance review](code-review-2026-10-24-model-maintenance.md)
  follow-ups #6 and #7 (damaged-row actions, tier precedence) were fixed
  there. `model_update` checks the same preconditions again and throws.
- **Solution:** each `ModelListRow` carries the actions available to it and
  the package each action targets. Settings only enables, labels, and
  dispatches.
- **Wins:** locality (the tier and damaged rules sit in one module), the rules
  are tested on Linux through `model_list_tests`, the Settings menu code
  shrinks to rendering, and future macOS and Linux settings UIs reuse the
  rules (leverage).
- **ADR fit:** supports
  [ADR 0007](../adr/0007-native-ui-per-platform.md), since native UIs per
  platform want the rules outside the Win32 module.
- **Questions for grilling:** whether "task running" (`idle`) stays a UI input
  or becomes a parameter; how a row names an action's target package (path or
  candidate); whether `model_update`'s throws stay as a second check or
  become asserts behind the eligibility.

## 2 — Speech queue module out of main.cpp

Files: `src/main.cpp` (`TraySpeechScheduler`, `SpeechRequest`),
`include/tts_host/tray_icon.hpp` (`SpeechQueueMode`),
`src/local_api_server.cpp`, `src/stub_runner.cpp` (test adapter).

- **Problem:** the replace-versus-queue rule, cancelling an eager warm-up when
  a read arrives, the idle-unload poll, reloading after a profile change, and
  the playback-status JSON all live in a class inside `main.cpp`. No test
  reaches them.
- **Solution:** a speech queue module with a small interface (submit, stop,
  publish config, status), with synthesis behind a seam. The real runners and
  the stub runner are its two adapters.
- **Wins:** the queue rules can be tested with the stub runner, two adapters
  justify the seam, `main.cpp` shrinks to wiring, and the
  played-text history and visible queue (`roadmap.md`) would build on this
  module (leverage).
- **Questions for grilling:** whether the seam is `run_synthesis` as it is
  or a narrower "speak one request" function; where the playback-status JSON
  belongs (it is a local API concern); whether the 5-second poll stays the
  idle-unload clock.

## 3 — One profile resolution module

Files: `src/main.cpp` (`resolve_runner_selection`,
`resolve_default_runner_selection`, `resolve_fallback_selection`,
`RunnerChoice`, the load-rejection fallback in `run_synthesis`),
`src/model_session.cpp` (`load_eager`), `include/tts_host/model_session.hpp`
(`ModelRunnerSelection`), and `include/tts_host/synthesis_pipeline.hpp`
(`RunnerSelection`).

- **Problem:** the chain from language to profile to model to
  `fallbackProfile` lives in `main.cpp` and is tested only through CLI CMake
  scripts (`tests/assert_language_selection.cmake`). `load_eager` picks the
  eager profile and reads `idleUnloadSeconds` on its own. `RunnerSelection`
  and `ModelRunnerSelection` hold the same fields and are copied field by
  field.
- **Solution:** one module turns a request plus config into a selection and
  its remaining fallback, and the eager-profile pick moves into it. One
  selection type remains.
- **Wins:** fallback rules are tested without processes, a duplicate struct is
  deleted, and the per-model options slice (`roadmap.md`) would build on this
  module (leverage).
- **Governing docs:**
  [configuration](../design/architecture.md#configuration) and
  [load rejection](../design/architecture.md#load-rejection) define the
  behavior, which must not change.

## 4 — Typed reads over the config document

Files: `src/settings_window.cpp` (33 raw JSON paths), `src/main.cpp` (17),
`src/model_session.cpp` (7), `include/tts_host/config_loader.hpp`.

- **Problem:** callers walk the JSON tree by key name and repeat defaults (for
  example `value("loadPolicy", "onDemand")`), so the config module's interface
  is effectively as wide as its schema.
- **Solution:** the config module exposes typed reads for profiles, the model
  registry, and audio, and keeps key names and defaults inside.
- **Wins:** locality (one home per key and default); the interface shrinks.
- **ADR fit:** [ADR 0004](../adr/0004-json-canonical-config.md) still holds.
  The file stays JSON; only the in-process interface changes. Settings
  writes would still edit the document.
- **Overlap:** candidate 3 removes most of the profile reads; do this after it,
  or fold the profile part into it.
