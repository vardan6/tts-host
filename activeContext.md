# Active Context

- Mode: session closed.
- Phase/slice: `roadmap-slices` checkpoint complete; branch review/finalization pending.
- State: Existing work committed as `d984cdf`; shortcut lifecycle code/tests and
  session/planning docs remain uncommitted. All 39 Linux CTests pass; Windows
  Release Host builds and the shortcut CTest passes. Now Playing dialog accepted.
- Next atomic step: Review `roadmap-slices` against `main`, including the current
  working tree. Recommendation is to finalize this checkpoint before Linux work;
  commit/merge has not been requested for the remaining changes.
- Next after finalization: Create a focused Linux selection branch from updated
  `main`; implement KDE Plasma Wayland capture per the roadmap. Kubuntu is the
  primary desktop acceptance target; Rocky 8/9 KDE compatibility and X11 follow.
- Pending acceptance: Windows tray icon, shortcut/Settings round-trip, seek,
  browser article/playback, and startup Settings/sign-in in an isolated environment.
- Blockers/environment: Agent Chrome disallows Developer mode; use the user's
  permitted browser. Never enable developer-machine login startup (`PROJECT.md`);
  use `config.development.json`. Running Debug Host blocks replacement; quit it
  before rebuilding Debug. Release builds are available.
- Open questions: English bake-off execution, Qwen runtime/quantization, release
  sequencing. Branch-wide review and merge approval remain pending.
- Discarded as noise: Do not retry Chrome `--load-extension` or WSL CDP to bypass
  managed policy. Unchecked product-roadmap items do not define this branch's scope.
