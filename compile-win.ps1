# Go-to Windows build script: clean rebuild from scratch, then build and test
# everything currently implemented (tts-host, every runner, and the CTest
# suite). Deleting build/ every run is deliberate, not laziness — see
# README.md "Don't mix WSL and native Windows for the same build/ directory":
# a stale cache from a different generator/environment fails in confusing
# ways, and a full wipe is the only fix that's actually reliable.

$ErrorActionPreference = "Stop"

Write-Host "==> Cleaning previous build/" -ForegroundColor Cyan
if (Test-Path build) {
    Remove-Item -Recurse -Force build
}

Write-Host "==> Configuring (Visual Studio 17 2022)" -ForegroundColor Cyan
cmake -S . -B build -G "Visual Studio 17 2022"

Write-Host "==> Building everything (Debug)" -ForegroundColor Cyan
cmake --build build --config Debug --parallel

Write-Host "==> Running tests" -ForegroundColor Cyan
ctest --test-dir build -C Debug --output-on-failure

Write-Host "==> Done" -ForegroundColor Green
Write-Host ""
Write-Host "Other useful targets (see README.md ""Targets""):"
Write-Host "  cmake --build build --config Debug --target list-models   (fast manual run, no full rebuild)"
Write-Host "  cmake --build build --config Debug --target package       (build the installer zip)"
