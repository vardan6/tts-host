# Code review — Windows desktop checkpoint

- **Range:** `1ad34c1..0a915c52bb8783427cb639d8e4e2074661bc6490`.
- **Reviewer:** Codex (GPT-6, OpenAI), same provider as the implementer.
  This is a user-requested local review, not an independent cross-review.
  No other agent or external agent CLI was launched for this review.
- **Date:** 2026-10-14.
- **Method:** incremental checkpoint review using the October 3 report as the
  baseline; inspect later runtime changes and independently check the paths
  involved in its correctness blockers. Focus: configuration snapshots and
  persistence, runner inheritance/recovery, model residency, stream/seek/speed
  coordination, startup choice/registration, Settings lifetime, Activity log,
  browser controls, and Windows development scripts.
- **Docs:** `docs/requirements/product.md`, relevant sections of
  `docs/design/architecture.md`, `docs/design/developer-workflow.md`,
  ADRs 0002/0007/0008, `docs/implementation-notes.md`, `PROJECT.md`,
  `roadmap.md`, and the October 3 review/triage.
- **Limits:** not an exhaustive fresh audit of all 170 branch files. Earlier
  normalization, model numerics, catalogue cryptography, dependency vendoring,
  and release packaging rely on the existing review/test record. Native
  audible/browser/login acceptance and performance measurements remain open.
  The earlier 46/46 Windows and Linux CTest runs do not prove those checks.

| # | Finding | Evidence (file:line) | Severity | Risk | Value | Effort |
|---|---------|----------------------|----------|------|-------|--------|
| 1 | Now Playing Stop cancels active playback but does not clear queued reads; the scheduler starts the next queued request immediately afterward. | `src/now_playing_window.cpp:97`, `src/playback_controller.cpp:128`, `src/main.cpp:285`, `src/main.cpp:307`, `src/main.cpp:364` | med | med | high | low |
| 2 | External JSON edits are not reloaded by the desktop Host, despite the live-reload design and the checked roadmap bootstrap item. Only Settings-close publication updates its configuration snapshot. | `src/main.cpp:294`, `src/main.cpp:340`, `src/main.cpp:473`, `src/config_snapshot.cpp:20` | med | med | high | med |
| 3 | Now Playing lacks the specified source/capture display and Read selection/Read clipboard controls. Current controls expose playback and speed only. The accepted visual prototype does not implement these remaining requirements. | `src/now_playing_window.cpp:132`, `docs/requirements/product.md:138`, `docs/design/architecture.md:519` | med | low | med | med |
| 4 | Download progress rebuilds and repaints the entire retained Activity log for every network read. It also consumes one of the 200 retained entries per read, rapidly evicting earlier actionable messages. Coalesce progress updates. | `src/catalogue_download.cpp:391`, `src/settings_window.cpp:414`, `src/settings_window.cpp:720`, `src/activity_log.cpp:14` | enhancement | low | med | low |

## Details

### Finding 1

Trigger: start a read, use Queue clipboard, then press Stop in Now Playing.
The button calls `PlaybackController::stop`, which only cancels its active
token. `pending_` belongs to `TraySpeechScheduler`; its worker subsequently
pops the next request. The browser's `playback_command("stop")` already clears
that queue under the scheduler mutex. Route the desktop button through the
same scheduler operation and regress the real UI command delegation.
Requirements explicitly say Stop clears queued requests; natural completion
alone should advance the queue.

### Finding 2

The desktop loads a document once; its worker repeatedly reads an immutable
in-memory snapshot. No file change check publishes a new snapshot. Editing
JSON externally therefore has no effect until restart. This is distinct from
the deliberately documented Settings-close publication boundary. Restore an
explicit unfinished roadmap slice for external file reload, including invalid
JSON retention and restart-only server settings. Do not expand this checkpoint
into all unfinished product behavior.

### Finding 3

The window's create/command paths contain state/position, seek, pause/Stop,
and speed controls, but no read actions or saved external target. The user
accepted its visuals; do not reopen that completed visual acceptance. Schedule
the missing behavior separately, preserving capture-failure non-interruption
and the prohibition on capturing Host controls or stealing focus for Copy.

### Finding 4

`WinHttpReadData` reads at most 64 KiB and invokes progress after every read.
Each notification records another entry, converts the entire retained snapshot,
replaces the edit text, scrolls, and repaints synchronously. A model download
can issue thousands of updates. Throttle or replace transient progress while
retaining failures and completion as separate events. No performance benchmark
was run for this observation; treat it as an enhancement, not a measured blocker.

## Initial disposition

Finding 1 blocks merge until fixed and regression-tested. Findings 2 and 3
are explicit unfinished product slices for continued work after the checkpoint;
finding 4 is backlog. No claim of full product acceptance or independent review
is made by this report.

## Triage — 2026-10-14

Must fix now: #1. The current UI and scheduler paths confirm it directly.

| # | Finding | Risk | Value | Effort | Priority | Reason |
|---|---------|------|-------|--------|----------|--------|
| 1 | Desktop Stop leaves queued reads | med | high | low | must_fix_now | Existing playback contract is broken; reuse the scheduler operation already used by browser Stop. |
| 2 | External config edits are not reloaded | med | high | med | should_fix_before_phase_complete | Known missing desktop behavior; restore explicit roadmap work instead of claiming full configuration acceptance. |
| 3 | Now Playing read actions/source are missing | low | med | med | should_fix_before_phase_complete | Accepted visuals are complete; remaining behavior needs a separate slice and capture safety checks. |
| 4 | Per-read progress rebuilds/evicts activity | low | med | low | backlog | Valid efficiency/usability improvement; no measured failure or checkpoint blocker. |

Findings 2–4 have explicit roadmap slices. Only #1 gates this development
checkpoint merge; native acceptance remains open rather than claimed passed.

## Resolution of finding 1

Desktop Now Playing now receives the Host's scheduler Stop callback through
the tray. Both browser and desktop Stop call `TraySpeechScheduler::stop_speech`,
which clears pending reads and cancels active playback under the scheduler
mutex. The window owns its callback and is destroyed before the controller
owner exits; singleton lookup is restricted to the current process.

The native Settings/tray lifecycle regression now drives the real Now Playing
Stop command with an injected queue-owner callback and verifies queued-work
clearing, active-token cancellation, close-to-hide without an extra Stop, and
window destruction on tray Quit. It does not play audio or change login startup.
The queue in this UI test is a fixture; the production scheduler's shared
Stop path is checked by source inspection. Native audible queue acceptance
remains part of the combined Windows walkthrough.

Validation after the fix: native Windows Debug and Linux Debug builds pass;
46/46 CTests pass on each platform, including the extended native UI regression.
Whitespace validation passes. No unresolved must-fix finding remains in this
focused checkpoint review; this is not a full-release acceptance result.

## Resolution of finding 2

The tray thread polls `config.json` once a second through
`ConfigFileWatcher` (`src/config_watch.cpp`). A changed file is read only
after the next poll sees it unchanged; content equal to the tray document
(Settings' own saves) is not a change. A valid edit replaces the tray
document and goes through the same publication as Settings close, and it
re-registers the selection hotkey if it changed. Invalid JSON, schema
violations, and a missing file keep the last valid document and are logged
once to Activity with the failing JSON path. Changed `/server/*` keys are
logged as needing a restart, because the listener is bound only at startup.
Polling is skipped while Settings is open, since Settings edits the same document.

`tts_host_config_watch` covers settling, same-content saves, partial writes,
invalid JSON, schema-invalid values, a missing file, and restart-only
reporting. The Win32 timer wiring was checked by native build only. Live
external-edit acceptance in the running tray remains part of the combined
Windows walkthrough. Validation: Linux Debug and native Windows Debug, 47/47
CTests each.

## Resolution of finding 3

Now Playing adds **Read selection** and **Read clipboard**, a label for the
saved external target, the active utterance's source, and the last capture
result. Each queued request carries a source label that `PlaybackController`
publishes when the utterance begins. A global-shortcut capture saves the
target window and its focused control's UI Automation RuntimeId, even when
the selection read fails. **Read selection** finds that control again under
the saved window and reads its TextPattern selection. It runs off the UI
thread on the existing single-worker deadline, never checks or changes focus,
and sends no input. A missing window or control, another process, a protected
field, an empty selection, or no saved target fails closed. The status line
then asks the user to reselect and use the global shortcut, logs the failure
to Activity, and leaves playback untouched. `clipboardOnly` makes
**Read selection** read the existing clipboard. Now Playing failures update
its status line instead of opening a message box.

Regressions: `tts_host_playback_controller` (source per utterance),
`tts_host_selection_capture` (target label, fail-closed saved capture,
failure guidance), and `tts_host_settings_lifetime`, which clicks the native
Read selection button with no saved target and checks that playback continues
and nothing is read. Reading a real external application's selection from
Now Playing was not exercised; it is part of the combined Windows
walkthrough. Validation: Linux Debug and native Windows Debug, 47/47 CTests
each.

## Resolution of finding 4

Settings download progress now goes through `DownloadProgressThrottle`
(`src/catalogue_download.cpp`). It passes the first update of each file and
the update that completes it, and otherwise at most one update every 250 ms.
Passed updates call `record_transient_activity`: while the newest Activity
entry is the same file's progress, it is replaced with a fresh sequence
instead of appended. Any other entry ends the replacement, so start,
completion, and failure messages stay separate. A download now uses one
retained entry per file, and Settings rebuilds and repaints the log only for
throttled updates.

Regressions: `tts_host_selection_capture` (1,000 progress updates occupy one
entry, sequence readers see a replacement, an intervening failure is not
replaced) and `tts_host_catalogue_download` (interval, new-file, and
completion rules). A live download in Settings was not exercised and no
performance was measured. Validation: Linux Debug and native Windows Debug,
47/47 CTests each.
