# Active Context

- Mode: planning captured; implementation not started
- Phase/slice: first roadmap slice — host starts and reads its configuration
- State: architecture accepted and recorded in ADRs 0001-0004; requirements,
  architecture, and roadmap rewritten to match; Apache-2.0 `LICENSE` added
- Next atomic step: use `/next-slice` to scope the headless host startup and
  `config.json` load/validate/reload path
- Blockers/environment: none blocking. Repository is still private; the licence
  decision that kept it private is now settled, so making it public is a
  pending action, not a pending decision
- Open questions: UI toolkit and its licensing (decided at the model-manager
  slice); llama.cpp versus PyTorch and Qwen quantization (outputs of the
  bake-off); whether a WSL headless distribution is published
- Discarded as noise: WSL-only deployment; end-user Python; treating roadmap
  product slices as one-session implementation slices; assuming JSON-RPC framing
  solves binary audio; forcing every runner to be C++; a real Windows Service
  (Session 0 cannot reach audio or the tray); token auth for v1; cloud providers
  for v1; a shell context menu and a floating widget in this project
