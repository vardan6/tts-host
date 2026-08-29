# Active Context

- Mode: session closed
- Phase/slice: "First audio through the runner protocol" is fully checked off.
  The host/runner audio-pipe deadlock is fixed: both runners now write their
  control response before the audio frame, so the host is already draining
  the audio pipe by the time a large frame arrives.
- State: 15/15 CTests pass on Linux. Verified end to end against real
  espeak-ng and real Kokoro-82M weights with a long sentence (342,600 sample
  frames, 685KB WAV) — no hang, past the old ~64KB threshold.
- Next atomic step: the next unchecked roadmap item is "HITL — English model
  bake-off" (RTX 3070 GPU comparison) — needs the user, not an agent. The next
  AFK-only work is under "Everyday desktop playback" (tray controls, host-side
  playback, chunked streaming, interrupt/queue semantics, output-device
  selection, start-at-login) — not yet broken into sub-bullets; run
  `/next-slice` to pick one.
- Next-after-next: wire `normalize_html` into an actual entry point once an
  HTML-vs-Markdown input signal exists (likely an HTTP API content-type, not
  yet built).
- Blockers/environment: three Windows-native build fixes (`CMakeLists.txt`
  `/utf-8` MSVC flag, `compile-win.ps1` 7-Zip PATH fallback,
  `src/espeak_phonemizer.cpp` exit-127 message) are uncommitted and awaiting a
  Windows rebuild to confirm 15/15 CTests — the user has not reported a
  rebuild result yet. `models/kokoro-en-v1/` (~330MB, gitignored) must be
  re-fetched in a fresh checkout via `tools/fetch_kokoro_weights.py`.
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
  block boundaries for the not-yet-built split stage); reading the audio pipe
  concurrently with the control response as the deadlock fix (writing the
  control response first is simpler and sufficient since the two channels are
  already separate pipes).
