# Builds the two-model Windows release zip and the per-user offline installer
# (docs/design/architecture.md#distribution), and checks that both bundled
# models speak from a clean extracted copy of the zip.
# Staging downloads the bundled packages once through the catalogue, so each
# carries its catalogue revision (ADR 0010); later runs reuse the staged copy.
param(
    [string]$QwenRuntime = "cache\qwen-evaluation\b11029\runtime",
    [string]$QwenDownloads = "cache\qwen-evaluation\b11029",
    [string]$ModelStage = "cache\release-models",
    [string]$BuildDir = "build-release",
    [switch]$SkipVerify,
    [switch]$SkipInstaller
)
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

# tts-host.exe is a GUI-subsystem program: PowerShell waits for it, and sets
# $LASTEXITCODE, only when its output is piped.
function Invoke-Checked {
    param([string]$File, [string[]]$Arguments, [switch]$Quiet)
    if ($Quiet) { & $File @Arguments | Select-Object -Last 2 | Out-Host }
    else { & $File @Arguments | Out-Host }
    if ($LASTEXITCODE -ne 0) { throw "$File failed with exit code $LASTEXITCODE" }
}

if (-not (Test-Path -LiteralPath $QwenRuntime)) {
    Invoke-Checked python @("tools/fetch_qwen_evaluation_runtime.py", "--cache", $QwenDownloads, "--out", $QwenRuntime)
}
$qwenRuntimePath = (Resolve-Path -LiteralPath $QwenRuntime).Path
New-Item -ItemType Directory -Force -Path (Join-Path $ModelStage "models") | Out-Null
$stagePath = (Resolve-Path -LiteralPath $ModelStage).Path
$stageModels = Join-Path $stagePath "models"
$fetchContentDir = Join-Path $root "cache\fetchcontent\windows-vs2022"

Invoke-Checked cmake @("-S", $root, "-B", $BuildDir, "-G", "Visual Studio 17 2022",
    "-DFETCHCONTENT_BASE_DIR=$fetchContentDir", "-DBUILD_TESTING=OFF", "-DTTS_HOST_RELEASE=ON",
    "-DTTS_HOST_QWEN_RUNTIME_DIR=$qwenRuntimePath", "-DTTS_HOST_RELEASE_MODELS_DIR=$stageModels")
Invoke-Checked cmake @("--build", $BuildDir, "--config", "Release", "--parallel")

# The stage mirrors the application tree, so the generated "$schema" is the
# same ../../schemas/model.schema.json the package resolves.
$host_exe = Join-Path $BuildDir "Release\tts-host.exe"
Copy-Item -Recurse -Force (Join-Path $root "schemas") $stagePath
$stageConfig = Join-Path $stagePath "config.json"
$config = Get-Content -LiteralPath (Join-Path $root "config.example.json") -Raw -Encoding UTF8 | ConvertFrom-Json
$config.modelRegistry.directories = @("./models")
$config.startup.launchOnLogin = $false
[System.IO.File]::WriteAllText($stageConfig, ($config | ConvertTo-Json -Depth 100), (New-Object System.Text.UTF8Encoding $false))
foreach ($model in @("kokoro-en-v1", "qwen3-tts-1.7b")) {
    if (-not (Test-Path -LiteralPath (Join-Path $stageModels "$model\model.json"))) {
        Invoke-Checked $host_exe @("--headless", "--config", $stageConfig, "--data-dir", $stagePath,
            "--download-model", $model) -Quiet
    }
}

Invoke-Checked cmake @("--build", $BuildDir, "--config", "Release", "--target", "package")
$zip = Get-ChildItem -LiteralPath (Join-Path $BuildDir "dist") -Filter "tts-host-*.zip" |
    Sort-Object LastWriteTime | Select-Object -Last 1
Write-Host "Release package: $($zip.FullName)"

if (-not $SkipVerify) {
    # Clean extracted copy: nothing from the build tree, the stage, or the
    # developer's per-user data. The real offline check is HITL acceptance.
    $check = Join-Path ([System.IO.Path]::GetTempPath()) ("tts-host-release-" + [guid]::NewGuid())
    try {
        Expand-Archive -LiteralPath $zip.FullName -DestinationPath $check
        $app = Get-ChildItem -LiteralPath $check -Directory | Select-Object -First 1
        $exe = Join-Path $app.FullName "tts-host.exe"
        $appConfig = Join-Path $app.FullName "config.json"
        Invoke-Checked $exe @("--headless", "--config", $appConfig, "--list-models")
        foreach ($model in @("kokoro-en-v1", "qwen3-tts-1.7b")) {
            $out = Join-Path $check "$model.wav"
            Invoke-Checked $exe @("--headless", "--config", $appConfig, "--model", $model,
                "--synthesize", "The release package speaks offline.", "--out", $out)
            if ((Get-Item -LiteralPath $out).Length -le 44) { throw "$model produced no audio" }
            Write-Host "$model synthesized $((Get-Item -LiteralPath $out).Length) bytes"
        }
    } finally {
        Remove-Item -Recurse -Force -LiteralPath $check -ErrorAction SilentlyContinue
    }
}

# The installer packs the same tree; tts-host.iss leaves out portable.marker.
if (-not $SkipInstaller) {
    $iscc = @("${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe", "$env:ProgramFiles\Inno Setup 6\ISCC.exe",
        "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe") | Where-Object { Test-Path -LiteralPath $_ } |
        Select-Object -First 1
    if (-not $iscc) { throw "Inno Setup 6 not found; install it (winget install JRSoftware.InnoSetup) or pass -SkipInstaller" }
    $tree = Join-Path (Resolve-Path -LiteralPath $BuildDir).Path "installer-tree"
    if (Test-Path -LiteralPath $tree) { Remove-Item -Recurse -Force -LiteralPath $tree }
    Invoke-Checked cmake @("--install", $BuildDir, "--config", "Release", "--prefix", $tree)
    $version = (Select-String -LiteralPath (Join-Path $root "CMakeLists.txt") -Pattern 'project\(tts_host VERSION ([0-9.]+)').Matches[0].Groups[1].Value
    $shipped = Get-Content -LiteralPath (Join-Path $root "config.example.json") -Raw -Encoding UTF8 | ConvertFrom-Json
    $launchOnLogin = if ($shipped.startup.launchOnLogin) { 1 } else { 0 }
    Invoke-Checked $iscc @("/Q", "/DSourceDir=$tree", "/DAppVersion=$version",
        "/DOutputDir=$((Resolve-Path -LiteralPath (Join-Path $BuildDir 'dist')).Path)",
        "/DLaunchOnLoginDefault=$launchOnLogin", (Join-Path $root "installer\tts-host.iss"))
    Write-Host "Installer: $(Join-Path $BuildDir "dist\tts-host-$version-setup.exe")"
}
