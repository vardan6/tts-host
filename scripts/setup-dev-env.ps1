# Dev-only. Creates a project-local venv for optional Python tooling (e.g.
# regenerating ONNX test fixtures). Never required to build, test, run, or
# package tts-host -- see README.md "Dev-only Python tooling".
$ErrorActionPreference = "Stop"

Set-Location (Join-Path $PSScriptRoot "..")

$VenvDir = ".venv-windows"

py -3 -m venv $VenvDir
& ".\$VenvDir\Scripts\pip" install --upgrade pip
& ".\$VenvDir\Scripts\pip" install -r tools\dev-requirements.txt

Write-Host "Dev venv ready at $VenvDir\. Activate with: .\$VenvDir\Scripts\Activate.ps1"
