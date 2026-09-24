# Go-to Windows build script: clean rebuild from scratch, then build and test
# everything currently implemented (tts-host, every runner, and the CTest
# suite). Deleting build/ every run is deliberate, not laziness — see
# README.md "Don't mix WSL and native Windows for the same build/ directory":
# a stale cache from a different generator/environment fails in confusing
# ways, and a full wipe is the only fix that's actually reliable. Downloaded
# FetchContent dependencies are intentionally kept outside build/ so the clean
# rebuild does not redownload them.

param(
    [switch]$ResetDependencyCache
)

$ErrorActionPreference = "Stop"

# Keep downloaded/extracted dependencies outside the disposable build tree.
# This path is gitignored and specific to the native Windows VS generator, so
# it cannot be confused with a WSL or another-generator build. Pass it through
# CMake's documented FETCHCONTENT_BASE_DIR override rather than teaching each
# individual dependency about a project-specific cache.
$dependencyCacheDir = Join-Path $PSScriptRoot "cache\fetchcontent\windows-vs2022"
if ($ResetDependencyCache -and (Test-Path $dependencyCacheDir)) {
    Write-Host "==> Resetting cached CMake dependencies" -ForegroundColor Yellow
    Remove-Item -Recurse -Force $dependencyCacheDir
}

# PowerShell's $ErrorActionPreference does not turn a non-zero exit from a
# native program (such as cmake or ctest) into a terminating error. Check it
# explicitly so a failed build cannot be followed by misleading, cascading
# CTest failures against executables that were never produced.
function Invoke-NativeChecked {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Description,
        [Parameter(Mandatory = $true)]
        [scriptblock]$Command
    )

    & $Command
    if ($LASTEXITCODE -ne 0) {
        throw "$Description failed with exit code $LASTEXITCODE."
    }
}

# Every run gets its own timestamped log under logs/ (gitignored) so past
# configure/build/test output can be reread later without rerunning the
# build. Date-time prefix keeps same-day runs distinct and sorts
# chronologically; the last file in logs/ is always the most recent run.
$logsDir = Join-Path $PSScriptRoot "logs"
if (-not (Test-Path $logsDir)) {
    New-Item -ItemType Directory -Path $logsDir | Out-Null
}
$logPath = Join-Path $logsDir ((Get-Date -Format "yyyy-MM-dd_HHmmss") + "_compile-win.log")

# Start-Transcript mirrors what's shown on screen into the log file without
# redirecting the output streams, so on-screen Write-Host colors are
# unaffected (unlike `*>&1 | Tee-Object`, which strips them).
Start-Transcript -Path $logPath | Out-Null

try {
    # espeak-ng.msi is unpacked by msiexec, which ships with Windows, so this
    # script no longer needs 7-Zip on PATH (see
    # docs/adr/0006-espeak-ng-vendoring-and-phoneme-mapping.md).

    Write-Host "==> Cleaning previous build/" -ForegroundColor Cyan
    if (Test-Path build) {
        Remove-Item -Recurse -Force build
    }

    Write-Host "==> Configuring (Visual Studio 17 2022)" -ForegroundColor Cyan
    Write-Host "    FetchContent cache: $dependencyCacheDir" -ForegroundColor DarkGray
    Invoke-NativeChecked "CMake configure" {
        cmake -S . -B build -G "Visual Studio 17 2022" "-DFETCHCONTENT_BASE_DIR=$dependencyCacheDir"
    }

    Write-Host "==> Building everything (Debug)" -ForegroundColor Cyan
    Invoke-NativeChecked "CMake build" { cmake --build build --config Debug --parallel }

    Write-Host "==> Running tests" -ForegroundColor Cyan
    Invoke-NativeChecked "CTest" { ctest --test-dir build -C Debug --output-on-failure }

    Write-Host "==> Done" -ForegroundColor Green
    Write-Host ""
    Write-Host "Other useful targets (see README.md ""Targets""):"
    Write-Host "  cmake --build build --config Debug --target list-models   (fast manual run, no full rebuild)"
    Write-Host "  cmake --build build --config Debug --target package       (build the installer zip)"
} finally {
    Stop-Transcript | Out-Null
}

Write-Host ""
Write-Host "Full log: $logPath" -ForegroundColor DarkGray
