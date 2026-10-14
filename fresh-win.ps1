# Reconfigure the Windows build tree, build every target, and run CTest.
param(
    [switch]$ResetDependencyCache
)

$ErrorActionPreference = "Stop"
Set-Location $PSScriptRoot

$fetchContentDir = Join-Path $PSScriptRoot "cache\fetchcontent\windows-vs2022"
$buildDir = Join-Path $PSScriptRoot "build"

# Refuse before deleting anything when a development process still uses this tree.
$buildPrefix = [System.IO.Path]::GetFullPath($buildDir).TrimEnd('\') + '\'
$runningBuildProcesses = @(Get-Process | Where-Object {
    try {
        $executablePath = $_.Path
        $executablePath -and $executablePath.StartsWith($buildPrefix, [System.StringComparison]::OrdinalIgnoreCase)
    } catch {
        $false
    }
})
if ($runningBuildProcesses.Count -gt 0) {
    $processList = ($runningBuildProcesses | ForEach-Object { "$($_.ProcessName) (PID $($_.Id))" }) -join ', '
    throw "Cannot refresh '$buildDir' while these processes are running: $processList. Choose Quit from the TTS Host tray menu, then rerun fresh-win.ps1. If a runner remains after the Host exits, end that listed runner in Task Manager."
}

if ($ResetDependencyCache -and (Test-Path -LiteralPath $fetchContentDir)) {
    Remove-Item -LiteralPath $fetchContentDir -Recurse -Force
}

if (Test-Path -LiteralPath $buildDir) {
    Remove-Item -LiteralPath $buildDir -Recurse -Force
}

cmake -S $PSScriptRoot -B $buildDir -G "Visual Studio 17 2022" "-DFETCHCONTENT_BASE_DIR=$fetchContentDir"
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

cmake --build $buildDir --config Debug --parallel
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

ctest --test-dir $buildDir -C Debug --output-on-failure
exit $LASTEXITCODE
