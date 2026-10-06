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

# Keep developer launches separate from the shipped defaults and user settings.
$developmentConfigPath = Join-Path $PSScriptRoot "config.development.json"
$configSourcePath = if (Test-Path $developmentConfigPath) { $developmentConfigPath } else { "config.example.json" }
$developmentConfig = Get-Content -Raw -Encoding UTF8 $configSourcePath | ConvertFrom-Json
if (-not $developmentConfig.PSObject.Properties["startup"]) {
    $developmentConfig | Add-Member -NotePropertyName startup -NotePropertyValue ([pscustomobject]@{})
}
$developmentConfig.startup | Add-Member -NotePropertyName launchOnLogin -NotePropertyValue $false -Force
$developmentConfigJson = $developmentConfig | ConvertTo-Json -Depth 100
[System.IO.File]::WriteAllText($developmentConfigPath, $developmentConfigJson, (New-Object System.Text.UTF8Encoding $false))

# Answer the first-run login-startup question in advance so development
# launches never ask or register (docs/design/architecture.md#login-startup).
$dataDir = if (Test-Path "build\Debug\portable.marker") { "build\Debug" } else { Join-Path $env:LOCALAPPDATA "TTS Host" }
$startupChoiceMarker = Join-Path $dataDir "startup-choice.marker"
if (-not (Test-Path $startupChoiceMarker)) {
    New-Item -ItemType Directory -Force -Path $dataDir | Out-Null
    [System.IO.File]::WriteAllText($startupChoiceMarker, "declined`n")
}

.\build\Debug\tts-host.exe --config $developmentConfigPath
exit $LASTEXITCODE
