# Active Context

- Mode: implementation (behavior-preserving refactor); branch `chore/improve-codebase-architecture` from
  `main` at `a47ebe4`; pushed, open as PR #5 (github.com/vardan6/tts-host/pull/5).
- State: three architecture refactors plus the fix for cross-review
  [finding #1](docs/reviews/code-review-2026-10-26-architecture-deepening.md)
  (profile reads in `with_selected_model()` via `profile_view()`); all
  review findings closed.
- Next atomic step: HITL model-row desktop click-through in `roadmap.md`
  (also covers the damaged-row actions #6), then merge PR #5.
- Verification: Linux build passed; 53/55 CTests passed — config persistence
  fails only against the uncommitted `config.example.json` (passes on the
  committed copy), tray on busy port 7861. Windows not rebuilt since the
  finding fix; prior build passed except its known language-test false
  failure.
- Blockers/environment: do not terminate the running Host without permission
  or link over its locked executable; use a separate build output. Developer
  launches use `config.development.json` with login startup disabled
  (`PROJECT.md`). Linux tests: `build-linux/`; listing models needs `--headless`.
  Existing user changes to `config.example.json` and its `.last-valid` file
  are outside this review; preserve them.
- Open questions: other typed config reads remain
  deferred. Qwen named-speaker/VoiceDesign scope, per-model options, and other
  open decisions remain in `roadmap.md`. Linux Qwen runner-folder loading is
  unverified; previous-only restoration is undesigned.
- Discarded as noise: mixed healthy/damaged copies do not lose rollback or
  the last-model warning (`damaged_package()` excludes loadable IDs); see
  review verification. Port 7861 collision and persistence failure against
  the user-edited example are environment/input issues. Windows language
  test's matching of "fallback" in the build path remains known noise.
  New persistence tests build their own profiles: asserting on the example
  file breaks against the user-edited copy.
