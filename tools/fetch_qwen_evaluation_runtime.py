#!/usr/bin/env python3
"""Fetch the pinned Windows x64 CUDA 12.4 llama.cpp evaluation runtime."""
import argparse
import hashlib
import shutil
import tempfile
import urllib.request
import zipfile
from pathlib import Path

ARTIFACTS = {
    "llama-b11029-bin-win-cuda-12.4-x64.zip":
        "b9740a623323aced02c8e4e4d9a98c16f348810c2bee6b1fc2036092bcdb8112",
    "cudart-llama-bin-win-cuda-12.4-x64.zip":
        "8c79a9b226de4b3cacfd1f83d24f962d0773be79f1e7b75c6af4ded7e32ae1d6",
}


def sha256(path):
    with path.open("rb") as stream:
        digest = hashlib.sha256()
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cache", required=True, type=Path)
    parser.add_argument("--out", required=True, type=Path)
    args = parser.parse_args()
    if args.out.exists():
        parser.error("output already exists; choose a new directory")
    args.cache.mkdir(parents=True, exist_ok=True)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    stage = Path(tempfile.mkdtemp(prefix=".qwen-runtime-", dir=args.out.parent))
    try:
        for name, expected in ARTIFACTS.items():
            archive = args.cache / name
            if not archive.is_file() or sha256(archive) != expected:
                partial = archive.with_suffix(".partial")
                with urllib.request.urlopen(
                        "https://github.com/ggml-org/llama.cpp/releases/download/b11029/" + name) as src:
                    with partial.open("wb") as dst:
                        shutil.copyfileobj(src, dst)
                if sha256(partial) != expected:
                    partial.unlink()
                    raise RuntimeError("SHA-256 mismatch: " + name)
                partial.replace(archive)
            with zipfile.ZipFile(archive) as package:
                for entry in package.infolist():
                    filename = Path(entry.filename).name
                    if filename.endswith(".dll") or filename.startswith("LICENSE"):
                        (stage / filename).write_bytes(package.read(entry))
        (stage / "tts-qwen-runtime.txt").write_text(
            "llama.cpp b11029 5c53396b89b05666c9d57445b7616a35f6198a16\n", encoding="ascii")
        stage.rename(args.out)
    finally:
        if stage.exists():
            shutil.rmtree(stage)
    print(args.out)


if __name__ == "__main__":
    main()
