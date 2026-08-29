# Go-to Windows build script: clean rebuild from scratch, then build and test
# everything currently implemented (tts-host, every runner, and the CTest
# suite). Deleting build/ every run is deliberate, not laziness — see
# README.md "Don't mix WSL and native Windows for the same build/ directory":
# a stale cache from a different generator/environment fails in confusing
# ways, and a full wipe is the only fix that's actually reliable.

$ErrorActionPreference = "Stop"

# cmake shells out to 7z to extract the vendored espeak-ng.msi (see
# docs/adr/0006-espeak-ng-vendoring-and-phoneme-mapping.md). 7-Zip's installer
# doesn't add itself to PATH, so fall back to its default install location.
if (-not (Get-Command 7z -ErrorAction SilentlyContinue)) {
    $sevenZipDir = "C:\Program Files\7-Zip"
    if (Test-Path (Join-Path $sevenZipDir "7z.exe")) {
        $env:PATH = "$env:PATH;$sevenZipDir"
    } else {
        Write-Error "7-Zip (7z) was not found on PATH or at '$sevenZipDir'. Install it (winget install -e --id 7zip.7zip) and retry."
    }
}

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
