# Go-to Windows build script: clean rebuild from scratch, then build and test
# everything currently implemented (tts-host, every runner, and the CTest
# suite). Deleting build/ every run is deliberate, not laziness — see
# README.md "Don't mix WSL and native Windows for the same build/ directory":
# a stale cache from a different generator/environment fails in confusing
# ways, and a full wipe is the only fix that's actually reliable.

$ErrorActionPreference = "Stop"

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
    Invoke-NativeChecked "CMake configure" { cmake -S . -B build -G "Visual Studio 17 2022" }

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
