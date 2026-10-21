- 2026-10-17: Qwen catalogue and local import — added `qwen3-tts-1.7b` (Q4_K_M
  backbone, Q8_0 mmproj, revision-pinned Hugging Face URLs, 1414 MB) to the
  compiled catalogue. Its reference voice is the public-domain LJ Speech clip
  LJ001-0001 from a revision-pinned mirror; entries can now declare host-written
  files, used for the `voiceLicense` provenance notice. Unit tests cover
  generated-file rules, a Qwen-shaped download validating as `qwen3-gguf`, and
  Settings-path import of a Qwen package. On Linux, a scratch harness installed
  the real entry through `download_catalogue_entry` (cached GGUFs, live WAV,
  real checksums); `--list-models` showed it and Host synthesis transcribed
  correctly with Whisper. 48/48 CTests pass. Not yet exercised through
  WinHTTP or Windows Settings.
- 2026-10-17: Qwen speech-speed control — Qwen has no speed input, so the
  `qwen3-gguf` runner streams its PCM through a WSOLA time stretch
  (`time_stretch.cpp`, 30 ms frames, ±10 ms alignment) that keeps pitch and
  incremental emission; the Host's sentence-boundary re-synthesis is unchanged.
  Unit tests pin output length round(n/speed), chunking independence, pitch
  and loudness. 48/48 Linux CTests pass; the real-GPU check scaled 0.5/1.5/2.0
  within tolerance without restarts (generation length varies 5–20% between
  runs even at a fixed seed) and now covers over-context rejection. Not yet
  rebuilt or listened to on Windows.
- 2026-10-16: Qwen end-to-end evaluation integration — optional native
  `tts-host-qwen3-gguf-runner` uses pinned llama.cpp b11029 CUDA libraries,
  Q4_K_M backbone, Q8_0 mmproj, and a declared reference voice. Added validated
  data-only packages, reproducible runtime/package preparation tools, and
  concurrent control/audio draining while retaining complete-sentence recovery.
  47/47 CTests pass on Linux and native Windows Debug. Real GPU Host synthesis
  produced valid two-sentence WAVs on both platforms; opt-in lifecycle checks
  passed resident reuse, unload/reload, bounded cancellation without retry, and
  synthesis afterward. Streaming fixture tests cover pre-reply PCM, partial
  death/retry, timeout, and cancellation. Native lifecycle smoke timings were
  4.659 s for 2.56 s audio (2.239 s first PCM), then 4.633 s for 2.96 s audio
  (1.427 s first PCM); these are not the frozen-corpus bake-off and fail its
  throughput target. Peak VRAM is explicitly unavailable, not measured zero;
  synthesis speed remains 1.0 only, with release speed support tracked separately.
  Evaluation config, package, WAVs and logs are under `build/qwen-evaluation/`;
  normal model registry/config were not changed and the development Host was
  restored with login startup disabled. Build/setup and opt-in checks:
  [README](README.md#qwen-evaluation). Existing planning/acceptance changes and
  this implementation remain uncommitted.

- 2026-10-16: Provisional Qwen evaluation selection — user approved
  Qwen3-TTS-12Hz-1.7B-Base with Q4_K_M backbone, Q8_0 mmproj, and llama.cpp
  CUDA for initial Host integration. Final release defaults remain a bake-off
  output. Streaming, cancellation, resident reuse, and native Windows feasibility
  must be verified before committing to the adapter; no Qwen implementation or
  new runtime acceptance is claimed. See [engine design](docs/design/architecture.md#engines-and-models).

- 2026-10-15: Captured two-model distribution plan on `feature/bundled-models`:
  Kokoro + Qwen, fully offline Windows installer, user-triggered compiled-catalogue
  model updates, and local/catalogue additions. Requirements/design updated;
  roadmap split into dependency-ordered slices. Qwen variant/runtime and model
  revision/storage/release layout remain open; no implementation or acceptance
  is claimed.

- 2026-10-14: Merged and closed `roadmap-slices` — PR #2 merged into `main`
  as `13d5aa9` with the user's approval; branch deleted locally and on origin.
  `roadmap.md` trimmed to open work only (completed slices live here and in
  git history); `activeContext.md` reduced to current state.

- 2026-10-14: Coalesce download activity progress — `DownloadProgressThrottle`
  passes each file's first and completing update and otherwise one per 250 ms;
  `record_transient_activity` replaces that file's newest progress entry, so a
  download keeps one Activity entry per file and repaints only throttled
  updates. Regressions in `tts_host_selection_capture` and
  `tts_host_catalogue_download`; design and finding 4 resolution updated.
  47/47 CTests on Linux and native Windows Debug; no live download exercised.

- 2026-10-14: Now Playing read actions and source targeting — Read
  selection/Read clipboard, saved-target label, utterance source, and capture
  status. Read selection re-finds the shortcut's saved control by UI
  Automation RuntimeId without focus or input and fails closed into the
  status line without interrupting playback. Linux and native Windows Debug
  47/47 CTests each; live external-target read pending the Windows walkthrough.
  The user's played-text history/queue idea is recorded as a roadmap planning item.

# Progress

- 2026-10-14: Reload external configuration edits — re-triaged the checkpoint
  review in Claude Code (no new findings; #1 already fixed). The tray now polls
  `config.json` once a second via `ConfigFileWatcher`: settled valid edits go
  through the Settings-close publication path (hotkey re-registered if
  changed); invalid JSON, schema violations, and a missing file keep the last
  valid document and log the failing path once; changed `/server/*` keys log
  that a restart is needed; polling pauses while Settings is open. New
  `tts_host_config_watch` regression; Linux and native Windows Debug 47/47
  CTests each. Live external-edit acceptance in the running tray not yet done.
  Roadmap follow-up section condensed; review findings 3 and 4 made the next work.

- 2026-10-14: Branch history restored — merged `desktop-follow-ups` into
  `roadmap-slices` as `b4e3e4f` at the user's request, preserving its original
  commits in addition to the earlier cherry-pick. Verified the merge changed
  no file contents and pushed it. Subsequent temporary-branch deletion is
  recorded below. Session closed with review/finalization and PR-workflow
  recovery still pending; no further merge to `main` performed.

- 2026-10-14: Removed temporary branch — verified local and remote
  `desktop-follow-ups` tips are ancestors of `roadmap-slices`, then deleted
  both branch references. All commits remain reachable through the merge;
  current branch remains `roadmap-slices`.

- 2026-10-14: Branch correction — the user clarified that the proposed merge
  and new branch were future steps, not authorization to execute them. Returned
  to `roadmap-slices` and cherry-picked all four state/policy documentation
  changes from `desktop-follow-ups`; implementation and review were already on
  `roadmap-slices`. PR #1 remains prematurely merged into `main`; no revert,
  further merge, or branch deletion performed. Current work is review/finalization.

- 2026-10-14: Windows checkpoint merged — Stop fix/review committed as
  `514c65e`; PR #1 merged with merge commit `001192c`. Updated local `main`
  and created `desktop-follow-ups` for remaining work. Review limits and native
  acceptance gaps remain explicit; no other agent or Claude Code was used for
  the completed review. Saved the user's explicit-agent-approval rule in
  `PROJECT.md`. The development Host was quit for the rebuild and left stopped.

- 2026-10-14: Local checkpoint review and Stop fix — reviewed the desktop
  checkpoint directly in Codex at the user's request, without other agents or
  Claude Code. Report/triage: `docs/reviews/code-review-2026-10-14-checkpoint.md`.
  Found desktop Stop leaving queued reads; routed it through the same scheduler
  queue-clearing operation as browser Stop and added native UI/lifetime
  regression coverage. External JSON reload and Now Playing read/source
  controls are explicit unfinished slices; download progress coalescing is
  backlog. Windows/Linux Debug builds and 46/46 CTests on each platform pass.

- 2026-10-14: Checkpoint PR — committed GUI-launch/Activity log and pending
  developer-workflow changes as `0b6391e`, pushed `roadmap-slices`, and opened
  draft PR #1 (https://github.com/vardan6/tts-host/pull/1). Started a read-only
  Claude Opus 5.5 cross-review of `1ad34c1..0b6391e`; stopped its process at
  the user's request before any findings report returned. Review remains
  pending; the draft description lists remaining native acceptance checks.

- 2026-10-14: GUI-launch and Activity log checkpoint — verified native
  `compile-win.ps1` rebuild and a separate shell launch using development
  config: Kokoro loads, Settings records timestamped model-selection errors,
  and messages survive closing/reopening Settings. Bounded the visible pane
  to the retained process-wide record; regression checks cover 200-entry
  eviction and runner synthesis with null/invalid Windows stderr. 46/46
  CTests pass on Windows and Linux. No login-startup changes or audible probe.

- 2026-10-13: Fresh-build process check — `fresh-win.ps1` now rejects running
  executables from its build tree before deleting build files or dependency
  cache, listing names/PIDs and shutdown instructions. Native verification
  identified Host 22484 and its Kokoro runner 29920 and stopped before cleanup;
  the runner holds `onnxruntime.dll`. Full fresh build awaits Host shutdown.

- 2026-10-13: Windows build-script paths — anchored build, configure, test,
  cleanup, config, marker, and executable paths to `$PSScriptRoot` in
  `compile-win.ps1` and `fresh-win.ps1`; filesystem reads/removals use literal
  paths. Both scripts passed native Windows PowerShell syntax parsing. A full
  fresh run remains pending closing the running development Host.

- 2026-10-02: TTS application icon — added original document/speaker artwork
  inspired by the supplied examples, embedded a multi-resolution Windows ICO,
  and replaced the generic tray icon with the embedded resource at the system
  small-icon size. Native Windows rebuild and visual tray acceptance remain.

- 2026-09-30: Windows startup option verification and developer-launch protection
  — added a native test of the real registration function using a process-local
  HKCU override, covering enable/disable, config updates, Unicode/space quoting,
  and unrelated-entry preservation. All 34 Windows CTests pass; the actual login
  Run key remains unchanged and isolated test keys are cleaned up. Developer
  launches now preserve a separate local config while forcing autostart off;
  first/repeat PowerShell probes passed and the native Host validated the config.
  Settings/sign-in acceptance remains for an isolated environment. The future
  installer choice is captured as the same application setting.

- 2026-09-30: Developer-machine startup preference — removed the login startup
  entry created by launching the Host with the example config, verified its
  absence, and recorded the user's local-development constraint in `PROJECT.md`.
  The application startup option and portable-first release plan remain in place.

- 2026-09-30: Explicit Windows clean and fresh operations — the generated Visual
  Studio clean target had already been verified to retain the configured tree.
  A native `fresh-win.ps1` run reconfigured and rebuilt the Windows tree, then
  passed all 33 CTests. `build-linux/` and the separate FetchContent cache
  remained intact; the reset switch was verified in an isolated PowerShell run.

- 2026-09-30: Windows clean/fresh isolated verification — a native Visual Studio
  `clean` probe removed its executable while retaining `CMakeCache.txt`.
  PowerShell runs of `fresh-win.ps1` with mocked build tools confirmed that the
  normal path removes only `build/`, preserves `build-linux/` and the FetchContent
  cache, and calls configure, build, then CTest; `-ResetDependencyCache` removes
  that cache. The full TTS fresh build and CTest run remain pending because the
  tray Host was running from `build/Debug`.

- 2026-09-30: Browser repeat-injection fix — after the user loaded the unpacked
  extension, repeat actions on one page failed. Reproduced top-level `const`
  redeclaration in both injected scripts and scoped each invocation; a Node VM
  check now sends fresh selection and article text twice on one page. Browser
  reload and native retest remain pending.

- 2026-09-28: Incremental Windows build and run loop — native MSBuild made no
  compiler invocation on an unchanged rebuild, then compiled only the affected
  text-splitter test after its source timestamp changed. The generated clean
  retained the Windows CMake cache, FetchContent directory, and separate Linux
  tree; a full rebuild and all 33 CTests passed. A native `compile-win.ps1`
  run then built, passed 33 CTests, and launched the tray Host process. Fresh
  rebuild acceptance remains separate.

- 2026-09-28: Native Windows/browser follow-up — `compile-win.ps1` built the
  Windows Host and all 33 CTests passed. Chrome 153 launched in an isolated
  profile, but Developer mode is managed by an administrator and off; the
  unpacked extension did not load, so toolbar selection and **Read article**
  were not exercised. Browser verification remains open.

- 2026-09-27: Native browser speech check — TTS Host was listening on Windows,
  but Computer Use stopped before the extension could be loaded or the toolbar
  selection and **Read article** actions could be observed. Verification remains
  open.

- 2026-09-27: Browser article reading — added a page context-menu action that
  sends semantic article/page text through the existing local API; at
  implementation time, Chrome-to-Host interaction was unverified.

- 2026-09-25: Windows start-at-login — added the default-on
  `startup.launchOnLogin` setting, per-user Run-key registration/removal, and
  an immediate Settings checkbox. Native Windows build and login interaction
  remain unverified in the WSL agent environment.

- 2026-09-25: Bake-off readiness review — confirmed Qwen3-TTS has pilot artifacts
  and external runtime measurements, but no TTS Host runner or catalogue entry.
  The exact benchmark corpus is now in `docs/requirements/product.md`; the
  earlier pilot's measurements remain provisional. The benchmark execution path
  is recorded as an open roadmap decision.

- 2026-09-25: Windows developer workflow run report — the user reports that
  native `compile-win.ps1` and `fresh-win.ps1` both ran successfully. A repeated
  no-change incremental run, changed-source rebuild, and generated clean target
  were not confirmed, so the two roadmap items remain open pending those checks.

- 2026-09-18: Desktop feedback consolidation — reduced five pending desktop
  HITL checkpoints to one combined Windows acceptance walkthrough; Copy adapter
  verification is AFK, with unsupported adapters disabled and independent work
  unblocked. Intermediate demos remain optional.

- 2026-09-18: Combined desktop planning — reconciled capture safety, source
  focus, playback states, bounded seek retention, and prototype feedback gates
  across both sessions' pending plans. Implementation remains pending.

- 2026-09-18: Kokoro bounded synthesis — the runner now splits mapped phoneme
  ids into lossless batches of at most 510, preferring punctuation boundaries,
  and emits ordered PCM frames under one synthesis request. Model-free boundary,
  long-unpunctuated-selection, frame-size, phoneme-mapping, and runner-selection
  tests pass on Linux; native Windows tray validation remains.

- 2026-09-18: Windows WASAPI shared-mode format negotiation — playback now
  calls `IAudioClient::IsFormatSupported`, uses the closest endpoint format,
  and converts runner PCM (sample rate, channel count, S16/S24/S32 PCM or
  float32) before buffering it. New platform-independent conversion tests,
  the full 32-test WSL suite, and the native Windows `tts-host` build pass.
  Audible endpoint and tray-selection validation remain required.

- 2026-09-18: Settings hotkey capture and activity log — the selection-hotkey
  field now records a pressed modifier chord rather than requiring manual text,
  and capture/validation/model/import/download output shares the scrollable
  lower activity pane. Native testing of the pre-rebuild binary showed that a
  saved chord reaches synthesis, but fallback Kokoro playback fails at WASAPI
  initialization with `AUDCLNT_E_UNSUPPORTED_FORMAT` (`0x88890008`); a separate
  Debug CRT `abort()` has no stack trace yet. The new Windows-only UI code still
  needs a native rebuild and manual check.

- 2026-09-18: Windows development dependency cache — `compile-win.ps1` still
  performs a clean build/test flow but preserves CMake FetchContent artifacts
  in ignored `cache/fetchcontent/windows-vs2022/`; `-ResetDependencyCache`
  deliberately discards them. README documents incremental edit/test commands.

- 2026-09-18: Configurable selection hotkey — Settings now edits
  `hotkeys.readSelection` through a manual chord field and **Check and save**.
  The check temporarily registers the chord, reporting Windows/application
  conflicts before it writes the configuration; blank disables the selection
  hotkey. The tray releases its current hotkey while Settings is open, then
  applies the saved binding when Settings closes. Native Windows build and
  manual validation remain required.

- 2026-09-18: Tray interrupt and queue semantics — routed tray speech through a single background scheduler, keeping the Win32 message loop responsive. **Read clipboard** and `Ctrl+Win+S` now cancel the current utterance by default; `Queue clipboard` is the explicit opt-in. WASAPI playback observes cancellation promptly, while synchronous runner synthesis stops at the next sentence boundary; queued requests are serialized so playback never overlaps. The changed Linux translation units compile and `git diff --check` passes. Native Windows build and tray/audio interaction remain required; the WSL build cache is Windows-generated and cannot be reused.

- 2026-09-18: Windows tray selection test implementation — added **Read clipboard** and a fixed `Ctrl+Win+S` hotkey. The hotkey sends the standard Copy command to the foreground application, waits for a changed clipboard, and passes that text to the owning Host's current-default-profile synthesis path; no second Host process is started. Linux build and all 31 CTests pass. Native Windows build and manual validation in representative applications remain required, so the roadmap slice is not complete.

- 2026-09-18: Native catalogue-download verification — added the `refresh_model_views` forward declaration and linked `winhttp` into the catalogue test target; the Visual Studio 2022 build succeeds and all 27 Windows CTests pass. The user confirmed the settings window renders. A real Kokoro-82M WinHTTP CLI download into an isolated registry passed checksum verification and discovery as `HAVE`; its temporary files were removed. The user then verified the settings-window Download from `GET` to `HAVE`; the screenshot showed the package installed and loaded. MSVC `getenv` deprecation warnings are non-fatal.

- 2026-09-18: English model bake-off pilot — on the RTX 3070 Laptop, Kokoro's native Windows runner reported 2.742 s to first chunk, 583 MB peak RSS, and 0 VRAM. Qwen3-TTS CustomVoice via PyTorch BF16 measured 3.28 RTF (0.6B; 2.74 GB peak reserved VRAM) and 3.66 RTF (1.7B; 4.65 GB peak reserved VRAM), both hard failures. Qwen3-TTS 1.7B Base Q4_K_M via llama.cpp CUDA generated 12.96 s of audio in 15.87 s (1.22 RTF), also a hard failure. LuxTTS GPU measured 0.18–0.19 RTF and 1.42 GB peak reserved VRAM for the full pilot text; its first sentence took 1.945 s on its first inference and 1.460 s on the immediate repeat, so first-call latency misses the quality limit and warmed latency only narrowly passes. LuxTTS CPU measured 0.47–0.52 RTF and 2.72 GB peak RSS, missing the fast profile. These are pilot timings from external model APIs under WSL2/CUDA, except Kokoro's native Windows runner; only Kokoro exposed the application's `stats` metric, and llama.cpp VRAM was sampled rather than peak-tracked. The requirement references agreed technical and long-form passages, but no such passages are captured in the repository; these results use one provisional technical passage and do not close the bake-off. Audio samples are staged in `build/bakeoff/listen/`; downloaded weights and Python environments remain under `/home/vardana/.cache/tts-bakeoff/`, outside `build/`.

- 2026-09-18: Preferred-profile fallback — added optional `profiles.<name>.fallbackProfile`. English now selects `quality` first and, when that model cannot be resolved or its runner is absent, selects CPU-only `fast` (Kokoro) and reports the reason. The native Windows test build passed all 27 CTests and a manual synthesis with the shipped configuration: absent `qwen3-tts-1.7b` selected `fast` and wrote a Kokoro WAV. This is a testable selection policy; no GPU engine is in TTS Host yet.

- 2026-08-27: Repository and naming — initialized Git, named the product TTS Host, and pushed to private `vardan6/tts-host`; no roadmap step started.
- 2026-08-27: Plan review — reviewed the full initial plan and recorded 22 findings in `docs/reviews/plan-review-2026-08-27-initial-plan.md`.
- 2026-08-27: Plan-review triage and documentation — triaged all findings and applied #5, #8, #12, #16, #17, #19, and #22 to the durable requirements and architecture.
- 2026-08-27: Architecture acceptance — grilled the plan across seven rounds, closed the remaining review findings, and captured the result as ADRs 0001-0004, rewritten requirements and architecture, a resliced roadmap, and an Apache-2.0 LICENSE (`eae88b5`); no roadmap step started.
- 2026-08-27: Host starts and reads its configuration — shipped the first C++20/CMake host slice with `--headless` startup, `config.json` schema validation, JSON-path error reporting, and passing bootstrap tests.
- 2026-08-27: Registry discovery CLI skeleton — added `--list-models`, scanned configured registry directories for package candidates, reported unsupported filesystem entries with actionable reasons, and passed the expanded startup tests.
- 2026-08-27: Registry manifest validation — added tracked `model.schema.json`, validated discovered `model.json` manifests, kept invalid packages visible with schema-specific reasons, and passed the expanded CLI tests.
- 2026-08-27: Registry discovers a model package — rejected package-escaping manifest paths, stabilized registry CLI output ordering, and completed the discovery roadmap item with passing CTest coverage.
- 2026-08-27: Runner control framing — added tested incremental JSON-RPC `Content-Length` framing for host-to-runner control messages; the runner-protocol roadmap item remains in progress.
- 2026-08-28: Runner protocol transport foundation — added typed `initialize` stub handshake, framed binary audio codec, and inherited audio-pipe writer; all native CTest tests pass and the roadmap item remains in progress.
- 2026-08-28: Runner stub synthesis — specified `synthesize` PCM metadata and deterministic end-of-stream stub audio over the inherited pipe; all native CTest tests pass and the roadmap item remains in progress.
- 2026-08-28: Windows native build verified and README fixed — confirmed VS 2022 Build Tools install and native build/test on Windows, fixed a `tts_host_list_models` test failure caused by a forward-slash needle not matching native backslash paths, and cleaned up duplicated/incomplete Windows commands in README; roadmap item remains in progress.
- 2026-08-28: Stub runner launch and WAV synthesis — added a cross-platform `RunnerSession` (fork/exec on POSIX, `CreateProcess` on Windows) that spawns a runner, drives `initialize`/`synthesize` over the control channel, and drains the inherited audio pipe; added `--synthesize`/`--out`/`--runner` to the CLI, a WAV writer, and an end-to-end CTest driving the real stub runner subprocess. Fixed a latent stub-runner deadlock: buffered `std::cin.read` blocks until its full buffer fills on a pipe, so it never saw short writes; replaced with raw short reads. Roadmap item remains in progress — the Kokoro runner is not yet built.
- 2026-08-28: Runner selection by engine — added `--model <id>` to resolve a registry package's `engine` and launch `tts-host-<engine>-runner[.exe]` beside the host instead of always defaulting to the stub runner; `--model`/`--runner` are mutually exclusive, unknown model ids and missing runner binaries fail with actionable errors. 3 new CTests pass (10/10 total, Linux only this session). Roadmap item remains in progress — the actual Kokoro ONNX runner is still not built.
- 2026-08-28: CI deferred, Kokoro-runner dev tooling — dropped the CI pipeline from the current roadmap item and moved it to Deliberately deferred (local CTest already covers it; revisit once the app works end-to-end). Added cross-platform, dev-only Python tooling (`scripts/setup-dev-env.sh`/`.ps1`, `tools/dev-requirements.txt`, `tools/generate_kokoro_runner_fixtures.py`) so the Kokoro runner skeleton has a checked-in placeholder `.onnx` fixture (`tests/fixtures/kokoro_runner/placeholder.onnx`, generated and confirmed working on both WSL and native Windows) without the shipped product ever depending on Python. Decided ONNX Runtime vendoring: prebuilt v1.29.0 release binaries (win-x64/linux-x64) via CMake FetchContent. No CMake/runner code written yet.
- 2026-08-28: ONNX Runtime vendored and toolchain proven — added FetchContent vendoring of ONNX Runtime v1.29.0 (win-x64/linux-x64, SHA256-pinned) with a hand-defined `onnxruntime::onnxruntime` IMPORTED target, since the release archives ship no usable CMake config for their own layout. Added `tts-host-onnxruntime-smoketest`, which loads the placeholder `.onnx` fixture and runs identity inference; wired into CTest (11/11 passing, Linux only this session). Named it `tts-host-onnxruntime-smoketest` rather than the originally planned `tts-host-kokoro-onnx-runner`, since that name is already the stub-runner test double used by `tts_host_synthesis_selects_runner_by_engine`. Roadmap item remains in progress — the real Kokoro runner still needs to be built against this toolchain, and Windows vendoring is unverified this session.
- 2026-08-28: Real Kokoro-onnx-runner speaks the protocol — built `tts-host-kokoro-onnx-runner` as a real executable (`src/kokoro_runner.hpp/cpp`, `src/kokoro_runner_main.cpp`) that mirrors the stub runner's `initialize`/`synthesize` shape but runs an actual ONNX Runtime session (against the vendored placeholder identity model) inside `synthesize`, replacing the CMake alias that previously copied the stub-runner binary under that name. Extracted shared raw stdin/stdout pipe I/O into `runner_control_io.hpp/cpp` to avoid duplicating it between the two runner mains. `tts_host_synthesis_selects_runner_by_engine` now exercises the real runner; 11/11 CTest pass, Linux only this session. Roadmap item remains in progress — the runner still loads a hardcoded placeholder model (protocol v1's `initialize` carries no model path yet), and espeak-ng/real Kokoro-82M weights are not yet vendored.
- 2026-08-28: Roadmap visibility — split the "First audio through the runner protocol" item into sub-slices reflecting completed protocol/runner work (control channel, stub runner, runner-by-engine selection, ONNX Runtime vendoring, real Kokoro runner, `load` request) versus what's left (espeak-ng, real weights); no new code, item remains in progress.
- 2026-08-28: `load` request added to the runner protocol — added a JSON-RPC `load` request/response (`params.modelPath`) per ADR 0002's capability list; the host now resolves a selected package's `files.model` path and sends `load` before `synthesize`, and both the stub runner (ack-only) and `tts-host-kokoro-onnx-runner` (opens its ONNX Runtime session lazily from the supplied path instead of a build-time placeholder macro) implement it. Added a `demo.onnx` fixture copy so `tts_host_synthesis_selects_runner_by_engine` exercises a real load; 11/11 CTest pass, Linux only this session. Roadmap item remains in progress — espeak-ng and real Kokoro-82M weights are still not vendored.
- 2026-08-28: espeak-ng licensing decided, real Kokoro-82M weights wired in — recorded ADR 0005 (espeak-ng runs as an isolated GPL-3.0 subprocess, never linked into any Apache-2.0 binary, reusing the existing runner-process boundary; vendoring itself is a separate, not-yet-implemented slice). Added `tools/fetch_kokoro_weights.py` (stdlib-only download of the real, full-precision Kokoro-82M ONNX model and one voice embedding from `onnx-community/Kokoro-82M-v1.0-ONNX`, Apache-2.0, into gitignored `models/kokoro-en-v1/`). Extended the `load` request with an optional `voicePath` (`runner_protocol.hpp/cpp`, `main.cpp`'s `RunnerSelection`), and `KokoroOnnxRunner` now branches on the loaded ONNX graph's input count at `synthesize` time: one input keeps running the placeholder identity model (existing CTest, untouched), three inputs (`input_ids`/`tokens`, `style`, `speed` — resolved dynamically by name, not hardcoded, since export variants differ) run the real model against a loaded per-phoneme-count voice style table (`voices/af_heart.bin`, one 256-float row per phoneme count, row `n-1` selected per `kokoro-onnx`'s `_style_for`) and convert its float32 waveform output to `pcm_s16le`. Verified end to end: `--model kokoro-en-v1 --synthesize "hello"` produced 32400 real, non-silent sample frames (confirmed via manual WAV inspection); 11/11 CTest still pass (Linux only this session). The runner still feeds a fixed hardcoded phoneme sequence regardless of request text — real Kokoro-82M weights are wired in, but arbitrary-text phonemization (espeak-ng) is not; that remains the roadmap item's last open sub-slice. Multi-size Kokoro model support (small/medium/large quantizations) needs no new mechanism: each is just another registry package id under `models/`, already selectable via `--model <id>`.
- 2026-08-28: espeak-ng licensing decided, real Kokoro-82M weights wired in — recorded ADR 0005 (espeak-ng runs as an isolated GPL-3.0 subprocess, never linked into any Apache-2.0 binary, reusing the existing runner-process boundary; vendoring itself is a separate, not-yet-implemented slice). Added `tools/fetch_kokoro_weights.py` (stdlib-only download of the real, full-precision Kokoro-82M ONNX model and one voice embedding from `onnx-community/Kokoro-82M-v1.0-ONNX`, Apache-2.0, into gitignored `models/kokoro-en-v1/`). Extended the `load` request with an optional `voicePath` (`runner_protocol.hpp/cpp`, `main.cpp`'s `RunnerSelection`), and `KokoroOnnxRunner` now branches on the loaded ONNX graph's input count at `synthesize` time: one input keeps running the placeholder identity model (existing CTest, untouched), three inputs (`input_ids`/`tokens`, `style`, `speed` — resolved dynamically by name, not hardcoded, since export variants differ) run the real model against a loaded per-phoneme-count voice style table (`voices/af_heart.bin`, one 256-float row per phoneme count, row `n-1` selected per `kokoro-onnx`'s `_style_for`) and convert its float32 waveform output to `pcm_s16le`. Verified end to end: `--model kokoro-en-v1 --synthesize "hello"` produced 32400 real, non-silent sample frames (confirmed via manual WAV inspection); 11/11 CTest still pass (Linux only this session). The runner still feeds a fixed hardcoded phoneme sequence regardless of request text — real Kokoro-82M weights are wired in, but arbitrary-text phonemization (espeak-ng) is not; that remains the roadmap item's last open sub-slice. Multi-size Kokoro model support (small/medium/large quantizations) needs no new mechanism: each is just another registry package id under `models/`, already selectable via `--model <id>`.
- 2026-08-30: espeak-ng vendoring mechanics decided, build tooling added — recorded ADR 0006 (Windows vendors espeak-ng via CMake `FetchContent` plus 7-Zip extraction of the official `.msi`, verified against the real installer downloaded and inspected this session; Linux relies on a system package for now, not yet a release target; Kokoro's phoneme-vocabulary mapping is ported from `hexgrad/misaki`, Apache-2.0). Added a "Text-to-phoneme (espeak-ng)" section to `docs/design/architecture.md` and split `roadmap.md`'s espeak-ng item into three sub-bullets citing it; no espeak-ng code written yet. Separately, added CMake `list-models` (fast incremental rebuild+run against the registry fixture) and `package` (CPack `ZIP` generator, matching the Distribution section) targets, verified `package` end-to-end on Linux (built, zipped, unzipped, ran standalone). Rewrote `compile-win.ps1` as the go-to clean/configure/build/test script with step status messages, and documented all three targets (default build, `list-models`, `package`) in README.md. Flagged an unresolved conflict: the Distribution section's illustrative tree (`runtimes/`, `runners/` split) doesn't match `package`'s actual flat output, pending a still-unconfirmed decision on per-engine runner directory isolation.
- 2026-08-30: espeak-ng IPA subprocess capture — added an isolated, shell-free cross-platform subprocess helper and routed real Kokoro synthesis text through `espeak-ng -q --ipa=3 -v en-us --`; added Linux dev setup and deterministic argument/output tests. All 12 CTests pass; IPA-to-Kokoro mapping and Windows MSI vendoring remain.
- 2026-08-30: Markdown normalization — added host-side `normalize_markdown` (`text_normalizer.hpp/cpp`) implementing the five documented rules (code blocks announced and skipped, inline code read, links read as their text, tables skipped with a marker, headings given a leading pause as a block boundary), plus emphasis, blockquotes, list items, thematic/setext breaks, escapes and CRLF; `src/main.cpp` now routes `--synthesize` text through it so runners never see markup. 31-case unit test added (13/13 CTest pass, Linux only). Undocumented judgment calls pinned by tests: images read as alt text, each list item its own block, and `snake_case`/`2 * 3` left intact. HTML normalization is the remaining half of the requirement.
- 2026-08-30: HTML normalization — added `normalize_html` (`text_normalizer.hpp/cpp`) applying the same five rules over HTML tag syntax instead of Markdown (`<pre>`/`<code>` blocks announced and skipped, inline `<code>` read, `<a>`/`<img>` reduced to text/alt, `<table>` skipped with a marker, `<h1>`-`<h6>` as their own block), plus entity decoding and HTML comment stripping, reusing `BlockBuilder` via a new `append_text` helper. New unit tests added to `tests/text_normalizer_tests.cpp`; `ctest -R text_normal` passes. Not wired into `src/main.cpp` — nothing in the codebase yet distinguishes HTML input from Markdown input (likely an HTTP API content-type decision, not yet built), so the function is implemented and tested but unused.
- 2026-08-30: espeak-ng Windows MSI vendoring — added `CMakeLists.txt` `WIN32`-gated `FetchContent`/7-Zip extraction of the official `espeak-ng.msi` (v1.52.0, SHA256-pinned, verified by a real `7z x` extraction producing exactly the 443 flat files ADR 0006 describes), staging `espeak_ng.exe`/`libespeak_ng.dll` and the remaining data files into `espeak-ng-data/` at configure time, then copying both next to `tts-host-kokoro-onnx-runner.exe` in a `POST_BUILD` step matching the existing `onnxruntime.dll` pattern, plus matching CPack `install()` entries. Linux configure/build/ctest (14/14) still pass since the block is fully `WIN32`-gated. Windows-native verification still required — cannot be done from Linux/WSL. `espeak_phonemizer.cpp` was deliberately left untouched: wiring the `--path=<data dir>` argument ADR 0006 specifies would require changing `phonemize_with_espeak_ng`'s fixed 6-argument invocation, which `tests/espeak_phonemizer_tests.cpp` currently asserts exactly — flagged as a follow-up gap, not fixed in this slice.
- 2026-08-30: CLI stdin/clipboard text input — added `--stdin` and `--clipboard` as text sources alternative to `--synthesize` (product.md's "Command-line client accepting direct text, standard input, and clipboard text"), mutually exclusive and each still requiring `--out`; added `src/clipboard.cpp`/`include/tts_host/clipboard.hpp` (Win32 `CF_UNICODETEXT` on Windows, `wl-paste`/`xclip` subprocess on Linux). New `tts-host-config-loader-tests` covers the parsing/validation rules; all 15 CTests pass, and `--stdin` verified end to end against the stub runner. Independent of the in-progress Misaki phoneme-mapping work and the undecided UI toolkit.
- 2026-08-30: Misaki IPA-to-Kokoro phoneme mapping — ported hexgrad/misaki's (Apache-2.0) espeak-ng-to-Kokoro phoneme table into `kokoro_phoneme_mapping.hpp/cpp`, wired into `run_kokoro_synthesis` in place of the hardcoded "hello" token sequence; the mapping (digraph/tie folding, syllabic-consonant handling, the American-English branch, the 114-entry Kokoro vocabulary table) was validated against a real downloaded espeak-ng binary and a Python reference port before translating to C++, with a 9-case unit test added to CTest. Verified end to end with real espeak-ng and real Kokoro-82M weights: distinct input texts ("hi" vs "cat") now produce distinct, non-silent audio, where every request previously produced identical audio regardless of text. All CTests pass on Linux/WSL. Discovered, but did not fix, a pre-existing host/runner audio-pipe deadlock (see roadmap.md's new unchecked bullet under "First audio through the runner protocol"): the runner writes its audio frame before its control response, the host reads the control response before draining the audio pipe, so any synthesis whose audio exceeds the OS pipe buffer (~64KB, i.e. more than a couple of words) hangs both processes forever — the old hardcoded 5-phoneme fixture happened to just barely fit under that limit, which is why this was never hit before.
- 2026-08-30: Windows-native build fixes (first native verification attempt) — `compile-win.ps1` failed configuring because 7-Zip (already installed, required by ADR 0006's MSI vendoring) wasn't on PATH; added a fallback that picks it up from its default install directory with a clear error if truly missing. After that, 2/15 CTests failed on Windows only: `tts_host_kokoro_phoneme_mapping` (root cause: no target had MSVC's `/utf-8` flag, so `\uXXXX` narrow-string literals — the IPA phoneme constants throughout `kokoro_phoneme_mapping.cpp` — were encoded with the locale's execution charset instead of UTF-8 and silently corrupted; fixed with a global `add_compile_options($<$<CXX_COMPILER_ID:MSVC>:/utf-8>)` in `CMakeLists.txt`) and `tts_host_espeak_phonemizer` (Windows' `CreateProcessW` failure path threw a message without "exited with code", unlike the POSIX `execvp`-failure/exit-127 path the test was written against; made it throw "exited with code 127" too). Fixes are uncommitted and not yet re-verified on Windows — awaiting a rebuild to confirm 15/15. Roadmap item remains in progress; the audio-pipe deadlock (next atomic step) is untouched.
- 2026-08-30: Host/runner audio-pipe deadlock fixed — both `kokoro_runner_main.cpp` and `stub_runner_main.cpp` now write the control response before the audio frame, so the host is already draining the audio pipe by the time a large frame arrives instead of blocking on the control response first. 15/15 CTests pass on Linux; verified end to end against real espeak-ng and real Kokoro-82M weights with a sentence producing 342,600 sample frames (685KB WAV, well past the old ~64KB hang threshold) with no hang. "First audio through the runner protocol" is now fully checked off.
- 2026-08-30: `stats` capability implemented for the bake-off — while attempting to actually run the "English model bake-off" HITL step on this RTX 3070 laptop, found the protocol had no way to measure anything: ADR 0002 specifies a `stats` capability (peak RSS, peak VRAM, time-to-first-chunk, sample count) precisely so the bake-off wouldn't need throwaway tooling, but it was never implemented. Added `RunnerStatsRequest`/`RunnerStatsResponse` to `runner_protocol.hpp/.cpp`, a cross-platform `peak_resident_set_size_bytes()` helper (`process_stats.hpp/.cpp`, `getrusage`/`GetProcessMemoryInfo`), wired `stats` handling into both `kokoro_runner_main.cpp` and `stub_runner_main.cpp` (peak VRAM hardcoded to 0 — Kokoro runs CPU-only, no GPU execution provider wired up), and added a `tts-host --stats` CLI flag that prints the numbers after `--synthesize`. New protocol round-trip unit test and e2e CTest; 16/16 CTests pass on Linux. Could not get real Kokoro numbers this session: WSL has no `espeak-ng` installed (needs `sudo`, no password available) and WSL/CPU numbers wouldn't reflect the real Windows-native GPU path anyway — the actual bake-off measurement still needs to run on the Windows-native build once rebuilt.
- 2026-08-30: Host-side audio playback — added `PlaybackSink`/`SystemPlaybackSink` (`playback_sink.hpp/.cpp`) and a new `--play` CLI flag that plays synthesized audio through the default output device via WASAPI on Windows; other platforms throw a clear "not implemented" error, proven end to end by a new CTest. `--play` can be combined with or used instead of `--out`. 17/17 CTests pass on Linux; real playback still needs a manual check on Windows. No chunked streaming, device pinning, or interrupt/queue semantics yet — those remain open sub-bullets under "Everyday desktop playback".
- 2026-08-30: Output-device selection wired end to end — `SystemPlaybackSink::play` now takes a `device_name` (`kSystemDefaultOutputDevice` by default), enumerates WASAPI render endpoints via `IMMDeviceEnumerator::EnumAudioEndpoints`, and matches by `PKEY_Device_FriendlyName`, throwing a clear error if none match; `main.cpp` reads config's `audio.outputDevice` (already schema-validated but previously unused) and passes it through. New CTest (`tts_host_play_forwards_pinned_output_device`) proves a pinned device name from a new fixture config reaches the platform error end to end. 18/18 CTests pass on Linux; real device pinning still needs a manual check on Windows. Chunked streaming, live default-device-change following, and interrupt/queue semantics remain open under "Everyday desktop playback".
- 2026-08-30: Chunked streaming — added `text_splitter.hpp/cpp` (`split_into_sentences`) and wired it into `run_synthesis`: normalized text is split into sentence-scale chunks, each synthesized as a separate back-to-back request in the same runner session, played immediately per chunk (starts playing before the rest of a long text finishes synthesizing), and concatenated for `--out` WAV output. Added a splitter unit test suite and a new end-to-end CTest proving multi-sentence text yields multiple concatenated stub-runner frames; 20/20 CTests pass on Linux. No lookahead overlap (synthesizing the next chunk during current playback) or interrupt/queue semantics yet — both remain open under "Everyday desktop playback".
- 2026-08-31: UI toolkit decision resolved, Windows tray icon implemented — recorded ADR 0007 (native platform APIs per OS — Win32/AppKit/GTK behind the existing small platform-interface pattern — instead of a shared cross-platform toolkit; rejected Qt 6 for its footprint and LGPL compliance overhead and wxWidgets for weaker per-platform consistency than Qt despite its lighter footprint), settling the roadmap's former "Desktop UI toolkit" open decision and updating `docs/adr/0001-cpp-host.md` and `docs/design/architecture.md`'s host-stack section to point to it. Implemented the first slice: `run_tray_icon()` (`tray_icon.hpp/cpp`) shows a Shell_NotifyIcon tray icon with a right-click Quit menu and blocks on a message loop until chosen; `config_loader.cpp` now accepts running without `--headless` as tray mode (rejected only if combined with CLI action flags like `--list-models`/`--synthesize`), and `main.cpp` branches into it. Non-Windows throws a clear not-implemented error, proven by a new CTest; 21/21 CTests pass on Linux. The tray icon itself still needs manual verification on a native Windows build (no GUI in this WSL environment). No settings window or model switching yet.
- 2026-08-31: Release packaging audit — documented fresh-clone, portable, installer, and multi-platform release gaps plus a recommended canonical release-root strategy in `docs/reviews/2026-08-31-release-packaging-audit.md`; no implementation or roadmap item completed.
- 2026-08-31: Build-log tooling — `compile-win.ps1` now writes each configure/build/test run to a timestamped `logs/*_compile-win.log` via `Start-Transcript`, after a stream-redirection attempt (`*>&1 | Tee-Object`) was found to strip `Write-Host` console colors; not a roadmap slice, untested on native Windows.
- 2026-08-31: Default model selection from `profiles`/`languageDefaults` — split `resolve_runner_selection` in `src/main.cpp` so the no-`--model`/`--runner` path resolves `languageDefaults.en` to a profile, that profile's `model` to a registry package, and loads that engine's runner, instead of silently falling back to the unshipped stub runner (closes audit triage row 1). No CTest exercised the old default path (fixtures always pass `--model`/`--runner`), so verification was a Linux build plus a manual `--synthesize` run confirming it now reaches the kokoro runner instead of the stub. Windows-native re-verification still pending.
- 2026-08-31: Tray "Settings…" menu item — `tray_icon.cpp` now takes the loaded `ConfigDocument` and adds a "Settings…" item alongside Quit that calls `run_settings_window`, blocking the tray's own message loop until the settings window closes (no threading yet). Fixed a bug this surfaced: `run_settings_window` unconditionally called `RegisterClassW`, which fails the second time it runs in the same process — now tolerates `ERROR_CLASS_ALREADY_EXISTS`. 22/22 CTests pass on Linux (Win32 code paths only compile-checked); native Windows manual check (click Settings…, close it, click again) still pending.
- 2026-08-31: Windows settings-window shell — added `--settings` (mutually exclusive with `--headless` and CLI action flags) and `run_settings_window()` (`settings_window.hpp/cpp`), mirroring the tray icon: a plain native window blocking on a message loop until closed on Windows, a clear not-implemented error elsewhere. 22/22 CTests pass on Linux (3 new parse_cli cases plus a not-implemented-on-this-platform CTest). No config-editing controls or tray "Settings…" entry yet; real window verification still needs a manual check on Windows.
- 2026-08-31: Release packaging audit verified and triaged — re-checked every claim in `docs/reviews/2026-08-31-release-packaging-audit.md` against the sources (all held) and added five findings to that file: `portable.marker` is an `install(CODE)` rule so every installed tree is portable (contradicts the audit's own single-release-root plan), macOS is absent rather than misconfigured (Linux ONNX archive plus ELF-only `$ORIGIN`/`SONAME` and no eSpeak branch at all), release-asset preparation is Python-only, `FetchContent_Populate` uses the CMP0169-deprecated single-argument form, and install rules read build outputs instead of declared sources. Triaged all 15 findings into the file: no must-fix-now items, two are current-phase (default model selection from `profiles`/`languageDefaults`, now a roadmap checklist item, and model licence display), the rest release-phase backlog. No code changed; no ADR written since the release-root architecture is still a recommendation.
- 2026-08-31: Settings-window output-device control — added `list_output_devices` (WASAPI render-endpoint enumeration, refactored out of the existing `SystemPlaybackSink::play` device-lookup code, `playback_sink.hpp/cpp`) and wired a combo box into `settings_window.cpp` that preselects `audio.outputDevice`, lists "System Default" plus every enumerated device (keeping a no-longer-attached pinned device selectable), and writes the selection back to `config.json` on change; `run_settings_window` now takes the `ConfigDocument`. 22/22 CTests still pass on Linux (compiles clean through the `_WIN32` gate; the control itself needs a native Windows build/manual check). Config live-reload — a running tray/settings session picking up the write without restarting — is not built yet, per `docs/design/architecture.md#live-reload`.
- 2026-08-31: Settings-window server host/port controls -- added EDIT controls for server.host/server.port to settings_window.cpp, preselected from config, written back on focus loss (EN_KILLFOCUS), with invalid/empty input discarded rather than written, plus a static "Restart required to take effect." label since server.host/server.port can't apply live per docs/design/architecture.md#live-reload. 22/22 CTests still pass on Linux (compiles clean through the _WIN32 gate; the control itself needs a native Windows build/manual check). Model manager and hotkeys controls remain the open settings-window sub-bullets.
- 2026-08-31: Settings-window model status and licence display — added a read-only panel listing compatible model packages with licence metadata and unsupported packages with their actionable registry reasons; 22/22 CTests pass on WSL, with native Windows UI verification pending.
- 2026-08-31: Chunked-streaming lookahead overlap — each chunk now plays on a background thread (`src/main.cpp`), joined before the next chunk starts playing, so the next chunk's runner round trip overlaps with the current chunk's playback instead of waiting for it; playback errors are captured on the thread and rethrown at the next join point, preserving the existing not-implemented/pinned-device CLI error tests unchanged. 22/22 CTests pass on Linux; real overlap timing still needs a native Windows manual check. Interrupt/queue semantics remain the last open sub-bullet under "Everyday desktop playback".
- 2026-09-01: Settings-window default-English-profile control — added a combo box (`kDefaultProfileComboId`, `settings_window.cpp`) listing every `profiles` key, preselecting `languageDefaults.en`, and writing the selection back to `config.json` on change, with a "Restart required" label since profile switching isn't live-reloaded. 22/22 CTests pass on WSL; native Windows UI verification pending. Load/unload, idle timeout, directory watching, import, and catalogue download remain the open model-manager sub-bullets.
- 2026-09-02: Model load/unload — added `ModelSessionManager` (`include/tts_host/model_session.hpp`, `src/model_session.cpp`) holding at most one model resident in a live runner process (resolve → launch engine runner → initialize/load handshake; unload terminates the process, since the protocol advertises an `unload` capability but defines no request), wired it into the settings window as a model combo box, Load/Unload buttons, and a status line, and moved model-id resolution out of `src/main.cpp` so CLI synthesis and the UI share one path; `run_settings_window`/`run_tray_icon` now take the runner directory. New `tts_host_model_load_unload` CTest drives a real runner via a fixture package with `"engine": "stub"`; 23/23 CTests pass on WSL, native Windows UI verification pending. A model loaded from the window is resident only for that process, so CLI synthesis still launches its own runner — that closes when a long-lived request source (local API server or synthesizing tray session) exists.
- 2026-09-02: Runner-protocol `unload` request — gave the `unload` method ADR 0002 lists among the initialize capabilities an actual wire contract (`RunnerUnloadRequest`/`RunnerUnloadResponse` in `runner_protocol.hpp/cpp`, no params since this version keeps one model per runner process) and implemented it in both runners: the stub acks it as the conformance fixture, and `KokoroOnnxRunner::handle_unload_message` releases the `Ort::Session` and voice-style table so the process stays alive and reloadable. `ModelSessionManager::unload` now sends it best-effort before terminating the runner. Tests: round-trip, double-unload, and two rejection cases in `runner_protocol_tests.cpp`, plus an initialize → load → unload → load → synthesize sequence against a real stub-runner subprocess in `model_session_tests.cpp`; the best-effort catch was separately verified not to be swallowing a failure via a temporary stderr probe. 23/23 CTests pass on WSL. Note the conflict: the parallel session closing its load/unload slice the same day recorded this request as deliberately discarded ("process exit already frees the weights"), which is true for process-per-model unload but leaves the advertised capability unimplemented and gives idle unloading no way to release weights without respawning. ADR 0002 still names the method without specifying the contract, so the wire shape lives only in the header.
- 2026-09-02: Model package import — added `import_model_package` (`include/tts_host/model_import.hpp`, `src/model_import.cpp`) copying a package directory into the first configured `modelRegistry.directories` entry, with `validate_model_package`/`resolve_registry_directory` promoted out of `model_registry.cpp`'s anonymous namespace so import refuses invalid, incomplete (a declared `files.*` that the package does not carry), duplicate-id, and already-installed packages with exactly the reason `--list-models` prints; the copy is staged under a dot-prefixed directory and moved into place only after it re-validates, so a failed import leaves nothing behind. Wired to `tts-host --headless --import-model <path>` (runs before `--list-models`) and the settings window's Import… button (`kImportModelButtonId`, `IFileOpenDialog` folder picker, refreshes the model combo and licence display; `uuid` added to the Windows link libraries for `CLSID_FileOpenDialog`). Non-obvious: a package's relative `$schema` stops resolving once copied to a different depth, so import repoints the copy at the installation's own `schemas/model.schema.json` — the source manifest is never touched. New `tts_host_model_import` (generated source packages and model directory under the build tree) and `tts_host_cli_model_import` (CLI import → `--list-models` → duplicate refusal) CTests; 25/25 pass on WSL in a separate `build-import/` tree, since a parallel session held `build-linux/`. The Import… button itself is inside the `_WIN32` gate and still needs a native Windows build and manual check.
- 2026-09-02: Request language selection — added `language_selection.hpp/cpp` (`detect_language_from_script` over decoded UTF-8 code points: Latin → `en`, Cyrillic → `ru`, Armenian → `hy`, dominant script wins, ties and letterless text detect nothing; `select_request_language` applying explicit → script → `en`), replacing the hardcoded `languageDefaults.en` lookup in `src/main.cpp` so the request's language names the profile, model, and engine runner. Added `--language <tag>` (rejected beside `--runner`/`--model`, which already name what to speak with, and without a text source), and headless synthesis now prints `Language: <tag> (<source>), profile <p>, model <m>`. Two ordering/behavior notes: normalization moved ahead of runner selection so detection sees speakable text rather than markup, which also means empty-after-markup text now fails before a runner is launched; and a detected language is only honoured when `languageDefaults` has it, so Cyrillic text cannot route to a Russian profile the user never configured, while an explicit unconfigured tag errors by name instead of silently speaking English. Tests: `tts_host_language_selection` (detection, ties, malformed UTF-8, the three resolution paths), `tts_host_cli_language_selection` (a generated two-package registry driven through the real CLI via `--stdin`, since a non-ASCII argv would depend on the Windows code page), and `--language` validation cases in `config_loader_tests.cpp`. 27/27 CTests pass on WSL in `build-linux/`. Only the CLI resolves a language; the settings window and tray still pick models by id.
- 2026-09-02: Model manager idle timeout — added `ModelSessionManager::unload_if_idle(idle_unload_seconds, now)` (`model_session.hpp/cpp`), a clock-injectable check that unloads the resident model once it has been loaded for at least `modelRegistry.idleUnloadSeconds`; `idleUnloadSeconds <= 0` disables it, since the schema's `minimum: 0` otherwise had no meaning. Wired into `settings_window.cpp` as a `WM_TIMER` polling every 5 s (started on window creation, killed on `WM_DESTROY`), the settings window being the only process that currently loads a model. Since no synthesis activity yet feeds the resident session, "idle" means time since load rather than time since last use — noted in the header as an open gap that closes once CLI synthesis or the local API server become long-lived consumers. New cases in `tests/model_session_tests.cpp` cover not-yet-idle, exactly-at-threshold, disabled, and no-model-loaded; 27/27 CTests pass on WSL. Windows timer behavior itself still needs a native manual check.
- 2026-09-02: Model manager directory watching — added `registry_scan_changed` (`model_registry.hpp/cpp`), comparing two `ModelRegistryScan`s by discovered package id/path and unsupported entry path/reason so a poll can tell a real filesystem change from `scan_model_registry` re-validating the same packages; wired into `settings_window.cpp` as a second `WM_TIMER` (same 5 s interval and poll-based mechanism as idle timeout, started only when `modelRegistry.watchForChanges` is true) that rescans and refreshes the model combo/licence display only when the scan actually changed, leaving the combo selection undisturbed the rest of the time. New `tts_host_model_registry_scan_diff` CTest drives real filesystem scans (missing directory, added/removed packages); 28/28 CTests pass on WSL. Catalogue download is the only remaining open sub-bullet under "Model manager and settings window"; native Windows manual check of the timer/UI still pending.
- 2026-09-02: Download catalogue (listing half) — added `model_catalogue.hpp/cpp`: the curated set is a compiled-in `std::vector<CatalogueEntry>` rather than a runtime-fetched or installed-beside-the-binary data file, per `docs/design/architecture.md#download-catalogue`'s trust argument (the catalogue is exactly as trustworthy as the binary, so no signing or key management). Each entry carries licence (with a non-commercial flag badged by `describe_catalogue_license`), and per file an HTTPS URL, pinned lowercase-hex SHA-256, and size; `validate_catalogue` rejects a missing licence, non-HTTPS URL, unpinned/short/uppercase checksum, zero size, duplicate id, duplicate or package-escaping file path (the registry's containment rule applied lexically, since the destination does not exist yet), and the catalogue's own test runs it over the *shipped* entries so a typo fails CTest rather than a user's download. `catalogue_entry_installed` matches an entry id against a `ModelRegistryScan`. Wired to `tts-host --headless --list-catalogue` (prints `GET`/`HAVE`, engine, languages, size, display name, licence); `join_languages` factored out in `src/main.cpp` and reused by `--list-models` with byte-identical output. Kokoro-82M is the only entry — it is bundled per the design, so the entry is for restoring or installing into a second model directory, and its URLs/checksums are `tools/fetch_kokoro_weights.py`'s canonical onnx-community files with the SHA-256s computed from the local copies those URLs produced; Qwen3-TTS, LuxTTS, and MMS Armenian are absent rather than stubbed, because their artifacts are a bake-off output and inventing URLs or checksums would defeat the validation. New `tts_host_model_catalogue` (11 cases) and `tts_host_cli_list_catalogue` (offered, then `HAVE` once a package with that id is dropped into the registry) CTests; 30/30 pass on WSL. The fetch/verify/install half is now its own roadmap sub-bullet.
- 2026-09-06: README rewritten against the actual CLI and UI surface — the old text still claimed the host could not launch a runner, return WAV data, or play audio, all false for several slices. New structure: Windows quick start, "Get a model" (the weights fetch is the only Python step), "Speak something" with the real flag set taken from `--help`, "Desktop app" for the tray/settings window, plus configuration, packaging, runners, and a "What works today" / "Not built yet" split. Windows toolchain setup and the WSL-vs-native `build/` warning kept verbatim. Two limits called out rather than smoothed over: a model loaded from the settings window is resident only for that process, and the server host/port controls say "restart required" for a server that does not exist yet.
- 2026-09-06: First native Windows run of the settings window, and two Windows-only defects fixed. (1) GUI: no control ever received `WM_SETFONT`, so every control fell back to the legacy bitmap `SYSTEM_FONT` — bold and oversized — which overflowed the fixed pixel layout, clipping the `Import…` caption to "Import!" and cutting the second line off "Default English profile:". Added `shell_ui_font()` (`SPI_GETNONCLIENTMETRICS` → `lfMessageFont`) applied to every control in one `EnumChildWindows` pass, widened the label column 120 → 152 (the longest caption needs ~145 px even in Segoe UI), shifted the control column 144 → 176, and the window 480 → 512. (2) espeak-ng crashed with `0xC0000005` on every synthesis: 7-Zip cannot extract the vendored MSI correctly — the embedded CAB stores all 441 payload files under flat names and the directory tree lives only in the MSI's `Directory`/`File` tables, so the build produced no `espeak-ng-data/lang/gmw/en` or `voices/!v/`, voice lookup failed, and espeak-ng null-dereferenced; 7z also mangles `libespeak-ng.dll` → `libespeak_ng.dll`, which the import table will not resolve. Replaced the 7z extraction with `msiexec /a <msi> /qn TARGETDIR=<dir>`, which applies both tables and yields the real layout, deleting the data-staging loop, the DLL rename, and the 7-Zip build prerequisite (`compile-win.ps1`, README); `default_espeak_ng_executable()` now looks for `espeak-ng.exe`, the name the MSI actually installs. Verified the msiexec layout directly (`lang/gmw/en` and `voices/!v/` present); could not verify espeak-ng *executes*, since WSL interop returns exit 5 with no output for that binary regardless of install. 30/30 CTests pass on Linux, which proves nothing here — every changed line is Windows-only. Both fixes await a native `compile-win.ps1` run.
- 2026-09-07: Settings-window background color mismatch — fixed a third Windows-only visual defect (reported via screenshot): STATIC labels and the read-only licence display painted white (DefWindowProc's default `WM_CTLCOLORSTATIC` brush) against the window's unset background, producing a visible white/grey checkerboard. The window class now sets `hbrBackground` to `COLOR_BTNFACE`, and a new `WM_CTLCOLORSTATIC` handler in `settings_window.cpp` returns the same brush with `TRANSPARENT` background mode. Not yet verified on native Windows — joins the font/layout and espeak-ng MSI fixes awaiting the next `compile-win.ps1` run.
- 2026-09-10: Catalogue download itself, closing "Model manager and settings window" — resolved the outbound-HTTP-client decision as WinHTTP, Windows-first per direct instruction (rejected libcurl to avoid a new vendored dependency, matching ADR 0007's native-per-platform policy already used for the tray/settings window/playback sink). Added `download_catalogue_entry` (`include/tts_host/catalogue_download.hpp`, `src/catalogue_download.cpp`): fetches each file over HTTPS with progress reporting and HTTP-Range resumability, verifies it against its pinned SHA-256 (a self-contained, incremental SHA-256 implementation so verification stays testable on Linux even though the WinHTTP fetch itself is Windows-only; validated against the FIPS `"abc"` test vector), generates the `model.json` a downloaded entry has no manifest for, and installs the result via the same staging/validate/rename pattern `import_model_package` uses. `CatalogueFile` gained a `manifest_key` field (e.g. `"model"`/`"voice"`) so the generated manifest's `files` object matches `schemas/model.schema.json`; `validate_catalogue` now enforces it is present, unique, and that one file claims `"model"`. Wired to `tts-host --headless --download-model <id> [--model-directory <path>]`; non-Windows throws the same not-implemented convention as the tray/settings window/playback sink. New `tts_host_catalogue_download` CTest drives the pipeline end to end against a fake `HttpFetchFunction` (fresh download, resume-from-partial, checksum-mismatch rejection-and-cleanup, already-installed short-circuit before any fetch) — the real WinHTTP path itself needs a native Windows run to verify, same limitation as every other `_WIN32`-gated slice. 31/31 CTests pass on WSL. Settings-window download UI is not built — catalogue listing was CLI-only first too, so this follows the same precedent; that plus the still-unverified Windows font/espeak-ng/background-color fixes from the last native run are the open items.
- 2026-09-10: Settings-window catalogue-download UI, closing the last autonomous sub-bullet of "Model manager and settings window" — added a Catalogue combo box (`kCatalogueComboId`, `settings_window.cpp`) listing every compiled-in `model_catalogue()` entry with the same GET/HAVE, size, and licence summary `--list-catalogue` prints, and a Download button (`kDownloadModelButtonId`) calling the existing `download_catalogue_entry` into the first configured `modelRegistry.directories` entry (the same destination `import_model_package` uses), reporting progress and failures (already installed, no configured directory, transport/checksum errors) on the shared model status line and refreshing both the model and catalogue views on success, mirroring the existing Import… button's pattern. 31/31 CTests pass on WSL; the control is inside the `_WIN32` gate and still needs a native Windows build and manual check, joining the existing backlog of unverified Windows-only fixes. Also removed a stray untracked `.kokoro-en-v1.downloading` fixture directory left over from a prior session's run, which was making `tts_host_list_models` fail on an extra phantom unsupported entry. Only hotkeys remain open under "Model manager and settings window", deferred until hotkey capture design exists.

- 2026-09-18: Native Windows tray-selection validation — user confirmed the configured selection shortcut and **Read clipboard** speak through the selected endpoint without closing the runner. Captured the next selection-capture policy: UI Automation first, configurable Copy/clipboard fallback, and browser DOM selection through a first-class extension path; also planned a separate Now Playing window with pause/resume/stop and generated-audio seek controls. No implementation or automated verification was added in this session.
- 2026-09-18: Windows direct selection and capture policy — removed unconditional synthetic `Ctrl+C`; added focused UI Automation `TextPattern` capture off the tray thread with a bounded deadline, late-result rejection, protected/empty/stale-target failures, bounded text-free diagnostics, and manual-copy guidance. Added canonical `selection.capturePolicy` schema/config/Settings support for `automatic`, `uiAutomationOnly`, and `clipboardOnly`; Copy remains disabled until a focused-control adapter passes compatibility verification. Verified 34/34 Linux tests and the native Windows selection test; changed Windows host sources compile, while final relinking was blocked by the running tray executable.
- 2026-09-19: Now Playing speed behavior captured — specified a persisted 0.5×–2.0× typed synthesis option (default 1.0×) and an active-utterance control that takes effect at the next sentence boundary by regenerating unplayed lookahead, avoiding pitch-changing resampling, skipped/repeated text, and false claims of seamless mid-sentence adjustment. Generic model-specific controls remain deferred until the bake-off establishes real capabilities. Planning only; no playback code was implemented.

- 2026-09-19: Now Playing playback-controller foundation — added a shared
  `PlaybackController`/`PlaybackControl` lifecycle for the one active tray
  utterance, with preparing/playing/paused/stopping/idle state, serialized
  replacement cancellation, and cooperative WASAPI pause/resume/stop. Added
  lifecycle/replacement tests; Linux build and all 35 CTests pass. The modeless
  Now Playing UI and speed configuration remain the next slices; native Windows
  interaction verification is still required.

- 2026-09-19: Now Playing controls — added the singleton modeless Windows
  window opened from the tray, with state refresh plus Pause/Resume and Stop
  wired to `PlaybackController`; closing hides it without interrupting speech.
  Linux build and all 35 CTests pass; native Windows interaction verification
  remains required.

- 2026-09-19: Now Playing speed controls — added persisted `audio.speechSpeed`
  (0.5×–2.0×, default 1.0×), Settings and Now Playing controls, typed runner
  speed validation, and stale-lookahead regeneration at a sentence boundary.
  Also corrected focused UI Automation target ownership to compare process ids,
  allowing console/terminal proxy HWNDs while retaining foreground validation.
  Focused Linux tests pass; native Windows rebuild and manual Ctrl+F8 retest
  remain required.
- 2026-09-23: Clipboard-only selection capture — changed the existing `clipboardOnly` policy to read current clipboard text on Ctrl+F8 without synthesizing Copy; Settings now says to copy the intended selection first, and requirements/design plus the review record document stale-text behavior. `automatic` remains the default. `git diff --check` passed; tests were not run, and native Windows verification remains pending.
- 2026-09-24: Generated-audio seek — added source-timeline seeking across generated sentence chunks, sample-rate-aware frame mapping, a 64 MiB RAM cache with a 1 GiB per-utterance PCM spool, configurable 1–30 s interval, paused-seek wakeup, enabled-only Now Playing controls, and visible storage errors. Focused host and test targets compile on Linux; tests were not run, and native WASAPI behavior remains for combined acceptance.
- 2026-09-24: Browser selected-text reader — added a Manifest V3 Chrome extension and loopback Boost.Beast `POST /v1/selection` ingress. The extension reads DOM selection without clipboard access; Host accepts only configured exact CORS origins and submits non-empty text as an interrupting request. Linux host and API test targets compile; tests and browser interaction were not run.
- 2026-09-24: Global selection shortcut toggle — added a checkable tray menu command that registers or unregisters the configured chord without changing its saved binding; Settings preserves a runtime disable when it closes. Native Windows build/interaction and tests remain unverified, so combined acceptance now includes the toggle. Scoped `git diff --check` passed; the repository-wide check reports CRLF-only whitespace on the pre-existing `config.example.json` edit.
- 2026-09-25: Windows developer build/run workflow plan — captured incremental verification, app relaunch, clean, and fresh-build requirements/design in docs and roadmap; README documents the CMake commands currently available. Script modes are not implemented yet.
- 2026-09-25: Local API test shutdown hang — retained the actual bound endpoint after port-0 bind so server teardown wakes the accept loop, and added a 10-second CTest timeout. The focused Windows test remains unrun.
- 2026-09-25: Windows developer build/run scripts — `compile-win.ps1` now configures when needed, incrementally builds, runs CTest, then launches the app; `fresh-win.ps1` rebuilds the Windows tree while preserving the FetchContent cache unless requested otherwise. Linux commands use a separate `build-linux/` tree. Native Windows acceptance was not run, so the roadmap slices remain unchecked.
- 2026-10-03: Browser-local playback controls — added a Chrome control page with Host state, played/generated position, Pause/Resume/Stop, disconnect recovery, and close-without-stop. Native `/v1/playback` WebSocket validates exact origins and commands; asynchronous API connections keep selection requests and shutdown responsive. Stop clears pending reads. Linux and Windows Release Host builds and all three focused CTests pass; native Chrome/audible acceptance remains. The running Debug Host was left running; developer login startup was not changed.

- 2026-10-03: Global shortcut lifecycle verification — moved tray registration and runtime suspension into a platform-neutral helper used by the native menu, Settings round-trip, hotkey dispatch, startup, and shutdown. Added lifecycle regression coverage for suspension preservation, conflict retry, absent bindings, and fresh-process registration. Linux Host builds and all 39 CTests pass. Windows Release Host and test target build; the new CTest passes on Windows. Native menu acceptance remains HITL; developer login startup was not changed.

- 2026-10-03: Now Playing dialog acceptance — user supplied the native Windows dialog screenshot and requested its testing be considered complete. Recorded user acceptance; no additional agent-run native interaction or audible checks were performed. Other combined Windows acceptance items remain pending.

- 2026-10-03: Linux selection target captured — user delegated the X11/Wayland choice based on Ubuntu/Rocky usage and added a preference for KDE/Kubuntu. Selected KDE Plasma on Wayland first, Kubuntu primary native acceptance, Rocky 8/9 KDE compatibility, and X11 later. Saved the target in platform design and roadmap; adapter implementation and desktop acceptance remain pending.

- 2026-10-03: Branch checkpoint assessment and session close — `roadmap-slices` spans 12 commits beyond local `main` across runner, Windows desktop, model-management, browser, and developer-workflow features. Recommended reviewing/finalizing this checkpoint before a separate Linux feature branch; no branch-specific all-roadmap completion contract was found. Commit `d984cdf` saved the pre-session changes; shortcut lifecycle and captured planning remain uncommitted. Native acceptance remains pending; no merge or branch switch occurred.

- 2026-10-03: Branch checkpoint commit and cross-review — committed the remaining shortcut lifecycle code/tests and planning docs as `52f1f76` (no merge or push). Ran `/cross-review` (Claude Opus 5.5; implementer assumed Codex) over `1ad34c1..52f1f76` as a static review with no build or test run, writing 17 findings to `docs/reviews/code-review-2026-10-03-roadmap-slices-branch.md`. Triage and the resulting roadmap follow-ups are owned by a parallel session; this session did not change `activeContext.md` or `roadmap.md`.

- 2026-10-03: Branch review triage and remediation planning — validated all 17 findings against code; recorded nine blockers, deferred phase/backlog slices, and rejection of #14 in the review. Captured planning constraints and verification needs, sequenced roadmap fixes, corrected catalogue verification note (#15), and saved continuation state. Static inspection, plan-link validation, and whitespace checks only; no code changes, runtime tests, registry writes, or commits.

- 2026-10-03: Stop and seek reliably wake paused playback (#4) — `PlaybackControl::cancel()` and `request_seek()` now update wait predicates under `mutex_`; three regressions drive Stop/seek through the predicate-to-wait window via a private test hook (fail without the fix, pass with it; 50 repeats clean). 39/39 Linux CTests pass; no Windows run. Committed with the triage/plan docs as `7b99ddd`.
- 2026-10-03: Speech uses a consistent configuration snapshot (#1) and saved skip interval applies to the next press (#7) — the tray worker takes an immutable `ConfigSnapshot` per request; the tray-owned document is published only after Settings closes, which also applies default speed and seek interval to the controller. New `tts_host_config_snapshot` CTest (in-flight isolation, concurrent no-mixed snapshot, next seek press uses new interval) is TSan clean; 40/40 Linux CTests, 37/37 Windows Debug tests pass, MSVC compiles tray/main; `tts-host.exe` link blocked by a running Debug Host; manual Windows Settings→Now Playing check pending. Uncommitted.

- 2026-10-03: Settings window lifetime (#6) and safe configuration persistence (#8) — guarded tray Settings reentry, kept normal close local to its window, propagated tray Quit through the nested loop, and cleaned up HWNDs before stack state dies. Settings validates before staged atomic replacement, retains the prior schema-valid `.last-valid` copy, restores the document/form after save failure, and reports failures in the activity log. New persistence failure-injection and process-isolated native Win32 regressions pass; Windows Release Host builds; 41/41 Linux CTests and 37/39 Windows Release CTests pass. Two ONNX/API-version mismatch failures reproduce with existing Debug binaries (runtime reports 1.17.1 versus required API 29); separate investigation pending. Initial native harness used unscoped window lookup, changed example seek interval (restored), and may have closed the existing Host; final harness scopes every lookup/count to its own process and uses a temporary config. No startup registry changes or commits. Both slices restore documented intent; no requirements/design/ADR update needed.

- 2026-10-03: Windows API listener exclusivity (#2) — Windows uses SO_EXCLUSIVEADDRUSE before bind; regressions reject a second listener and a reuse-address competitor, report the occupied endpoint, and prove the original listener still serves requests. Linux and native Windows Release local API tests pass. Restores the Local API contract; no durable spec change or commit.

- 2026-10-03: Runner handle inheritance (#10) — Windows process creation uses an explicit stdin/stdout/audio/stderr handle list and cleans temporary handles on launch failures. A native regression launches concurrent sessions, checks unrelated writer EOF while children live, finishes one while the other lives, and exchanges control/audio. Windows Release inheritance and model-session tests pass. Restores ADR 0002; no spec change or commit.

- 2026-10-03: Unicode Windows paths (#9) — use wide process launch/environment and CLI entry, UTF-8 JSON/CLI path conversions, and wide profile-directory lookup; fix schema/registry/model/voice decoding and path diagnostics. Native Windows regressions load a model through the Settings manager and synthesize from a copied portable Host/runner using Armenian/Cyrillic config, directory, model/voice, and WAV names. Linux additionally loads Unicode model/voice paths through the actual Kokoro decoder. 42/42 Linux CTests and 39/41 Windows Release CTests pass; the same two existing ONNX 1.17.1/API-29 failures remain, so native Kokoro loading still awaits that separate DLL-resolution fix. Windows Host and Kokoro runner build. All three batch fixes restore cited contracts; durable-doc check found no requirements/design/ADR updates necessary. No commits or developer login-startup changes.

- 2026-10-04: Runner recovery (#3), prompt retained seek (#12), and runtime regression audit (#17) — bounded cross-platform control/audio/write/exit waits, Stop cancellation, one logged restart/reload/retry with incomplete PCM discarded, and actionable repeated failure. Extracted the portable synthesis coordinator; one lookahead task permits retained seek while synthesis is blocked, preserves pause, and joins playback on errors. Recovery and production-pipeline harnesses pass on Linux and native Windows; 44/44 Linux and 41/43 Windows Release CTests, with the same two existing ONNX/API failures. ADR 0002 records the user-authorized timeout recovery choice; architecture and review audit updated. Kokoro initialize/load measured 0.118/2.088 s on Linux; full synthesis measurement blocked by absent espeak-ng. No commits or developer login-startup changes.

- 2026-10-04: Desktop launch/fatal reporting (#11) — Windows Host uses the GUI subsystem, preserves redirected headless/help streams and runner stderr handles, and shows actionable desktop fatal errors in a native dialog with a failing exit code. Native subsystem/dialog/exit, Unicode CLI, language/profile, and stub synthesis checks pass. No startup-registration policy change.

- 2026-10-04: Resident synthesis lifecycle (#13) — tray/browser/hotkey speech and tray Settings share one resident recovering runner. Sequential reads and explicit Settings loads reuse model weights; eager/on-demand policies, profile idle overrides, actual utterance activity, busy UI controls, model switching, recovery, and warm-up cancellation have regressions. Closing Settings preserves the tray session; standalone Settings remains independent. Linux 45/45 CTests and Windows 43/43 checks outside the two existing ONNX/API failures pass. Architecture and obsolete idle note reconciled; native listening/performance acceptance remains pending. No commits or developer login-startup changes.

- 2026-10-04: Unicode temp-directory startup regression — parallel native checks exposed spool cleanup narrowing unrelated Armenian/Cyrillic filenames and failing Host bootstrap. Use UTF-8 filename inspection and prove cleanup preserves an unrelated Unicode directory. Durable-doc check: restoration of supported Windows behavior; no new contract.

- 2026-10-05: Branch-review remediation checkpoint committed (`b733ad1`) — blockers #1–#4 and #6–#10 plus #11/#12/#13/#17; 45/45 Linux CTests.

- 2026-10-05: Measure sentence transitions (#16) — Windows loopback probe (`tools/playback_gap_probe.cpp`) shows each per-sentence `play()` adds ~80 ms (p95 ~90 ms) of silence; validated against a 20 ms control gap (measured 23 ms). Continuous-stream fix is justified but not scheduled.

- 2026-10-06: Decide startup synchronization (#5) and schedule the #16 fix — user delegated to recommendations; captured ADR 0008 (OS owns login-startup state, first-run pre-ticked question), design sections [login startup](docs/design/architecture.md#login-startup) and [utterance audio stream](docs/design/architecture.md#utterance-audio-stream), Settings tabs, and five Windows roadmap slices. No code changed.

- 2026-10-06: Settings tabs (`cd9e592`) — General/Playback/Models/Server tabs above the activity log; controls keep ids; lifetime test covers tab switching.

- 2026-10-06: OS-owned Windows startup switch (`2894c0b`) — launches no longer register; Settings reads Run + `StartupApproved` (on/off/system-disabled/other copy) on open and activation, writes only Run, and links `ms-settings:startupapps`; isolated-registry tests.

- 2026-10-06: First-run startup question (`93025fc`) — Windows tray launch asks once per data directory (pre-ticked from `startup.launchOnLogin`), writes `startup-choice.marker` whatever the answer, registers only on yes; `compile-win.ps1` pre-creates the marker. 46/46 CTests on Windows and Linux. Native visual check of tabs and dialog not yet done.

- 2026-10-07: Per-utterance stream — `PlaybackSink::open_stream` returns one `PlaybackStream` per utterance; the Windows stream opens an event-driven WASAPI client on the first audio, queues each sentence without draining, flushes on seek, stops on pause/Stop, reopens on a changed default endpoint, format, or invalidation, and drains once at the end (a seek during the drain replays retained audio into the same stream). Fake-endpoint regression in `tts_host_synthesis_pipeline`; gap probe gained a `stream` condition. 46/46 CTests on Windows and Linux. Audible re-measure pending (HITL).

- 2026-10-08: Playback tab tuning — schema adds `audio.bufferMilliseconds`, `sentencePauseMilliseconds`, `lookaheadSentences`; the Playback tab edits them and **Reset to defaults** restores only that tab. The pipeline now appends each synthesized sentence to retained history at once, keeps up to the configured depth of unheard sentences, discards all of them on a speed change at first launch, and inserts the pause as retained source PCM. Pause and lookahead come from the controller (live after Settings publishes); the buffer applies per utterance. Pipeline and Settings regressions; 46/46 CTests on Windows and Linux. Native view of the new controls not yet done.

- 2026-10-13: Unplanned fixes (uncommitted) — GUI-launched Host has no stderr, so runner launch failed with "could not duplicate a handle for runner inheritance (Windows error 6)"; runner stderr now falls back to `NUL`. New process-wide `activity_log` (bounded, in-memory, timestamped) records speech failures, runner restarts, eager-load failures, tray error dialogs, hotkey warnings, selection-capture results, and Settings messages; Settings replays it on open, polls every 500 ms, and its pane is renamed "Activity log". 46/46 CTests on Windows; `tts-host.exe` not re-linked (was running) and not natively verified.

- 2026-10-16: Guided Windows boundary-gap re-measure — shared stream detected no gaps at the 4 ms threshold; separate calls median 73 ms; user heard no gaps, clicks, or interruptions. Five loopback discontinuities retained as a caveat. Partial desktop acceptance and UI findings recorded in [results](docs/reviews/windows-acceptance-2026-10-16.md); remaining Windows checks stay open.

- 2026-10-16: Windows login startup acceptance — closed at the user's explicit request based on their confirmation; agent did not perform isolated-machine testing or change developer-machine startup registration. See [acceptance record](docs/reviews/windows-acceptance-2026-10-16.md#login-startup--user-confirmed-complete).

- 2026-10-16: Partial Windows desktop acceptance — OneNote direct/saved-target selection and empty-selection feedback, external development-config reload, audible playback controls/seek, close-to-hide, and user-confirmed tray shortcut toggle verified; Ctrl+F8 retained. Queue clearing, tray reopening/Settings agreement, terminal/browser/download checks and visual findings remain open in the [acceptance report](docs/reviews/windows-acceptance-2026-10-16.md).

- 2026-10-16: Additional desktop confirmation — user confirmed tray hide/reopen and copy-first reading across Terminal/Codex, embedded terminal, OneNote, and browser. Development `clipboardOnly` requires manual copy; these results do not establish direct capture in every app. Remaining checks updated in the acceptance report.

- 2026-10-16: Windows acceptance completed — user checked all interactive checklist items and explicitly accepted all as done, closing combined desktop acceptance and accepting the recorded UI findings. Login startup and guided gap acceptance are also complete. No additional agent testing or code fixes claimed; synthetic Copy compatibility remains separate. See [final closure](docs/reviews/windows-acceptance-2026-10-16.md#final-closure--all-checks-accepted).

- 2026-10-16: Qwen evaluation cross-review and fixes — Claude Opus 5.5 reviewed `5e03455..5af176b` (7 findings, [report](docs/reviews/code-review-2026-10-16-qwen-evaluation.md)). Fixed #1–#5: ADR 0002 adds a synthesize rejection reply (end-of-stream frame, then JSON-RPC `error`), so Qwen speed ≠ 1.0, prompt overflow, generation limit, and empty output keep the model resident and report their reason instead of restarting and reloading. Multi-language `qwen3-gguf` manifests are rejected, and the first I/O failure is reported. #6 deferred; #7 folded into the release-layout decision. Linux: 47/47 CTests and the real-GPU Qwen integration test pass. Windows not rebuilt.

- 2026-10-16: Select bundled Qwen package — user skipped the English bake-off. [ADR 0009](docs/adr/0009-qwen-release-selection-without-bake-off.md) fixes Qwen3-TTS 1.7B Base Q4_K_M/Q8_0 on llama.cpp b11029 CUDA as the release package: selectable, not a profile default (misses RTF ≤ 1.0; VRAM unmeasured). Requirements, design, README, and roadmap updated; distribution slices no longer wait on a bake-off.

- 2026-10-17: Decide model revision and replacement storage — user accepted all recommendations. [ADR 0010](docs/adr/0010-model-revisions-storage-and-release-layout.md): file-fingerprint revisions in `model.json` (local imports unversioned), per-user Windows installer, read-only bundled models plus per-user storage, catalogue-match-wins precedence, one previous version kept for Revert, per-runner `runners/<engine>/` folders with private libraries (resolves Qwen review #7), CUDA-only Qwen. Requirements, design, and roadmap updated; update slice split in two.

- 2026-10-17: Model revisions and two-tier discovery — catalogue installs write the ADR 0010 fingerprint (`sha256sum`-format listing hashed again) as `revision`; import drops it. The registry scans the executable's read-only `models/` plus the configured writable directories. Within a tier, a later copy of the same id is unsupported (previously both were accepted with no warning). Across tiers the catalogue match wins, otherwise per-user, and the loser is listed as superseded. `--list-models`, `--list-catalogue`, and Settings label packages local/current/update available. SHA-256 moved to `src/sha256.cpp`. Linux: 49/49 CTests (new tier test). Windows `tts-host` compiles; Settings labels not yet viewed on Windows.

- 2026-10-17: Manual catalogue model update — Settings Update/Revert/Delete previous/Remove superseded and matching `--update-model`/`--revert-model`/`--delete-previous-model`/`--remove-superseded-model`. Downloads stage in `.<id>.downloading`. A journaled commit moves the old folder to `.previous/<id>@<revision>` (one kept), and a bundled package's update goes to the per-user directory. Revert swaps the two versions, or removes the per-user copy when the bundled copy is the previous version. Startup recovery completes or rolls back an interrupted commit or revert. The package switch runs under the session speech lock, unloading a resident runner of that model. The registry skips dot-prefixed folders. Linux 50/50 CTests; the 8 model tests also pass natively on Windows, and `tts-host` builds. Settings buttons not yet clicked on Windows; the "unavailable selected voice" report is not separately tested, since the catalogue's voices are the same across revisions.

- 2026-10-19: Two-model release package — runners build and ship in `runners/<engine>/` (engine-ID folders; ADR 0010 amended) with private libraries beside them; the Qwen runner loads llama.cpp/ggml/CUDA from its own directory (`TTS_QWEN_RUNTIME_DIR` and `AddDllDirectory` removed). `TTS_HOST_RELEASE=ON` packages both catalogue-staged models, app-local MSVC runtime, and `licenses/`, and fails on any missing file (checked by removing one Qwen file). `scripts/release-win.ps1` built the 2.3 GB zip; from a clean extracted copy both models list `[current]` and synthesize. Network was not disabled. Linux 50/50 and Windows Debug 50/50 CTests.
- 2026-10-19: Offline Windows installer — Inno Setup 6 `installer/tts-host.iss` (1.99 GB setup.exe, near Inno's 2.1 GB single-file limit). Silent per-user install into a temp folder with startup unticked: no `portable.marker`, per-user config seeded, both models `[current, bundled]` and synthesize, uninstall removes the app and keeps per-user data and another copy's Run entry. Upgrade-over-existing and the interactive startup tick not exercised.

- 2026-10-19: Windows test-fixture abort dialogs — catch runner failure-fixture exceptions and report stderr/nonzero exit so cancellation pipe errors do not invoke the Debug CRT abort dialog. Added a deterministic invalid-control-input regression. Windows Debug: fixture-exit, synthesis-pipeline, runner-recovery, and resident-synthesis CTests pass (4/4). Product behavior and release acceptance next step unchanged.
- 2026-10-19: Debug Qwen runner build — load of the downloaded Qwen 1.7B package failed with "No runner installed for engine 'qwen3-gguf'" because Debug `build/` was configured without `TTS_HOST_QWEN_RUNTIME_DIR`; reconfigured it with the pinned b11029 runtime and built `tts-host-qwen3-gguf-runner` into `build\Debug\runners\qwen3-gguf\` with its runtime. Configuration only, no code change; the reload is not yet verified.

- 2026-10-19: Console-window fix implemented — Windows speech runners now launch with `CREATE_NO_WINDOW`; runner inheritance/protocol/audio CTest passes (1/1). Host compilation succeeded but link failed with LNK1168 because the running Debug Host locks its executable. Quit from tray, rebuild, and verify desktop startup/model loading without a terminal; deployment and visual acceptance remain pending.
- 2026-10-19: Qwen runner always built on Windows — configure falls back to `cache/qwen-evaluation/b11029/runtime` when `TTS_HOST_QWEN_RUNTIME_DIR` is unset (checked with it forced empty); `tts-host` depends on its runners; runtime copies are if-different, so rebuilding while a runner holds its DLLs succeeds (previously "Permission denied"). Your Debug Host loaded the 1.7B model (runner resident, ~3 GB). Windows Debug runner CTests 5/5.
- 2026-10-19: Console-window fix verified — user confirmed the rebuilt Debug Host starts and loads models without a terminal window.
- 2026-10-19: Settings model-combo selection kept across refresh — Delete previous / Remove superseded (and every `refresh_model_views` caller) reset the Model and Catalogue combos to the first entry (Kokoro); the loaded session was untouched. Selection is now restored by id. Compiles; link blocked by LNK1168 (Host running); user verification pending; uncommitted. "has no kept previous version" is correct when no Update has run.

- 2026-10-19: Activity-log scrolling repaint fix — read-only EDIT controls now receive opaque text backgrounds rather than the labels transparent background, erasing old glyphs during scrolling. Windows compilation passes; linking blocked by running Host (LNK1168); visual verification pending.
- 2026-10-19: Model maintenance planning captured — grilled Remove, Verify and repair, Open folder, and a More ▾ menu (no force-reinstall; bundled installed-mode packages not removable or repairable in-app); design section and requirement added, three roadmap slices queued for a new branch after this branch is reviewed and merged.
- 2026-10-19: Bundled-models cross-review triaged and planned — Codex review of `main..feature/bundled-models` (51/51 CTests) gave five findings, all confirmed against code. #1 interrupted-Revert recovery and #4 installer negative startup answer must be fixed before merge, with #5 stale architecture examples; #2 GPU-unavailable reporting (needs an ADR 0002 decision) and #3 stale release stage are queued after merge. Design states the #3/#4 intent; no code changed.
- 2026-10-19: Bundled-models pre-merge fixes — #1: interrupted-Revert recovery
  now validates the restored package before finishing; if it does not
  validate, the working copy goes back in place, the restored version returns
  to its `.previous/` slot, and recovery reports it. New tests inject a crash
  after the second and third revert renames, including an invalid (schema 2)
  previous package; the invalid case fails on the old code. #4: the installer
  shares one ownership check between uninstall and an unticked startup box,
  which now removes this copy's Run entry; `StartupApproved` is untouched.
  The script compiles with ISCC against a dummy tree; it was not run in an
  isolated Windows environment. #5: architecture Qwen manifest example and
  runner-layout sentence corrected. 51/51 Linux CTests pass; Windows build not
  run.
- 2026-10-21: Qwen quality and model options planned — after the user judged
  bundled Q4_K_M Qwen clearly worse than Kokoro, captured catalogue-only Q8_0
  and bf16 variants (design), a listening comparison with a cleaner reference
  clip, CustomVoice/VoiceDesign llama.cpp research, and a per-model options
  plan in `roadmap.md`; the deferred sampling-parameters item folded into the
  last. 1.7B is the largest Qwen3-TTS release. No code changed.
- 2026-10-21: Merge `feature/bundled-models` — PR #3 merged into `main`
  (`01f1684`); review #2 and #3 stay as roadmap slices. Branch deleted locally
  and on `origin`; work continues on `feature/model-maintenance`. The user's
  Windows checks (`fresh-win.ps1`, isolated unticked-startup upgrade, log
  scrolling) were not run before the merge.
- 2026-10-21: Model removal, open folder, and maintenance menu — Settings
  replaces the Revert/Delete previous/Remove superseded buttons with a More ▾
  menu enabling only valid actions, plus Remove… (confirmation with space
  freed, affected profiles, bundled-copy-returns and last-model notes, and a
  "Keep the previous version" tick), Open folder, and Open models folder.
  `plan_model_removal`/`remove_model_package` rename each folder to
  `.<name>.removing` before deleting, so a locked file fails with the package
  whole; start-up recovery deletes leftovers. Bundled packages in installed
  mode are not removable. Linux 51/51 CTests (new removal cases in
  `tts_host_model_update`); Windows `settings_window.cpp` compiles, link
  blocked by the running Debug Host. Menu and dialog not yet viewed on Windows.
- 2026-10-21: Verify and repair catalogue models — More ▾ gains "Verify and
  repair…" (enabled only for a package at the catalogue revision). Hashing runs
  on the model-task worker with progress and Cancel and reports damaged or
  missing files with sizes; after a No-default confirmation, the repair
  re-downloads only damaged files into `.<id>.downloading`, moves good files
  over, validates, and swaps through the `.<id>.repairing` journal, discarding
  the damaged copy via `.<id>.damaged` and leaving `.previous/` alone.
  Start-up recovery rolls back or completes it. Bundled installed-mode packages
  report only. Linux 51/51 CTests (new verify/repair/recovery cases in
  `tts_host_model_update`); Windows Debug build compiles everything and the
  catalogue/update tests pass, `tts-host.exe` link blocked by the running Debug
  Host (LNK1168). Menu and repair dialog not yet viewed on Windows.
