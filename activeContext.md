# Active Context

- Mode: session closed
- Phase/slice: first audio through the runner protocol — stub runner launch
  and WAV synthesis
- State: the host now launches a runner process end to end. `RunnerSession`
  (`include/tts_host/runner_launcher.hpp`, `src/runner_launcher.cpp`) spawns
  the runner (fork/exec on POSIX, `CreateProcess` on Windows), wires the
  Content-Length control channel over stdin/stdout, and creates the inherited
  audio pipe. The CLI gained `--synthesize <text> --out <path.wav> [--runner
  <path>]`, defaulting to the in-repo stub runner next to the executable.
  `wav_writer` writes a canonical PCM WAV. A CTest end-to-end test
  (`tests/assert_wav_synthesis.cmake`) drives the real stub runner subprocess
  and checks the WAV output. Fixed a latent stub-runner deadlock: buffered
  `std::cin.read` blocks until its full buffer fills when reading from a pipe,
  so it never saw the host's short writes; `src/stub_runner_main.cpp` now uses
  raw short reads/writes. All 7 native CTest tests pass (built and run on
  Linux this session; not yet re-verified on native Windows).
- Next atomic step: build the Kokoro runner (ONNX, bundled model) so the
  roadmap item moves from stub-only to a real voice; the runner protocol
  plumbing (control channel, audio pipe, launcher, WAV writer) is already in
  place and reusable.
- Next-after-next: run the CI pipeline itself (currently only proven via local
  CTest) so "drive the whole path from an end-to-end test against the stub in
  CI" is actually verified in CI, not just locally.
- Blockers/environment: none. The Windows `CreateProcess` path in
  `runner_launcher.cpp` is implemented but only reasoned through, not yet
  built/run on native Windows this session — verify there before trusting it.
- Open questions: UI toolkit/licensing; Qwen PyTorch versus llama.cpp and
  quantization; WSL headless distribution.
- Discarded as noise: collapsing audio into JSON-RPC; using stderr for audio;
  concurrent synthesis requests in protocol v1; end-user Python; WSL-only
  deployment; real Windows Service; token auth and cloud providers for v1;
  `cmake --build --target clean` as a routine pre-build step (it doesn't fix
  the WSL/native cache-mismatch problem, only `rmdir build/` does).
