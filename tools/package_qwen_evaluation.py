#!/usr/bin/env python3
"""Create a local, data-only Qwen Base evaluation package (no downloads)."""
import argparse
import json
import shutil
import tempfile
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("model", "mmproj", "voice", "voice-license", "out"):
        parser.add_argument("--" + name, required=True, type=Path)
    parser.add_argument("--id", default="qwen3-tts-1.7b")
    parser.add_argument("--language", choices=("en", "ru"), default="en")
    args = parser.parse_args()
    import re
    if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._-]*", args.id):
        parser.error("--id must be a manifest-safe model ID")
    assets = {"model": args.model, "mmproj": args.mmproj,
              "voice": args.voice, "voiceLicense": args.voice_license}
    for key, path in assets.items():
        if not path.is_file() or not path.stat().st_size:
            parser.error(f"{key} must be a non-empty local file: {path}")
    for path in (args.model, args.mmproj):
        with path.open("rb") as stream:
            if stream.read(4) != b"GGUF":
                parser.error(f"not a GGUF file: {path}")
    if args.out.exists():
        parser.error("output already exists; choose a new directory")
    args.out.parent.mkdir(parents=True, exist_ok=True)
    stage = Path(tempfile.mkdtemp(prefix=".qwen-stage-", dir=args.out.parent))
    try:
        names = {"model": "model.gguf", "mmproj": "mmproj.gguf",
                 "voice": "reference.wav", "voiceLicense": "reference-license.txt"}
        for key, path in assets.items():
            shutil.copyfile(path, stage / names[key])
        (stage / "schemas").mkdir()
        shutil.copyfile(Path(__file__).resolve().parents[1] / "schemas/model.schema.json",
                        stage / "schemas/model.schema.json")
        manifest = {"$schema": "schemas/model.schema.json", "schemaVersion": 1,
                    "id": args.id, "displayName": "Qwen3-TTS Base (evaluation)",
                    "engine": "qwen3-gguf", "languages": [args.language], "files": names,
                    "license": {"name": "Apache-2.0", "url": "https://huggingface.co/Qwen"}}
        (stage / "model.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
        stage.rename(args.out)
    finally:
        if stage.exists():
            shutil.rmtree(stage)
    print(args.out)


if __name__ == "__main__":
    main()
