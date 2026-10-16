# Windows acceptance — 2026-10-16

Agent-observed partial native acceptance on the developer Windows machine using the existing
`build/Debug/tts-host.exe` (October 14 build), development configuration, and
pre-created startup-choice marker. No build or automated test suite was run.
Login startup registration was not changed. Original development configuration
was restored after temporary tests; the Host remains running.

## Boundary gap — completed guided run

User confirmed audio ready and, after the probe, reported no audible gaps,
clicks, or interruptions, including the final shared-stream sequence.
Command: `build/Debug/tts-host-playback-gap-probe.exe 20`.

| Measurement | Result |
|---|---|
| Source / loopback mix | 24,000 Hz mono / 48,000 Hz |
| Control, inserted 20 ms gaps | 20/20 detected; min/median/p95/max 23 ms |
| Separate playback calls | 20/20 detected; min 63, median 73, p95/max 83 ms |
| Per-call wall overhead | min 30.2442, median 40.1504, p95 48.1266, max 49.2168 ms |
| Shared utterance stream | 0 detected gaps at the 4 ms reporting threshold |
| Loopback discontinuities | 5 across all conditions |

Zero detected gaps is threshold-limited, not proof of zero silence. Preserve the
discontinuity count as a measurement caveat.

## Login startup — user-confirmed complete

On October 16 the user explicitly requested that login startup acceptance be
considered done. The roadmap item is closed on that confirmation. The agent did
not run the isolated-machine tests; environment and individual test results were
not supplied. Developer-machine startup registration was left unchanged.

## Desktop evidence

- Development config has `startup.launchOnLogin: false`; launch used the marker.
- General, Playback, and Models tabs rendered without control overlap. Application
  title-bar icon was visible; tray icon visual acceptance remains unverified.
- Temporarily used `automatic` capture and the installed Kokoro `fast` profile.
  A dedicated OneNote page named **TTS acceptance test October 16** remains in
  the notebook. Selected title text was captured through `Ctrl+F8` using UI
  Automation TextPattern. Now Playing **Read selection** then used the saved
  OneNote target while the Host was foreground; playback reached Playing.
- Collapsing the OneNote title selection and invoking `Ctrl+F8` produced explicit
  empty-selection feedback with manual-copy recovery advice; no new read began.
- No synthetic Copy adapters are enabled in the current implementation;
  compatibility remains separately pending. Clipboard-format preservation was
  not measured in this run.
- Settings saved a six-second seek interval to the config. Now Playing remained
  at five seconds while Settings was open and changed to six after it closed.
- An external development-config edit to seven seconds appeared as ±7 seconds
  in Now Playing without restarting the Host.
- Long test text submitted through local `POST /v1/selection` enabled playback
  controls. Pause held position at 10.6/14.5 seconds elapsed/generated. Backward
  seek reached 3.6 seconds and retained Paused; forward seek returned to 10.6
  seconds, still Paused. Backward/forward buttons disabled when their seven-second
  targets were outside the available source interval. Resume advanced playback.
  Stop returned to Idle with 0.0/0.0 seconds and disabled controls.
  User confirmed pause, resume after seeking, and Stop all sounded correct.
- User confirmed the tray **Global selection shortcut** toggle works after the
  checked/active and unchecked/suspended test was explained. Ctrl+F8 remains the
  chosen shortcut. Settings agreement across suspend/resume was not separately
  confirmed.
- Closing Now Playing removed the visible window while the Host process remained
  running. User subsequently confirmed both hiding and reopening from the tray.
- User considers Terminal/Codex, embedded-terminal, OneNote, and browser reading
  checks complete for the copy-first workflow: manually copy, then Ctrl+F8.
  The restored development config uses `clipboardOnly`, which deliberately
  reads existing clipboard text without capturing or copying a selection.
  This does not establish direct selection capture in every app. Direct OneNote
  capture was separately verified with `automatic` during this run.
- Acceptance clarification: clipboard-only reading is complete; repeating it
  across source apps adds no selection-capture evidence. No selection in a
  source app is irrelevant to this policy: existing clipboard text can still be
  read. Empty/non-text clipboard handling is distinct and was not explicitly
  tested here. OneNote selected/empty direct capture and saved-target reading
  are already complete; no repeat OneNote run is needed. Other-app direct
  capture under `automatic` remains separate from clipboard-only acceptance.
- Catalogue displayed HAVE Kokoro-82M (310.9 MB); no download was performed.

## Findings and gaps at the time of agent testing

The final user-confirmed closure below supersedes the pending status in this
section and the agent-only evidence above; those observations remain historical.

1. Now Playing clips the long empty-selection diagnostic and its recovery advice
   at the bottom of the taller window. Source and target labels also truncate.
2. Seek interval changes become visible after Settings closes; immediate
   Settings-to-Now-Playing propagation while Settings stays open was not observed.
3. Settings reports an existing startup entry pointing to another copy. It was
   left untouched by the agent. Login startup acceptance was subsequently closed
   on the user's confirmation above.
4. Direct Terminal/Codex/embedded-terminal selection requires manual participation
   under the computer-use skill; user confirmed copy-first reading across apps.
   Browser automation was stopped by the tool because it
   could not determine the browser URL confidently enough to enforce policy;
   direct browser selection, article menu, control page, reconnect, and
   close-without-stop remain unverified. No browser policy was bypassed.
5. Pending: queued-read clearing, tray icons, shortcut Settings
   agreement, live catalogue download/Activity progress, and Server tab visuals.
   User confirmed audible playback controls and the tray shortcut toggle, and
   chose to keep Ctrl+F8. First-run acceptance is included in the user-confirmed
   login startup closure above.

## Final closure — all checks accepted

On October 16 the user checked every item in the interactive checklist and
explicitly requested “please accept all as done”. All initial desktop, login
startup, and audible-gap acceptance items are therefore closed on user
confirmation. This includes the previously pending direct/empty-selection,
queue-clearing, shortcut/Settings agreement, visual, download/Activity, and
browser-extension checks. The clipping and delayed skip-update findings are
accepted for this closure; no code fixes or additional agent verification are
claimed. Ctrl+F8 remains the chosen shortcut. Synthetic Copy adapters remain
disabled; their separate technical compatibility work is not completed by this
acceptance confirmation.
