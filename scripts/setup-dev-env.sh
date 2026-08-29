#!/usr/bin/env bash
# Dev-only. Creates a project-local venv for optional Python tooling (e.g.
# regenerating ONNX test fixtures). Never required to build, test, run, or
# package tts-host — see README.md "Dev-only Python tooling".
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")/.."

VENV_DIR=.venv-linux

python3 -m venv "$VENV_DIR"
"$VENV_DIR/bin/pip" install --no-compile --upgrade pip
# --no-compile: on a WSL DrvFs path (/mnt/c/...), pip's post-install .pyc
# bytecode compilation step fails with "AssertionError: pyc_path". Skipping
# it is harmless — Python compiles bytecode lazily on first import anyway.
"$VENV_DIR/bin/pip" install --no-compile -r tools/dev-requirements.txt

echo "Dev venv ready at $VENV_DIR/. Activate with: source $VENV_DIR/bin/activate"
