# Builds, verifies, tags, and drafts a GitHub release
# (docs/design/architecture.md#versions-and-releases). Runs locally on Windows:
# hosted runners have no GPU for the NVIDIA speech check and would re-download
# the models on every run.
#
#   scripts/release.ps1 -Version 0.1.0-alpha.1 [-Variants standard,nvidia] [-DryRun]
#
# Procedure:
# 1. Refuse unless the tree is clean, on main, main matches origin/main, the
#    tag v<version> exists neither locally nor on origin, and the notes file
#    docs/releases/v<version>.md exists. -DryRun skips the main and origin/main
#    checks, so a release branch is rehearsed before it merges.
# 2. Create a temporary detached worktree of that commit outside the source
#    tree. Write the version there and commit it; when the version is already
#    written, that commit is the release commit and no empty commit is made.
# 3. Build in the worktree and require every test to pass.
# 4. Build each selected variant with scripts/release-win.ps1, which runs its
#    extracted-copy speech check. Model stages, runtimes, and FetchContent
#    sources come from this checkout's cache/, so nothing cached is fetched.
# 5. Stop on any artifact of 2 GiB or more (GitHub's per-file limit).
# 6. -DryRun stops here: artifacts stay in build-release/dist/dry-run/.
# 7. Tag the release commit, then push it to main and push the tag in one
#    atomic push; a rejected push deletes the local tag. Then fast-forward this
#    checkout's main to the release commit.
# 8. Create the draft (a pre-release when the version has a suffix) with its
#    notes, and upload the artifacts.
#
# Through step 6 this checkout's branches and tags are untouched and the
# worktree is removed, so a dry run or any failure is retried by running the
# script again; a real run rebuilds everything rather than reusing a
# rehearsal. The tag names the commit its artifacts were built from, and
# nothing unverified is pushed. A step 8 failure leaves the pushed tag; the
# artifacts stay in build-release/dist/<version>/ and the script prints the
# gh commands that finish the draft.
param(
    [Parameter(Mandatory = $true)]
    [string]$Version,
    [ValidateSet("standard", "nvidia")]
    [string[]]$Variants = @("standard", "nvidia"),
    [switch]$DryRun
)
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

# Exit codes decide failure. Windows PowerShell turns a native command's stderr
# into a terminating error under "Stop" once the script's output is redirected,
# and git, cmake, and MSBuild report progress there.
# A stderr line arrives as an ErrorRecord whose TargetObject is the line itself.
filter Format-NativeLine { if ($_ -is [System.Management.Automation.ErrorRecord]) { "$($_.TargetObject)" } else { "$_" } }
function Invoke-Native {
    param([string]$File, [string[]]$Arguments)
    $ErrorActionPreference = "Continue"
    & $File @Arguments 2>&1 | Format-NativeLine | Out-Host
}
function Invoke-Checked {
    param([string]$File, [string[]]$Arguments)
    Invoke-Native $File $Arguments
    if ($LASTEXITCODE -ne 0) { throw "$File $($Arguments -join ' ') failed with exit code $LASTEXITCODE" }
}
function Get-GitOutput {
    param([string[]]$Arguments)
    $ErrorActionPreference = "Continue"
    $output = & git @Arguments
    if ($LASTEXITCODE -ne 0) { throw "git $($Arguments -join ' ') failed with exit code $LASTEXITCODE" }
    return ($output -join "`n").Trim()
}

# SemVer stages this project uses: X.Y.Z, or X.Y.Z-(alpha|beta|rc).N.
$match = [regex]::Match($Version, '^(\d+\.\d+\.\d+)(?:-((?:alpha|beta|rc)\.[1-9]\d*))?$')
if (-not $match.Success) { throw "Version '$Version' is not X.Y.Z or X.Y.Z-(alpha|beta|rc).N" }
$numeric = $match.Groups[1].Value
$prerelease = $match.Groups[2].Value
$tag = "v$Version"
$Variants = @($Variants | Select-Object -Unique)
if ($Variants -notcontains "standard") { throw "Every release ships the standard variant (ADR 0011)" }

# 1. Preconditions, before anything is built.
if (Get-GitOutput @("status", "--porcelain")) { throw "The tree is not clean; commit or set aside every change first" }
$branch = Get-GitOutput @("rev-parse", "--abbrev-ref", "HEAD")
if (-not $DryRun -and $branch -ne "main") { throw "Releases are made from main, not '$branch'" }
Invoke-Checked git @("fetch", "--quiet", "origin")
$base = Get-GitOutput @("rev-parse", "HEAD")
if (-not $DryRun -and $base -ne (Get-GitOutput @("rev-parse", "origin/main"))) { throw "main does not match origin/main" }
& git rev-parse --quiet --verify "refs/tags/$tag" *> $null
if ($LASTEXITCODE -eq 0) { throw "Tag $tag already exists locally" }
if (Get-GitOutput @("ls-remote", "--tags", "origin", "refs/tags/$tag")) { throw "Tag $tag already exists on origin" }
$notes = Join-Path $root "docs\releases\$tag.md"
if (-not (Test-Path -LiteralPath $notes)) { throw "Release notes $notes are missing" }
if (-not $DryRun) { Invoke-Checked gh @("auth", "status") }

$work = Join-Path ([System.IO.Path]::GetTempPath()) ("ttsrel-" + [guid]::NewGuid().ToString("N").Substring(0, 8))
$dist = Join-Path $root ("build-release\dist\" + $(if ($DryRun) { "dry-run" } else { $Version }))
$artifacts = @()
try {
    # 2. The release commit, in a worktree this checkout's refs never see.
    Invoke-Checked git @("worktree", "add", "--detach", $work, $base)
    $cmakeLists = Join-Path $work "CMakeLists.txt"
    $text = [System.IO.File]::ReadAllText($cmakeLists)
    $written = [regex]::Replace($text, 'project\(tts_host VERSION [0-9.]+', "project(tts_host VERSION $numeric")
    $written = [regex]::Replace($written, 'set\(TTS_HOST_VERSION_PRERELEASE "[^"]*"', "set(TTS_HOST_VERSION_PRERELEASE `"$prerelease`"")
    if ($written -ne $text) {
        [System.IO.File]::WriteAllText($cmakeLists, $written, (New-Object System.Text.UTF8Encoding $false))
        Invoke-Checked git @("-C", $work, "commit", "--quiet", "-am", "Release $tag")
    }
    $commit = Get-GitOutput @("-C", $work, "rev-parse", "HEAD")

    # 3. Every test, from the release source.
    $fetchContent = Join-Path $root "cache\fetchcontent\windows-vs2022"
    $qwenRuntime = Join-Path $root "cache\qwen-evaluation\b11029\runtime"
    $testBuild = Join-Path $work "build-test"
    $configure = @("-S", $work, "-B", $testBuild, "-G", "Visual Studio 17 2022",
        "-DFETCHCONTENT_BASE_DIR=$fetchContent", "-DBUILD_TESTING=ON")
    if (Test-Path -LiteralPath $qwenRuntime) { $configure += "-DTTS_HOST_QWEN_RUNTIME_DIR=$qwenRuntime" }
    Invoke-Checked cmake $configure
    Invoke-Checked cmake @("--build", $testBuild, "--config", "Release", "--parallel")
    Invoke-Checked ctest @("--test-dir", $testBuild, "-C", "Release", "--output-on-failure")

    # 4. Each selected variant, with its packaging and speech checks.
    foreach ($variant in $Variants) {
        & (Join-Path $work "scripts\release-win.ps1") -Variant $variant `
            -QwenRuntime $qwenRuntime -QwenDownloads (Join-Path $root "cache\qwen-evaluation\b11029") `
            -ModelStage (Join-Path $root "cache\release-models") -FetchContentDir $fetchContent `
            -BuildDir (Join-Path $work "build-release\$variant")
        Set-Location $root
        $variantDist = Join-Path $work "build-release\$variant\dist"
        $names = if ($variant -eq "standard") {
            @("tts-host-$Version-setup.exe", "tts-host-$Version-portable.zip")
        } else {
            @("tts-host-$Version-nvidia-setup.exe")
        }
        foreach ($name in $names) {
            $path = Join-Path $variantDist $name
            if (-not (Test-Path -LiteralPath $path)) { throw "The $variant variant did not produce $name" }
            $artifacts += Get-Item -LiteralPath $path
        }
    }

    # 5. GitHub's per-file limit.
    foreach ($artifact in $artifacts) {
        if ($artifact.Length -ge 2GB) { throw "$($artifact.Name) is $($artifact.Length) bytes, over the 2 GiB limit" }
    }
    if (Test-Path -LiteralPath $dist) { Remove-Item -Recurse -Force -LiteralPath $dist }
    New-Item -ItemType Directory -Force -Path $dist | Out-Null
    $artifacts = @($artifacts | ForEach-Object { Copy-Item -LiteralPath $_.FullName -Destination $dist -PassThru })
} finally {
    Set-Location $root
    if (Test-Path -LiteralPath $work) { Invoke-Native git @("worktree", "remove", "--force", $work) }
    Invoke-Native git @("worktree", "prune")
}

# 6. The rehearsal ends before anything is tagged or pushed.
if ($DryRun) {
    Write-Host "Dry run passed for $tag (commit $commit, not kept). Artifacts in ${dist}:"
    $artifacts | ForEach-Object { Write-Host "  $($_.Name)  $($_.Length) bytes" }
    exit 0
}

# 7. Tag the built commit; publish it and the tag together or not at all.
Invoke-Checked git @("tag", "-a", $tag, $commit, "-m", "TTS Host $Version")
Invoke-Native git @("push", "--atomic", "origin", "${commit}:refs/heads/main", "refs/tags/$tag")
if ($LASTEXITCODE -ne 0) {
    Invoke-Native git @("tag", "-d", $tag)
    throw "Push of $tag was rejected; the local tag was deleted and origin is unchanged"
}
Invoke-Checked git @("merge", "--ff-only", "--quiet", $commit)

# 8. The draft; the user publishes it after testing.
$create = @("release", "create", $tag, "--draft", "--verify-tag", "--title", "TTS Host $Version", "--notes-file", $notes)
if ($prerelease) { $create += "--prerelease" }
$create += @($artifacts | ForEach-Object { $_.FullName })
Invoke-Native gh $create
if ($LASTEXITCODE -ne 0) {
    Write-Host "Tag $tag is pushed, but the draft was not completed. Finish it with:"
    Write-Host "  gh $($create -join ' ')"
    Write-Host "or, if the draft exists: gh release upload $tag --clobber $(($artifacts | ForEach-Object { $_.FullName }) -join ' ')"
    exit 1
}
Write-Host "Draft $tag created; test it, then publish it on GitHub."
