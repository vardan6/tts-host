# Reconfigure the Windows build tree, build every target, and run CTest.
param(
    [switch]$ResetDependencyCache
)

$ErrorActionPreference = "Stop"
Set-Location $PSScriptRoot

$fetchContentDir = Join-Path $PSScriptRoot "cache\fetchcontent\windows-vs2022"

if ($ResetDependencyCache -and (Test-Path $fetchContentDir)) {
    Remove-Item -Recurse -Force $fetchContentDir
}

if (Test-Path "build") {
    Remove-Item -Recurse -Force "build"
}

cmake -S . -B build -G "Visual Studio 17 2022" "-DFETCHCONTENT_BASE_DIR=$fetchContentDir"
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

cmake --build build --config Debug --parallel
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

ctest --test-dir build -C Debug --output-on-failure
exit $LASTEXITCODE
