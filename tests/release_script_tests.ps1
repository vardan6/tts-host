# Exercises the ref and publication guarantees of scripts/release.ps1
# (docs/design/architecture.md#versions-and-releases) against a temporary bare
# remote. The build, packaging, and gh commands are stubbed, so no installer is
# built and no GitHub account is used.
$ErrorActionPreference = "Stop"
$releaseScript = Join-Path (Split-Path -Parent $PSScriptRoot) "scripts\release.ps1"
$version = "1.2.3-alpha.1"
$tag = "v$version"
$temp = Join-Path ([System.IO.Path]::GetTempPath()) ("ttsreltest-" + [guid]::NewGuid().ToString("N").Substring(0, 8))
$failures = @()

function Invoke-Git {
    $ErrorActionPreference = "Continue"
    $output = & git.exe @args 2>&1 | ForEach-Object { "$_" }
    if ($LASTEXITCODE -ne 0) { throw "git $($args -join ' ') failed: $($output -join ' ')" }
    return ($output -join "`n").Trim()
}

function Test-Ref {
    param([string]$Repo, [string]$Ref)
    & git.exe -C $Repo rev-parse --quiet --verify $Ref *> $null
    return $LASTEXITCODE -eq 0
}

function Assert-True {
    param([bool]$Condition, [string]$Message)
    if (-not $Condition) { $script:failures += $Message; Write-Host "FAIL: $Message" }
}

function Write-File {
    param([string]$Path, [string]$Text)
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $Path) | Out-Null
    [System.IO.File]::WriteAllText($Path, $Text, (New-Object System.Text.UTF8Encoding $false))
}

# A checkout on main whose origin is a bare repository, holding the real
# release script beside a release-win.ps1 that only writes the artifacts.
function New-Fixture {
    param([string]$Name)
    $dir = Join-Path $temp $Name
    $remote = Join-Path $dir "origin.git"
    $repo = Join-Path $dir "repo"
    Invoke-Git init --quiet --bare --initial-branch=main $remote | Out-Null
    Invoke-Git init --quiet --initial-branch=main $repo | Out-Null
    foreach ($setting in @(@("user.name", "Release Test"), @("user.email", "release-test@example.com"),
            @("core.autocrlf", "false"))) {
        Invoke-Git -C $repo config $setting[0] $setting[1] | Out-Null
    }
    Write-File (Join-Path $repo "CMakeLists.txt") "project(tts_host VERSION 0.0.1)`nset(TTS_HOST_VERSION_PRERELEASE `"`")`n"
    Write-File (Join-Path $repo ".gitignore") "build-release/`n"
    Write-File (Join-Path $repo "docs\releases\$tag.md") "Notes`n"
    Write-File (Join-Path $repo "scripts\release.ps1") ([System.IO.File]::ReadAllText($releaseScript))
    Write-File (Join-Path $repo "scripts\release-win.ps1") @'
param($Variant, $QwenRuntime, $QwenDownloads, $ModelStage, $FetchContentDir, $BuildDir)
$dist = Join-Path $BuildDir "dist"
New-Item -ItemType Directory -Force -Path $dist | Out-Null
foreach ($name in @("tts-host-$env:TTS_RELEASE_TEST_VERSION-setup.exe", "tts-host-$env:TTS_RELEASE_TEST_VERSION-portable.zip")) {
    Set-Content -LiteralPath (Join-Path $dist $name) -Value $name
}
'@
    Invoke-Git -C $repo add -A | Out-Null
    Invoke-Git -C $repo commit --quiet -m "Base" | Out-Null
    Invoke-Git -C $repo remote add origin $remote | Out-Null
    Invoke-Git -C $repo push --quiet -u origin main | Out-Null
    return @{ Remote = $remote; Repo = $repo; Base = (Invoke-Git -C $repo rev-parse HEAD) }
}

function Invoke-Release {
    param([string]$Repo, [switch]$DryRun)
    $arguments = @("-NoProfile", "-ExecutionPolicy", "Bypass", "-File", (Join-Path $Repo "scripts\release.ps1"),
        "-Version", $version, "-Variants", "standard")
    if ($DryRun) { $arguments += "-DryRun" }
    $ErrorActionPreference = "Continue"
    $output = & powershell.exe @arguments 2>&1 | ForEach-Object { "$_" }
    return @{ Exit = $LASTEXITCODE; Output = ($output -join "`n") }
}

$savedPath = $env:Path
try {
    # cmake, ctest, and gh resolve to stubs; gh logs its arguments and fails
    # `release` calls with TTS_RELEASE_TEST_GH_EXIT.
    $bin = Join-Path $temp "bin"
    New-Item -ItemType Directory -Force -Path $bin | Out-Null
    Write-File (Join-Path $bin "cmake.cmd") "@exit /b 0`r`n"
    Write-File (Join-Path $bin "ctest.cmd") "@exit /b 0`r`n"
    Write-File (Join-Path $bin "gh.cmd") ("@echo %*>>`"%TTS_RELEASE_TEST_GH_LOG%`"`r`n" +
        "@if `"%1`"==`"release`" exit /b %TTS_RELEASE_TEST_GH_EXIT%`r`n@exit /b 0`r`n")
    $env:Path = "$bin;$savedPath"
    $env:TTS_RELEASE_TEST_VERSION = $version
    $env:TTS_RELEASE_TEST_GH_LOG = Join-Path $temp "gh.log"

    # A dry run moves no ref, even when origin has moved on and gained a tag.
    $env:TTS_RELEASE_TEST_GH_EXIT = "0"
    $fixture = New-Fixture "dry-run"
    $other = Join-Path $temp "dry-run\other"
    Invoke-Git clone --quiet $fixture.Remote $other | Out-Null
    Invoke-Git -C $other -c user.name=Other -c user.email=other@example.com commit --quiet --allow-empty -m "Ahead" | Out-Null
    Invoke-Git -C $other tag unrelated | Out-Null
    Invoke-Git -C $other push --quiet origin main unrelated | Out-Null
    Invoke-Git -C $fixture.Repo checkout --quiet -b release-prep | Out-Null
    $before = Invoke-Git -C $fixture.Repo show-ref
    $result = Invoke-Release $fixture.Repo -DryRun
    Assert-True ($result.Exit -eq 0) "a dry run passes from a release branch: $($result.Output)"
    Assert-True ((Invoke-Git -C $fixture.Repo show-ref) -eq $before) "a dry run leaves every ref unchanged"
    Assert-True (-not (Invoke-Git -C $fixture.Repo tag --list)) "a dry run imports and creates no tag"
    Assert-True (Test-Path -LiteralPath (Join-Path $fixture.Repo "build-release\dist\dry-run\tts-host-$version-setup.exe")) `
        "a dry run keeps its artifacts in dist/dry-run"
    Assert-True (@((Invoke-Git -C $fixture.Repo worktree list) -split "`n").Count -eq 1) "a dry run removes its worktree"

    # A rejected atomic push leaves origin unchanged and deletes the local tag.
    $fixture = New-Fixture "rejected"
    Write-File (Join-Path $fixture.Remote "hooks\pre-receive") "#!/bin/sh`nexit 1`n"
    $result = Invoke-Release $fixture.Repo
    Assert-True ($result.Exit -ne 0 -and $result.Output -match "rejected") "a rejected push fails and says so"
    Assert-True ((Invoke-Git -C $fixture.Remote rev-parse main) -eq $fixture.Base) "a rejected push leaves origin's main"
    Assert-True (-not (Test-Ref $fixture.Remote "refs/tags/$tag")) "a rejected push leaves origin without the tag"
    Assert-True (-not (Test-Ref $fixture.Repo "refs/tags/$tag")) "a rejected push deletes the local tag"
    Assert-True ((Invoke-Git -C $fixture.Repo rev-parse main) -eq $fixture.Base) "a rejected push leaves the local main"

    # Success tags the built commit, pushes it to main, and fast-forwards main.
    Remove-Item -LiteralPath $env:TTS_RELEASE_TEST_GH_LOG -ErrorAction SilentlyContinue
    $fixture = New-Fixture "published"
    $result = Invoke-Release $fixture.Repo
    Assert-True ($result.Exit -eq 0) "a release succeeds: $($result.Output)"
    $tagged = Invoke-Git -C $fixture.Remote rev-parse "refs/tags/$tag^{commit}"
    Assert-True ($tagged -eq (Invoke-Git -C $fixture.Remote rev-parse main)) "origin's main is the tagged commit"
    Assert-True ($tagged -eq (Invoke-Git -C $fixture.Repo rev-parse main)) "the local main fast-forwards to the tag"
    Assert-True ((Invoke-Git -C $fixture.Remote rev-parse "$tagged^") -eq $fixture.Base) "the release commit sits on main"
    $cmake = Invoke-Git -C $fixture.Remote show "${tagged}:CMakeLists.txt"
    Assert-True ($cmake -match "VERSION 1\.2\.3" -and $cmake -match 'PRERELEASE "alpha\.1"') "the tagged commit carries the version"
    $gh = Get-Content -Raw -LiteralPath $env:TTS_RELEASE_TEST_GH_LOG
    Assert-True ($gh -match "release create $tag --draft" -and $gh -match "--prerelease" -and
        $gh -match "tts-host-$version-setup\.exe" -and $gh -match "tts-host-$version-portable\.zip") `
        "the draft is a pre-release with both artifacts"

    # A failed draft leaves the pushed tag, keeps the artifacts, and prints recovery.
    $env:TTS_RELEASE_TEST_GH_EXIT = "1"
    $fixture = New-Fixture "draft-failed"
    $result = Invoke-Release $fixture.Repo
    Assert-True ($result.Exit -eq 1) "a failed draft fails the run"
    Assert-True (Test-Ref $fixture.Remote "refs/tags/$tag") "a failed draft leaves the pushed tag"
    foreach ($name in @("tts-host-$version-setup.exe", "tts-host-$version-portable.zip")) {
        Assert-True (Test-Path -LiteralPath (Join-Path $fixture.Repo "build-release\dist\$version\$name")) `
            "a failed draft keeps $name"
    }
    Assert-True ($result.Output -match "gh release create $tag" -and $result.Output -match "gh release upload $tag") `
        "a failed draft prints the commands that finish it"
} finally {
    $env:Path = $savedPath
    Set-Location ([System.IO.Path]::GetTempPath())
    Remove-Item -Recurse -Force -LiteralPath $temp -ErrorAction SilentlyContinue
}

if ($failures.Count -gt 0) {
    Write-Host "$($failures.Count) release script check(s) failed"
    exit 1
}
Write-Host "Release script checks passed"
