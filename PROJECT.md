# Project instructions

## Local Windows development

- Do not enable TTS Host launch at Windows login on this developer machine or
  perform logout/login acceptance here.
- For local desktop launches, use `config.development.json` with
  `startup.launchOnLogin` set to `false`; the shipped example currently enables
  it. `compile-win.ps1` creates that local config and disables startup before
  launch. Keep the application's configurable startup feature available to users.
- Verify startup integration in an isolated test environment rather than
  changing this machine's login startup settings.
