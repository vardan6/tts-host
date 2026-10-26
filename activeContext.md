# Active Context

- Mode: review triage; branch `chore/improve-codebase-architecture` from
  `main` at `a47ebe4`.
- State: three architecture refactors committed through `56155ef`;
  cross-provider review complete with one low-severity spec-delivery gap and
  no confirmed runtime regression. See
  [findings](docs/reviews/code-review-2026-10-26-architecture-deepening.md).
- Next atomic step: `/review-triage` on finding #1 (remaining profile reads
  in config persistence); decide disposition before any fix.
- Afterwards: complete the model-row desktop click-through in `roadmap.md`
  before merge; it also covers the older damaged-row actions (#6).
- Verification: Linux build passed; 53/55 CTests passed, both remaining
  checks passed with isolated inputs. Prior Windows build passed except its
  known language-test false failure; desktop acceptance remains open.
- Blockers/environment: do not terminate the running Host without permission
  or link over its locked executable; use a separate build output. Developer
  launches use `config.development.json` with login startup disabled
  (`PROJECT.md`). Linux tests: `build-linux/`; listing models needs `--headless`.
  Existing user changes to `config.example.json` and its `.last-valid` file
  are outside this review; preserve them.
- Open questions: finding #1 disposition; other typed config reads remain
  deferred. Qwen named-speaker/VoiceDesign scope, per-model options, and other
  open decisions remain in `roadmap.md`. Linux Qwen runner-folder loading is
  unverified; previous-only restoration is undesigned.
- Discarded as noise: mixed healthy/damaged copies do not lose rollback or
  the last-model warning (`damaged_package()` excludes loadable IDs); see
  review verification. Port 7861 collision and persistence failure against
  the user-edited example are environment/input issues. Windows language
  test's matching of "fallback" in the build path remains known noise.
