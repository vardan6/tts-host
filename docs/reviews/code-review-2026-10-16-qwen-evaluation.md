# Code review — Qwen evaluation integration (2026-10-16)

- Range reviewed: `5e03455..5af176b` (`main..feature/bundled-models`)
- Docs consulted: `docs/adr/0002-runner-protocol.md`,
  `docs/design/architecture.md` (engines and models, model packages and
  discovery, model installation and updates, distribution),
  `docs/requirements/product.md` (distribution and usability), `roadmap.md`
- Reviewer: Claude Opus 5.5 (Anthropic); implementer Codex (OpenAI) per `progress.md`
- Mode: static review; no build or test run

| # | Finding | Evidence (file:line) | Severity | Risk | Value | Effort |
|---|---------|----------------------|----------|------|-------|--------|
| 1 | Speed ≠ 1.0 does not "fail explicitly" as designed. The runner's own exception terminates the process, the Host sees a closed channel, restarts the runner, reloads the GPU model, retries, crashes again, and reports a generic installation/memory error. Any user speed setting other than 1.0 with Qwen selected therefore costs two model loads per read and gives a misleading message. | `src/qwen_runner_main.cpp:87-88`, `src/qwen_runner_main.cpp:220-222`, `src/recovering_runner.cpp:59-68`, `docs/design/architecture.md` ("other speeds fail explicitly") | med | med | high | low |
| 2 | Same mechanism for other deterministic request errors: over-long text (`step_prompt < 0`, 1024-frame cap) and missing end-of-speech are retried after a full restart and reload, although a retry cannot succeed. The actionable runner text ("shorten the text") reaches only stderr, which a GUI-launched Host routes to `NUL`. | `src/qwen_runner_main.cpp:110`, `src/qwen_runner_main.cpp:116`, `src/qwen_runner_main.cpp:152`, `src/recovering_runner.cpp:63-65` | med | med | med | med |
| 3 | The runner always synthesizes with `package.languages.front()`. A manifest declaring `["en","ru"]` is selectable for Russian text, but the runner speaks it with the `en` tag and gives no error. The packaging tool writes one language only, but the registry does not reject multi-language `qwen3-gguf` manifests. | `src/qwen_runner_main.cpp:48-50`, `src/model_registry.cpp:134-149`, `include/tts_host/runner_protocol.hpp:74-78` | low | low | med | low |
| 4 | When the audio reader fails first, it sets `abort_io_`. The control reader then throws "runner synthesis I/O interrupted", and that `control_error` is rethrown in preference to the real `audio_error`, so the logged cause is lost. | `src/runner_launcher.cpp:487-499`, `src/runner_launcher.cpp:464` | low | low | med | low |
| 5 | The opt-in integration test claims to prove "incremental output", but `time_to_first_chunk_ms < elapsed` is also true for a whole-sentence runner. It also does not cover the speed ≠ 1.0 path, an over-long prompt, or runtime-missing/CPU-only failure. | `tests/qwen_integration_tests.cpp:30-34` | enhancement | low | med | low |
| 6 | Sampling parameters are hard-coded twice (sampler chain and `mtmd_helper_gen_audio_inp`), and the seed is fixed at 42. The bake-off needs to vary or record these, so they should come from a single source and appear in the stats/log output. | `src/qwen_runner_main.cpp:96-103` | enhancement | low | med | low |
| 7 | The runtime directory is an absolute developer path compiled into the binary, and `SetDefaultDllDirectories` changes the search policy for the whole process. This is acceptable for the evaluation runner, but the pending release-layout decision must replace it with an executable-relative path. It is worth listing explicitly as an input to that decision. | `CMakeLists.txt:227-229`, `src/qwen_api.hpp:22-28`, `src/qwen_runner_main.cpp:22` | enhancement | low | med | low |

## Details

**#1 / #2 — deterministic request errors.** ADR 0002 has no error reply for
`synthesize`; every runner reports failure by exiting, and `RecoveringRunner`
treats every `RunnerLaunchError` as transient. Kokoro had no deterministic
request rejections, so this was not visible before. Qwen adds several, and
speed is user-controlled (`audio.speechSpeed`, the Playback controller).
Suggested fixes, in order of preference:
- Check the capability in the Host: advertise supported speeds or a
  `fixedSpeed` capability in `initialize` and reject (or clamp, with an activity
  message) before sending. This needs an ADR 0002 amendment.
- Or add a protocol-level request error reply that `RecoveringRunner` surfaces
  without restarting. This is also an ADR 0002 change.
- At minimum: record the runner's stderr reason in the activity log, and make
  the design text match actual behavior.

The speed-control roadmap slice may make #1 moot for speed, but #2 remains.

**#3.** Either reject `qwen3-gguf` manifests with more than one language until
per-request language is supported, or pass the Host-selected language in
`synthesize`.

**#4.** When both errors are set, prefer `audio_error`, or rethrow
`control_error` only if `audio_error` is null.

**#5.** To verify incrementality, record the timestamp of the first audio frame
on the Host side (in the reader thread) and compare it with the control-reply
time. Alternatively, assert that more than one non-empty PCM frame arrived.

Checked with no finding: concurrent drain of both pipes (thread join before
`pending_audio_` is read, `abort_io_` atomic, cancellation polled on both
Windows and POSIX reads); discarding partial audio before retry (covered by
`audio-first-partial-once`); `vramMeasurement` parsing and display; GGUF magic
and containment validation in the registry; and the voice-path match in the
runner.

## Triage — 2026-10-16

Must fix now (2): #1, #2. The user asked for the accepted findings to be fixed, so #3–#5 were fixed in the same pass.

| # | Finding | Risk | Value | Effort | Priority | Reason |
|---|---------|------|-------|--------|----------|--------|
| 1 | Speed ≠ 1.0 crashes runner → restart + reload + misleading error | med | high | low | must_fix_now | breaks the documented "fail explicitly" contract; speed is user-controlled |
| 2 | Deterministic request errors retried with a reload; reason hidden | med | med | med | must_fix_now | same mechanism as #1, fixed together |
| 3 | Multi-language Qwen manifest speaks Russian with the `en` tag | low | med | low | should_fix_before_phase_complete | silent wrong output; trivial registry check (fixed) |
| 4 | Audio-reader error masked by "I/O interrupted" | low | med | low | should_fix_before_phase_complete | diagnostics; first failure now reported (fixed) |
| 5 | Integration-test gaps | low | med | low | should_fix_before_phase_complete | rejection path now covered in CTest and the GPU test; incrementality assertion left as is |
| 6 | Sampling params duplicated, fixed seed | low | low | low | backlog | value was for the bake-off |
| 7 | Absolute runtime path compiled into the runner | low | med | low | backlog | input to the release-layout decision (roadmap) |

Resolution:
- #1/#2: added a synthesize rejection reply (ADR 0002). The runner sends an
  end-of-stream frame, then a JSON-RPC `error`. `parse_runner_synthesize_response`
  throws `RunnerRequestError`, and `RecoveringRunner` consumes the stream before
  parsing, so the session stays loaded and is not retried. The Qwen runner
  rejects unsupported speed, prompt overflow, generation limit, and empty output
  this way. Covered by the `reject` mode of `tts_host_runner_recovery` and a
  protocol round-trip test.
- #3: the registry requires exactly one `en`/`ru` language for `qwen3-gguf`.
  Covered in `tts_host_model_session`.
- #4: `send_request` rethrows whichever reader failed first.
- #5: the real-GPU integration test checks speed 1.5 rejection without a
  restart, then a successful next sentence.

Verification: Linux Debug build clean; 47/47 CTests pass; the real-GPU
`tts-host-qwen-integration-tests` passes on Linux (RTX 3070 Laptop GPU, CUDA
libraries via `LD_LIBRARY_PATH`). Windows was not rebuilt or tested in this pass.
