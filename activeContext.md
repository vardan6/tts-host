# Active Context

- Mode: session closed
- Phase/slice: first audio through the runner protocol — real Kokoro-82M
  weights are now wired in and produce real, non-silent audio end to end; the
  only remaining sub-slice is espeak-ng vendoring so arbitrary text is
  actually phonemized (right now every synthesize request produces a fixed
  hardcoded "hello"-like utterance regardless of the requested text)
- State: recorded ADR 0005 (espeak-ng runs as an isolated subprocess, never
  linked into any Apache-2.0 binary, so its GPL-3.0 obligations stay
  contained to that one component — vendoring itself is still unimplemented).
  Added `tools/fetch_kokoro_weights.py` (stdlib-only) which downloads the
  real, full-precision Kokoro-82M ONNX model and the `af_heart` voice
  embedding from `onnx-community/Kokoro-82M-v1.0-ONNX` (Apache-2.0) into
  gitignored `models/kokoro-en-v1/` plus a generated `model.json`. Extended
  the `load` request with an optional `voicePath`
  (`runner_protocol.hpp/cpp`, `make_runner_load_request`, `main.cpp`'s
  `RunnerSelection`/`resolve_runner_selection` resolving `files.voice`).
  `KokoroOnnxRunner::run_synthesis` now branches on the loaded ONNX graph's
  input count: 1 input keeps running the placeholder identity model
  (existing CTest fixture, untouched); 3 inputs run the real Kokoro
  contract, with input/output tensor names resolved dynamically from the
  graph rather than hardcoded (`input_ids`/`tokens` naming varies by export
  variant). The voice file is a per-phoneme-count style table (one 256-float
  row per possible phoneme count, ~510 rows); row `n-1` is selected for an
  n-phoneme utterance, matching `kokoro-onnx`'s `_style_for`. `style`'s
  actual tensor rank (2, i.e. `[1,256]`) was discovered empirically via one
  ONNX Runtime rank-mismatch error — not documented anywhere upstream, so
  worth remembering if it's revisited. Verified manually: `--model
  kokoro-en-v1 --synthesize "hello" --config config.example.json` produced
  32400 real, non-silent PCM sample frames. 11/11 CTest still pass (Linux
  only this session; the placeholder-model path is unchanged so this should
  hold on Windows too, but that's unverified this session).
- Next atomic step: vendor espeak-ng as an isolated subprocess and wire real
  text-to-phoneme conversion into `run_kokoro_synthesis`, replacing the
  hardcoded phoneme sequence — this is the last sub-slice of "First audio
  through the runner protocol". Mechanics are now decided, not just the
  licensing isolation: ADR 0005 (isolated subprocess) plus ADR 0006 (Windows
  vendors via `FetchContent` + 7-Zip-extracted `espeak-ng.msi`, verified
  against the real installer; Linux relies on a system package for now;
  phoneme mapping is ported from `hexgrad/misaki`, Apache-2.0). Roadmap's
  espeak-ng item is now split into the three sub-bullets ADR 0006 governs.
- Next-after-next: once arbitrary text produces real speech end to end, close
  out the roadmap item and move to the English model bake-off (Kokoro vs.
  Qwen3-TTS) or the everyday-desktop-playback slice (hotkey, selection
  capture, host-side playback) — the user's actual near-term goal (select
  text, hotkey, hear it read aloud) needs both that slice and the Windows
  selection-reading HITL slice, neither started yet.
- Blockers/environment: five+ sessions of uncommitted work now sit in the
  tree — consider committing in logical chunks rather than one giant commit.
  Windows-native build of this session's changes is untested (WSL only).
  `models/kokoro-en-v1/` (real ~330MB weights + voice) now exists locally,
  gitignored, fetched via `tools/fetch_kokoro_weights.py` — not something a
  fresh checkout will have without re-running that script. This session's new
  CMake `list-models`/`package` targets and the rewritten `compile-win.ps1`
  are verified on Linux only; Windows — the actual target of
  `compile-win.ps1` — is unverified this session.
- Open questions: UI toolkit/licensing; Qwen PyTorch versus llama.cpp and
  quantization; WSL headless distribution; per-engine runner directory
  isolation (proposed — each runner's private runtime deps in their own
  `runners/<engine>/` subdirectory instead of flat beside `tts-host` — raised
  in conversation but not yet confirmed; affects `main.cpp`'s
  `runner_path_for_engine()`, `CMakeLists.txt`'s runtime-copy destinations,
  and the new `package` CPack target, and would also resolve the
  `docs/design/architecture.md` Distribution-tree conflict noted there).
- Discarded as noise: collapsing audio into JSON-RPC; using stderr for audio;
  concurrent synthesis requests in protocol v1; end-user Python; WSL-only
  deployment; a true SCM-registered Windows Service (per ADR 0001 —
  "reliable service" language from the user meant a robust per-user autostart
  process, not literal Windows Service registration, which would lose
  audio/tray access); token auth and cloud providers for v1; `cmake --build
  --target clean` as a routine pre-build step (it doesn't fix the WSL/native
  cache-mismatch problem, only `rmdir build/` does); putting engine→runner
  mapping in `config.json` (rejected in favor of a fixed naming convention,
  `tts-host-<engine>-runner`); a full GitHub Actions CI workflow now (built
  and then deliberately reverted — see roadmap.md Deliberately deferred);
  running `pip install` directly in an agent session (user wants dependency
  installation scripted and run manually in their own terminal); linking
  espeak-ng into any Apache-2.0 binary (ADR 0005 — isolated subprocess
  instead); designing a full multi-engine `voices` manifest schema now (the
  existing `files` object already accepts arbitrary string keys like
  `"voice"` without a schema change, so this was deferred rather than
  needed); a formal English-model bake-off before shipping any real audio
  (user asked to just pick a model now — chose Kokoro-82M full precision, the
  already-architected default, rather than re-opening the Kokoro-vs-Qwen
  question this session); extracting the vendored `espeak-ng.msi` via
  `msiexec /a` (needs Windows and the Installer service; 7-Zip reads the
  embedded CAB directly with no such dependency — see ADR 0006); running
  `list-models` automatically at the end of `compile-win.ps1` (moved to an
  opt-in target so the main script stays a clean/configure/build/test loop).
