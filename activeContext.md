# Active Context

- Mode: implementation; branch-review blockers in progress.
- Phase: `roadmap-slices` review remediation before checkpoint finalization.
- State: 17 findings triaged; #4 fixed (uncommitted), eight blockers remain, #14 rejected, #15 doc correction
  complete. Prior code/checkpoint committed as `52f1f76`; this session's docs are
  uncommitted. Canonical [triage and plan](docs/reviews/code-review-2026-10-03-roadmap-slices-branch.md#captured-implementation-plan);
  ordering and completion live in [roadmap](roadmap.md#branch-review-follow-up).
- Next atomic step: `/next-slice` for consistent config snapshot (#1), grouped with
  live skip interval (#7) since the snapshot publication is #7's hook. Finish blockers before checkpoint finalization;
  remaining phase work and Linux KDE Wayland work retain roadmap order.
- Verification: #4 regressions plus 39/39 Linux CTests pass. Previous baseline:
  39 Linux CTests and Windows Release Host/shortcut test passed. Windows tray,
  shortcut/Settings, seek, browser, and isolated startup acceptance remain pending;
  Now Playing visual acceptance is complete.
- Blockers/env: never enable developer-machine login startup (`PROJECT.md`); use
  `config.development.json`. Quit running Debug Host before replacing its binary.
  Managed Chrome disallows Developer mode; use the user's permitted browser.
- Open questions: startup registration triggers/ownership (#5); runner deadlines
  and partial-output recovery; bake-off execution, Qwen runtime/quantization, release
  sequencing. See captured plan before altering standing recovery/startup intent.
- Discarded as noise: no spool-offset fix for #14 without a recovery use case;
  do not retry Chrome policy bypasses. Review suggestions do not override default-on
  startup intent; unchecked product slices do not expand this branch's scope.
