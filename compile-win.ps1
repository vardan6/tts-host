# Incremental Windows build, test, and launch loop.
$ErrorActionPreference = "Stop"
Set-Location $PSScriptRoot

$fetchContentDir = Join-Path $PSScriptRoot "cache\fetchcontent\windows-vs2022"

if (-not (Test-Path "build/CMakeCache.txt")) {
    cmake -S . -B build -G "Visual Studio 17 2022" "-DFETCHCONTENT_BASE_DIR=$fetchContentDir"
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

cmake --build build --config Debug --parallel
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

ctest --test-dir build -C Debug --output-on-failure
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

.\build\Debug\tts-host.exe --config config.example.json
exit $LASTEXITCODE
