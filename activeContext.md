# Active Context

- Mode: session closed
- Phase/slice: first audio through the runner protocol — stub synthesis
- State: JSON-RPC control framing, typed `initialize` and `synthesize`, the
  `pcm_s16le` payload contract, framed audio codec, and deterministic stub audio
  through the inherited pipe are implemented and covered by native CTest.
  Windows native build is now verified end to end (VS 2022 Build Tools
  installed, `cmake -G "Visual Studio 17 2022"` configure/build/ctest all
  succeed). Fixed a `tts_host_list_models` CTest failure on Windows caused by
  an ordered-needle path using forward slashes against native backslash
  output (`CMakeLists.txt`). Cleaned up README: removed a triple-repeated
  `cmake` configure command, and added missing/duplicated Windows command
  blocks under "Try the host".
- Next atomic step: launch the stub runner from the host and add a CI end-to-end
  synthesis test.
- Next-after-next: write the synthesized PCM stream as a WAV response.
- Blockers/environment: none — Windows native build unblocked. Worktree is
  uncommitted (large amount of untracked implementation: CMakeLists.txt,
  src/, tests/, README.md, plus this session's fix and doc edits).
- Open questions: UI toolkit/licensing; Qwen PyTorch versus llama.cpp and
  quantization; WSL headless distribution.
- Discarded as noise: collapsing audio into JSON-RPC; using stderr for audio;
  concurrent synthesis requests in protocol v1; end-user Python; WSL-only
  deployment; real Windows Service; token auth and cloud providers for v1;
  `cmake --build --target clean` as a routine pre-build step (it doesn't fix
  the WSL/native cache-mismatch problem, only `rmdir build/` does).
