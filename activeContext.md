# Active Context

- Mode: session closed
- Phase/slice: first audio through the runner protocol — every sub-bullet of
  this roadmap item is now checked (espeak-ng subprocess, Windows MSI
  vendoring, real Kokoro-82M weights, and the Misaki IPA-to-Kokoro mapping),
  but the item itself stays unchecked: a newly discovered audio-pipe deadlock
  means realistic synthesis requests still hang. CLI stdin/clipboard support
  (Everyday desktop playback sub-item) also shipped this cycle. Six slices
  have now run in this line of work; all complete, all uncommitted.
- State: espeak-ng subprocess capture, Windows MSI vendoring, markdown/HTML
  normalization, CLI stdin/clipboard input, and the Misaki phoneme mapping are
  all complete and unit-tested (`kokoro_phoneme_mapping.hpp/cpp`, ported from
  hexgrad/misaki's espeak.py, validated against a real espeak-ng binary before
  translation to C++). Verified end to end with real espeak-ng and real
  Kokoro-82M weights: distinct input texts now produce distinct, non-silent
  audio, where every request previously produced identical audio regardless
  of text. All CTests pass on Linux/WSL.
- Next atomic step: fix the host/runner audio-pipe deadlock found while
  verifying the phoneme-mapping slice — `kokoro_runner_main.cpp` writes the
  audio frame before its control response, but `main.cpp`'s
  `synthesize_to_wav` calls `send_request` (blocks for the control response)
  before `read_audio_stream_until_end` (drains the audio pipe), so any
  synthesis whose audio exceeds the OS pipe buffer (~64KB — any real sentence
  past a couple of words) hangs both processes forever. The old hardcoded
  5-phoneme "hello" fixture (64,800 bytes) happened to just barely fit under
  that limit, which is why this was never hit before. Likely fix: read the
  audio pipe concurrently with (or interleaved with) the control response,
  not strictly after it.
- Next-after-next: wire `normalize_html` into an actual entry point once an
  HTML-vs-Markdown input signal exists (likely an HTTP API content-type, not
  yet built). Windows-native verification is still needed for the espeak-ng
  subprocess and MSI-vendoring slices.
- Blockers/environment: the pipe-deadlock bug above blocks realistic manual
  end-to-end testing against real weights — only very short (1-2 word)
  requests currently succeed. Windows-native verification is still needed for
  multiple slices; everything this session was verified on Linux/WSL only
  (espeak-ng here was extracted from a downloaded `.deb` rather than
  system-installed — fine for dev, matches ADR 0006's Linux story). The
  worktree is intentionally uncommitted and now holds multiple slices — commit
  them separately, since they touch different files (only `CMakeLists.txt` is
  shared). `models/kokoro-en-v1/` (~330MB, gitignored) must be re-fetched in a
  fresh checkout via `tools/fetch_kokoro_weights.py`.
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
