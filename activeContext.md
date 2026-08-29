# Active Context

- Mode: session closed
- Phase/slice: first audio through the runner protocol — every sub-bullet
  except the audio-pipe deadlock fix is checked. This session did the first
  native-Windows verification attempt of the already-shipped sub-slices
  (espeak-ng subprocess, MSI vendoring, phoneme mapping) and found/fixed
  three build/test bugs; not yet re-verified.
- State: `compile-win.ps1` failed configuring because 7-Zip (already
  installed) wasn't on PATH — added a fallback to its default install
  directory. After that, 2/15 CTests failed on Windows only:
  `tts_host_kokoro_phoneme_mapping` (MSVC was encoding `\uXXXX` narrow-string
  literals — the IPA phoneme constants — with the locale execution charset
  instead of UTF-8, since no target had `/utf-8`; fixed with a global MSVC
  `/utf-8` compile option) and `tts_host_espeak_phonemizer` (Windows'
  `CreateProcessW` failure path didn't say "exited with code", unlike the
  POSIX path the test expects; made it throw exit code 127 too, matching
  shell "command not found" convention). All three fixes are uncommitted
  (`CMakeLists.txt`, `compile-win.ps1`, `src/espeak_phonemizer.cpp`) and not
  yet re-verified — the user has not reported a rebuild result since the
  last fix.
- Next atomic step: re-run `compile-win.ps1` to confirm 15/15 CTests pass on
  Windows with these three fixes. If green, resume the actual next atomic
  step from before Windows verification started: fix the host/runner
  audio-pipe deadlock — `kokoro_runner_main.cpp` writes the audio frame
  before its control response, but `main.cpp`'s `synthesize_to_wav` calls
  `send_request` (blocks for the control response) before
  `read_audio_stream_until_end` (drains the audio pipe), so any synthesis
  whose audio exceeds the OS pipe buffer (~64KB — any real sentence past a
  couple of words) hangs both processes forever. Likely fix: read the audio
  pipe concurrently with (or interleaved with) the control response, not
  strictly after it.
- Next-after-next: wire `normalize_html` into an actual entry point once an
  HTML-vs-Markdown input signal exists (likely an HTTP API content-type, not
  yet built).
- Blockers/environment: awaiting Windows rebuild confirmation for the three
  fixes above. The pipe-deadlock bug still blocks realistic manual
  end-to-end testing against real weights — only very short (1-2 word)
  requests currently succeed. The worktree is intentionally uncommitted and
  now holds multiple unrelated slices plus these Windows fixes — commit them
  separately, since they touch different files (only `CMakeLists.txt` is
  shared). `models/kokoro-en-v1/` (~330MB, gitignored) must be re-fetched in
  a fresh checkout via `tools/fetch_kokoro_weights.py`.
- Open questions: UI toolkit and its licensing; Qwen PyTorch versus llama.cpp
  and quantization; WSL headless distribution; per-engine runner directory
  isolation (`runners/<engine>/` instead of flat beside `tts-host`) — still
  unconfirmed, and it also governs the `docs/design/architecture.md`
  Distribution-tree conflict noted there.
- Discarded as noise: normalizing markdown inside each client rather than the
  host (the design puts it in the host so every surface benefits); blanket
  stripping of `*`/`_` (mangles `snake_case` and arithmetic — the normalizer
  drops them only at word boundaries, pinned by tests); emitting SSML or any
  pause markup from the normalizer (no consumer exists; heading pauses are
  block boundaries for the not-yet-built split stage).
