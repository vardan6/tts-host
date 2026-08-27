# Active Context

- Mode: planning complete; implementation not started
- Phase/slice: architecture acceptance before portable discovery-to-audio slice
- State: product requirements, proposed C++ host architecture, and roadmap are captured
- Next atomic step: review and accept the C++ host, UI toolkit, and runner boundary
- Next after that: use `/next-slice` to define the first implementation change
- Blockers/environment: license is undecided, so the repository stays private
- Open questions: Qt licensing/toolkit choice; runner protocol; Qwen llama.cpp versus packaged PyTorch; portable data locations; timing of remote providers
- Discarded as noise: WSL-only deployment; requiring end-user Python; treating Gemma audio-input models as TTS; forcing every model runner to be C++
