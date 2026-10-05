# Code review — `roadmap-slices` branch

- **Range reviewed:** `1ad34c1` (merge-base with `main`)..`52f1f76` — 13 commits,
  142 files. Focus: host runtime (`main.cpp`, runner launch, playback, local
  API, tray/Settings, startup, downloads/import, extension). Kokoro numerics,
  text normalizer, and CMake packaging were skimmed only.
- **Docs consulted:** `docs/requirements/product.md`,
  `docs/design/architecture.md`, ADR 0002, ADR 0003,
  `docs/implementation-notes.md`, `PROJECT.md`, `roadmap.md`.
- **Reviewer:** Claude Opus 5.5 (Anthropic). The implementer is assumed to be a
  different provider (Codex); if not, treat this as a same-provider review.
- **Date:** 2026-10-03. Static review only; nothing was built or run.

| # | Finding | Evidence (file:line) | Severity | Risk | Value | Effort |
|---|---------|----------------------|----------|------|-------|--------|
| 1 | Settings mutates the shared `ConfigDocument` on the tray thread while the speech worker reads the same object unsynchronized during every request (data race on `nlohmann::json`, UB/crash). | `src/main.cpp:546`, `src/main.cpp:599`, `src/main.cpp:234`, `src/tray_icon.cpp:256`, `src/settings_window.cpp:207` | high | high | high | med |
| 2 | The local API sets `reuse_address(true)`. On Windows that is `SO_REUSEADDR`, so a second Host instance (login autostart plus a manual launch) binds the same port instead of failing. No single-instance guard exists. This contradicts "on collision the host fails to start". | `src/local_api_server.cpp:63`; `docs/design/architecture.md` (Local API, port collision) | high | high | high | low |
| 3 | No timeout on any runner read or write, and no runner restart/retry. A hung runner blocks the scheduler worker forever, so new requests, Stop, and Quit (the destructor joins) all hang. ADR 0002 "Runner failure" (restart and retry the chunk once, log restarts) is unimplemented. | `src/runner_launcher.cpp:164-229`, `src/main.cpp:348-351`, `src/main.cpp:466-475`; `docs/adr/0002-runner-protocol.md` §Runner failure | high | high | high | med |
| 4 | Lost-wakeup race: `PlaybackControl::cancel()` and `request_seek()` update atomics and notify without taking `mutex_`. A paused playback thread can miss Stop or seek and block forever, which then hangs the worker's `join_playback`. | `src/playback_controller.cpp:7-10`, `:32-44`, `:50-56` | med | med | high | low |
| 5 | A missing `startup` block is treated as `launchOnLogin: true`, and every non-headless launch (including `--settings`) rewrites the Run key to *this* executable and config. Any older or hand-written config therefore silently enables login startup on the developer machine, against `PROJECT.md`. A debug build also repoints the user's login entry. The design describes only the Settings toggle, not launch-time sync. | `src/main.cpp:576-584`; `schemas/config.schema.json` (`startup` not required); `PROJECT.md` | med | high | med | low |
| 6 | Settings' nested `GetMessageW(nullptr)` loop dispatches tray messages, so the tray stays live while Settings is open, contrary to the comment. Settings can be opened re-entrantly (two windows editing one document, with hotkey release/restore interleaved). Quit during Settings ends the inner loop, then `restore` re-registers the hotkey on a destroyed HWND and shows a spurious "unavailable" warning. | `src/tray_icon.cpp:247-263`, `src/settings_window.cpp:1031`, `src/settings_window.cpp:743` | med | med | med | low |
| 7 | Changing `audio.playbackSkipSeconds` in Settings is not applied to the running tray. Only speech speed is pushed, and the seek interval is set once in the scheduler constructor. The requirement says it "takes effect for the next button press". | `src/main.cpp:462-463`, `src/tray_icon.cpp:260-261`, `src/settings_window.cpp:207`; `docs/requirements/product.md` §Configuration | med | low | med | low |
| 8 | `save_config` truncates `config.json` in place with no temp-file-and-rename step, no schema validation, and no retained last-valid copy. A crash or full disk mid-write corrupts the canonical config. The design says writes are atomic and retain the last valid copy. | `src/settings_window.cpp:119-125`; `docs/design/architecture.md` §Configuration | med | med | high | low |
| 9 | Windows paths are narrowed to the ANSI code page: `CreateProcessA(path.string())`, and model/voice paths go into the JSON `load` request via `path.string()`. A non-ASCII install or profile path (likely for the Russian/Armenian audience) fails to launch, or produces invalid UTF-8 JSON. | `src/runner_launcher.cpp:106-109`, `src/main.cpp:224-227` | med | med | high | med |
| 10 | Runner `CreateProcess` uses `bInheritHandles=TRUE` without `PROC_THREAD_ATTRIBUTE_HANDLE_LIST`. A runner started concurrently (Settings Load plus tray speech) inherits the other session's inheritable pipe ends, so EOF on runner death never arrives and reads block (compounds #3). | `src/runner_launcher.cpp:66-109` | med | med | med | med |
| 11 | `tts-host` is a console-subsystem executable with no `FreeConsole` or `CREATE_NO_WINDOW`. A login start or Explorer launch of the tray host opens a console window, and fatal startup errors (for example a port collision) go only to stderr, which the user may never see. | `CMakeLists.txt:167-191`, `src/main.cpp:736-738`, `src/runner_launcher.cpp:107` | med | low | high | low |
| 12 | Seek has an audible gap: an interrupted `play_from` returns at once, but the new position plays only after the main loop finishes synthesizing the *next* sentence. Silence therefore lasts up to one sentence's synthesis time. | `src/main.cpp:328-395` (seek applied only after `send_request`/`read_audio_stream_until_end`) | low | low | med | med |
| 13 | Every tray, browser, or hotkey request spawns a fresh runner and reloads the model, and profile `loadPolicy` is ignored. Time to first audio includes process start plus model load on every read. This undercuts the bake-off latency targets and "Long text begins playing quickly". | `src/main.cpp:214-229`; `docs/design/architecture.md` (Host owns model lifecycle), `docs/implementation-notes.md` (`unload_if_idle`) | enhancement | low | high | high |
| 14 | `AudioHistory::append` can leave a partial write in the spool without advancing `size_`, so later offsets no longer match the file contents. | `src/audio_history.cpp:49-60` | low | low | low | low |
| 15 | Implementation note drift: the catalogue note says checksums are verified only after all files reach size. The code verifies each file right after its size check. | `docs/implementation-notes.md` (Catalogue download), `src/catalogue_download.cpp:221-249` | low | low | low | low |
| 16 | Each sentence opens a new WASAPI client and drains to zero padding before the next, which adds a gap at every sentence boundary. A single long-lived stream per utterance would give gapless playback and simplify pause/seek. | `src/playback_sink.cpp:186-345` | enhancement | low | med | high |
| 17 | Missing tests: no automated coverage for concurrent Settings/worker config access, the cancel-while-paused race, a second instance binding the same port, or runner hang/death. All four map to the higher findings above. | `tests/` | enhancement | low | med | med |

## Details

**1.** In `main.cpp`, `document` is passed by reference to both the
`TraySpeechScheduler` (as `const &document_`) and `run_tray_icon` (as a mutable
`ConfigDocument&`). Settings writes `state.document.value[...]` on the UI thread
while `run_synthesis` reads `document.value.at("audio")`, `languageDefaults`,
and `profiles` on the worker thread. Fix: give the worker a snapshot, such as a
`shared_ptr<const ConfigDocument>` swapped under a mutex after Settings closes
or saves. This also gives a natural hook for #7.

**2.** Remove `reuse_address` on Windows, or set `SO_EXCLUSIVEADDRUSE`.
`SO_REUSEADDR` is only needed on POSIX for TIME_WAIT. Also add a named-mutex
single-instance guard that tells the user the Host is already running.

**3.** Scenario: the runner deadlocks inside ONNX or espeak-ng. The worker
blocks in `ReadFile`, Stop only sets a flag, and Quit joins the worker
indefinitely. Minimum fix: a bounded deadline on reads (overlapped I/O or a
watchdog that calls `TerminateProcess`), then the ADR's restart-and-retry-once
behavior with logging.

**4.** Take `mutex_` (even briefly) before `notify_all` in `cancel()` and
`request_seek()`, or set the flags under the lock.

**5.** Suggested approach: apply the Run key only from the Settings toggle, as
designed, or at least skip it when `startup` is absent. Decide whether
launch-time sync is intended. If it is, record it in the design.

**6.** Either disable the tray context menu while Settings is open (a flag in
`TrayContext`), or make Settings a modeless window owned by the tray loop, with
no nested loop.

**9.** Use `CreateProcessW` with `wstring()`. Send UTF-8 paths in JSON
(`u8string()`), and have the runner convert them back with
`std::filesystem::path(std::u8string)`.

**13.** This is already acknowledged in the implementation notes for
`unload_if_idle`. It is listed so triage can decide whether a resident tray
runner belongs before the bake-off.

## Triage — 2026-10-03

Validated against the current working tree; static inspection only. No application
launches, registry changes, builds, or tests were performed. This request is triage
only; implementation is not authorized by this skill invocation.

Must fix now (9): #1, #2, #3, #4, #6, #7, #8, #9, #10.

| # | Finding | Risk | Value | Effort | Priority | Reason |
|---|---------|------|-------|--------|----------|--------|
| 1 | Shared configuration data race | high | high | med | must_fix_now | Worker reads the same JSON that Settings mutates; undefined behavior. |
| 2 | Windows API port exclusivity | high | high | low | must_fix_now | Reuse permits conflicting Windows binds; successful second listen is not established. |
| 3 | Unbounded runner I/O and missing recovery | high | high | med | must_fix_now | Blocking reads/writes and infinite process wait can hang Stop/Quit; ADR retry is absent. |
| 4 | Paused playback lost wakeup | med | high | low | must_fix_now | Atomic predicate updates outside the wait mutex can lose cancellation/seek notifications. |
| 6 | Reentrant Settings and tray teardown | med | med | low | must_fix_now | Nested loop dispatches tray commands; Quit destroys the HWND/context before restoration. |
| 7 | Skip interval does not apply live | low | med | low | must_fix_now | Settings persists the value but never updates the controller; next-button contract is broken. |
| 8 | Unsafe configuration persistence | med | high | low | must_fix_now | In-place truncation risks losing config; atomic writes and retained valid copy are required. |
| 9 | Unicode runner/model paths | med | high | med | must_fix_now | ANSI launch and narrow load paths break valid Windows installations; include model_session.cpp. |
| 10 | Unrestricted inherited pipe handles | med | med | med | must_fix_now | Concurrent launches can retain unrelated pipe writers and prevent EOF after runner death. |
| 5 | Launch-time startup registry synchronization | high | med | low | should_fix_before_phase_complete | Default true is specified; unconditional executable/config repointing needs an explicit policy decision. |
| 11 | Console window and startup error visibility | low | high | low | should_fix_before_phase_complete | Console subsystem and stderr-only fatal reporting undermine desktop launch usability. |
| 12 | Seek waits for in-flight synthesis | low | med | med | should_fix_before_phase_complete | Seek interruption is immediate but replacement playback waits for the synchronous lookahead request. |
| 13 | Runner reload per request and ignored loadPolicy | low | high | high | should_fix_before_phase_complete | Tray synthesis bypasses the resident manager; restore lifecycle before performance acceptance. |
| 17 | Regression coverage gaps | low | med | med | should_fix_before_phase_complete | Basic pause/seek tests exist; race, concurrent config, collision, and hang/death cases need targeted coverage with fixes. |
| 15 | Catalogue implementation note drift | low | low | low | backlog | Code hashes each file after its size check; correct the inaccurate ordering note. |
| 16 | WASAPI client recreated per sentence | low | med | high | backlog | Per-call activation and drain are confirmed; measure boundary gaps before a stream redesign. |
| 14 | Partial spool write corrupts later offsets | low | low | low | invalid | append throws and run_synthesis aborts; no subsequent append reuses this utterance history. |

Recommended immediate action: fix blockers in atomic slices, including relevant regression coverage, then `/session-close` STEP mode.

Docs impact: this review and `roadmap.md`; #5 needs a startup policy decision before implementation. #15 targets `docs/implementation-notes.md`. Any intentional behavior change goes through `/doc-update`.

### Validation qualifications

- #2: `LocalApiServer` enables reuse before bind/listen. Require Windows exclusive
  ownership and verify a second Host fails clearly. A named mutex is an optional
  implementation choice, not a separately established product requirement.
- #5: `product.md` Configuration and controls and architecture Desktop integration
  explicitly specify default-on startup. Missing `startup` therefore does not
  itself violate intent. `PROJECT.md` requires a development config with startup
  disabled. Launch-time repointing remains a policy gap; do not remove default-on
  behavior by treating the review suggestion as an approved specification.
- #6: Settings uses a thread-wide GetMessage loop and posts WM_QUIT when closed.
  Tray Quit can destroy TrayContext during the nested call; post-return access is
  more serious than a spurious registration warning.
- #9: runner launch, both host load call sites, and runner conversion from protocol
  strings must agree on Unicode/UTF-8 handling. Changing CreateProcess alone is insufficient.
- #14: failed append unwinds synthesis; no recovery path appends again to that
  AudioHistory. A transactional append is optional defensive work, not a current
  demonstrated offset corruption bug.
- #17: `playback_controller_tests.cpp` already exercises pause/resume and paused
  seek, but uses an established waiter; it does not probe the predicate-to-wait
  lost-wakeup window. Keep missing regression cases with their owning fixes.

### Finding 1

Triage: `must_fix_now`. Worker reads the same JSON that Settings mutates; undefined behavior.

### Finding 2

Triage: `must_fix_now`. Reuse permits conflicting Windows binds; successful second listen is not established.

### Finding 3

Triage: `must_fix_now`. Blocking reads/writes and infinite process wait can hang Stop/Quit; ADR retry is absent.

### Finding 4

Triage: `must_fix_now`. Atomic predicate updates outside the wait mutex can lose cancellation/seek notifications.

### Finding 6

Triage: `must_fix_now`. Nested loop dispatches tray commands; Quit destroys the HWND/context before restoration.

### Finding 7

Triage: `must_fix_now`. Settings persists the value but never updates the controller; next-button contract is broken.

### Finding 8

Triage: `must_fix_now`. In-place truncation risks losing config; atomic writes and retained valid copy are required.

### Finding 9

Triage: `must_fix_now`. ANSI launch and narrow load paths break valid Windows installations; include model_session.cpp.

### Finding 10

Triage: `must_fix_now`. Concurrent launches can retain unrelated pipe writers and prevent EOF after runner death.

### Finding 5

Triage: `should_fix_before_phase_complete`. Default true is specified; unconditional executable/config repointing needs an explicit policy decision.

### Finding 11

Triage: `should_fix_before_phase_complete`. Console subsystem and stderr-only fatal reporting undermine desktop launch usability.

### Finding 12

Triage: `should_fix_before_phase_complete`. Seek interruption is immediate but replacement playback waits for the synchronous lookahead request.

### Finding 13

Triage: `should_fix_before_phase_complete`. Tray synthesis bypasses the resident manager; restore lifecycle before performance acceptance.

### Finding 17

Triage: `should_fix_before_phase_complete`. Basic pause/seek tests exist; race, concurrent config, collision, and hang/death cases need targeted coverage with fixes.

### Finding 15

Triage: `backlog`. Code hashes each file after its size check; correct the inaccurate ordering note.

### Finding 16

Triage: `backlog`. Per-call activation and drain are confirmed; measure boundary gaps before a stream redesign.

Measured 2026-10-05 on the developer Windows machine (default endpoint, 48 kHz
shared mix) with `tts-host-playback-gap-probe` (`tools/playback_gap_probe.cpp`,
Windows-only, audible). It plays 0.4 s 440 Hz tones at Kokoro's 24 kHz mono
format and records the endpoint by WASAPI loopback, detecting the tone by
frequency so other applications' audio is ignored.

| Condition | Boundaries | Gap median | p95 | max |
|---|---|---|---|---|
| Control: one `play()` with a 20 ms silence between tones | 30/30 | 23 ms | 23 ms | 23 ms |
| One `play()` per tone on a fresh thread, as the pipeline does | 30 (+6 short dropouts of ~4 ms) | 83 ms | 93 ms | 93 ms |
| Wall-clock `play()` time beyond audio duration | 31 calls | 81 ms | 87 ms | 88 ms |

The control reads about 3 ms high (fade edges), so the audible gap the sink adds
per sentence boundary is about 80 ms (p95 about 90 ms). That matches the
per-call wall-clock overhead: endpoint resolution, activation, and Initialize on
every call, plus the 50 ms drain poll detecting an empty buffer late. Three
earlier runs with a broadband detector gave 66–88 ms median wall-clock
overhead; their audible counts were unreliable because of other system audio,
so the detector was replaced. The pipeline adds at most one 10 ms lookahead
pump before launching retained audio. The model's own pauses between
sentences come on top of this. The short dropouts within tones on the
per-call path are unexplained and were not seen in the control.

Conclusion: the gap is systematic and audible, not jitter. A continuous
utterance stream (or at least a reused, pre-started client per utterance with
event-driven feeding) is justified on evidence; whether to schedule it is a
roadmap decision.

### Finding 14

Triage: `invalid`. append throws and run_synthesis aborts; no subsequent append reuses this utterance history.

## Captured implementation plan

Captured at the user's request on 2026-10-03. This is a planning checkpoint;
unchecked roadmap slices remain unimplemented. The triage table owns priority
and rationale; roadmap owns ordering and completion, activeContext owns live state.

### Routing and constraints

- **Requirement:** #2, #3, #7, #8, #9, and #13 restore existing port collision,
  recovery, live controls, valid configuration, supported Windows paths, and model
  lifecycle expectations. Their roadmap slices cite the governing specs; no
  replacement requirements are inferred from this review.
- **Design:** #1, #4, #6, #10, and #12 need concurrency/lifetime fixes. Use immutable
  per-request config snapshots published without racing Settings; couple paused
  wait predicates to their mutex; keep Settings and tray teardown lifetimes safe;
  restrict child handle inheritance; decouple retained-audio seek from lookahead.
  These are proposed approaches, not frozen APIs or an implementation spec.
- **Roadmap:** #11 is desktop launch/error polish; #17 belongs in regression
  coverage accompanying the owning fixes, with an audit slice to close gaps.
  #13 precedes lifecycle/performance acceptance, but does not settle model choice.
- **Open question:** #5 launch-time registry synchronization is unspecified while
  default-on startup is specified. Decide registration triggers and ownership
  before changing startup behavior; preserve PROJECT.md's local restriction.
  No ADR is created before that decision exists.
- **Implementation note:** #15 corrects verification ordering without changing code.
- **Risk:** #16 confirms per-sentence endpoint creation but does not quantify
  audible gaps; measurement gates any continuous-stream redesign.
- **Temporary:** #14 is rejected as a current bug because append failure ends the
  utterance. Do not schedule transactional append work without a recovery use case.

### Verification and unresolved choices

Blocker fixes need targeted automated regressions plus appropriate native Windows
checks. Preserve already accepted Now Playing behavior; exercise relevant controls
rather than reopening its completed visual acceptance. Never modify this developer
machine's login startup registration or perform sign-in acceptance here.

Runner failure work must bound control/audio reads, writes, process exit, and
shutdown, then implement the ADR's one retry and restart logging. Choose deadlines
that tolerate legitimate model load/synthesis; values remain an implementation
choice to justify with measurements. Verify child death, a silent hung child,
Stop/Quit, repeated failure, and retry without replaying already audible PCM.
A killed hung runner and retry after partial output need explicit handling; the
existing ADR only specifies death and one failed-chunk retry. If the solution
changes standing recovery semantics, capture the decision before implementing it.

Config persistence needs failure injection proving the original valid file survives
validation/write/replace failure and that the retained copy remains usable. Config
publication must not let an in-flight request observe mixed Settings values.
Windows exclusivity tests must check bind/listen outcomes rather than assume
SO_REUSEADDR always permits a second listening Host. Unicode tests must traverse
launch and protocol decoding, including both tray and Settings model loading.

Sequence is canonical in roadmap.md: safety/correctness blockers first, remaining
phase work next, measured playback polish in backlog. Startup policy can be decided
without delaying unrelated blocker fixes. No parallel tracks were requested.

## Runtime regression audit — 2026-10-04

Finding #17's owning regressions now cover the identified gaps:

| Failure mode | Automated evidence |
|---|---|
| Concurrent Settings publication / in-flight consistency | `tts_host_config_snapshot` |
| Stop/seek at the paused predicate-to-wait boundary | `tts_host_playback_controller` |
| Native Windows listener collision / continued original service | `tts_host_local_api` |
| Concurrent Windows runner handle isolation / independent EOF | `tts_host_runner_inheritance` |
| Runner death, partial PCM then death, control/audio/load hangs, blocked write, slow control bytes, closed input, stuck exit, repeated failure, cancellation without retry | `tts_host_runner_recovery` |
| Seek during blocked lookahead, paused seek, Stop joining workers, repeated runner failure during playback | `tts_host_synthesis_pipeline` (production coordinator, fake endpoint, real child process) |

The same recovery fixture verifies restart/reload, exactly one failed-chunk
retry, and complete PCM publication without duplicating partial output. Limits
are total operation deadlines rather than timeouts refreshed by each byte.
Current defaults allow 30 s control, 10 min load/synthesis, and 1 s unload/exit;
short injected limits make hang regressions deterministic. Linux real Kokoro
initialize/load measured 0.118/2.088 s. Full synthesis deadline tuning for future
models remains part of performance acceptance: Linux lacks espeak-ng and native
Windows still resolves ONNX 1.17.1 rather than API 29.

Verification: 44/44 Linux CTests; Windows Release Host and new harnesses build,
41/43 native CTests pass. The two pre-existing ONNX mismatch failures remain.
Hardware-independent tests do not complete audible playback, native browser,
or Settings-to-Now-Playing acceptance. No developer startup registration changed.
