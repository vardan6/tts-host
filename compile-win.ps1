# Incremental Windows build, test, and launch loop.
$ErrorActionPreference = "Stop"
Set-Location $PSScriptRoot

$fetchContentDir = Join-Path $PSScriptRoot "cache\fetchcontent\windows-vs2022"
$buildDir = Join-Path $PSScriptRoot "build"
$debugDir = Join-Path $buildDir "Debug"

if (-not (Test-Path -LiteralPath (Join-Path $buildDir "CMakeCache.txt"))) {
    cmake -S $PSScriptRoot -B $buildDir -G "Visual Studio 17 2022" "-DFETCHCONTENT_BASE_DIR=$fetchContentDir"
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

cmake --build $buildDir --config Debug --parallel
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

ctest --test-dir $buildDir -C Debug --output-on-failure
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

# Keep developer launches separate from the shipped defaults and user settings.
$developmentConfigPath = Join-Path $PSScriptRoot "config.development.json"
$configSourcePath = if (Test-Path -LiteralPath $developmentConfigPath) { $developmentConfigPath } else { Join-Path $PSScriptRoot "config.example.json" }
$developmentConfig = Get-Content -LiteralPath $configSourcePath -Raw -Encoding UTF8 | ConvertFrom-Json
if (-not $developmentConfig.PSObject.Properties["startup"]) {
    $developmentConfig | Add-Member -NotePropertyName startup -NotePropertyValue ([pscustomobject]@{})
}
$developmentConfig.startup | Add-Member -NotePropertyName launchOnLogin -NotePropertyValue $false -Force
$developmentConfigJson = $developmentConfig | ConvertTo-Json -Depth 100
[System.IO.File]::WriteAllText($developmentConfigPath, $developmentConfigJson, (New-Object System.Text.UTF8Encoding $false))

# Answer the first-run login-startup question in advance so development
# launches never ask or register (docs/design/architecture.md#login-startup).
$dataDir = if (Test-Path -LiteralPath (Join-Path $debugDir "portable.marker")) { $debugDir } else { Join-Path $env:LOCALAPPDATA "TTS Host" }
$startupChoiceMarker = Join-Path $dataDir "startup-choice.marker"
if (-not (Test-Path -LiteralPath $startupChoiceMarker)) {
    New-Item -ItemType Directory -Force -Path $dataDir | Out-Null
    [System.IO.File]::WriteAllText($startupChoiceMarker, "declined`n")
}

& (Join-Path $debugDir "tts-host.exe") --config $developmentConfigPath
exit $LASTEXITCODE
