"""Fetch the real Kokoro-82M ONNX weights and one voice embedding.

Dev-only, stdlib-only (no extra dependency beyond Python itself): downloads
the full-precision model and a single default voice from the canonical
onnx-community/Kokoro-82M-v1.0-ONNX Hugging Face repo (Apache-2.0) into
models/kokoro-en-v1/, gitignored real product data, not a checked-in test
fixture like tools/generate_kokoro_runner_fixtures.py. Run manually; the host,
runners, and release package never run this script or need Python installed.
"""

import json
import urllib.request
from pathlib import Path

REPO = "onnx-community/Kokoro-82M-v1.0-ONNX"
MODEL_FILE = "onnx/model.onnx"
VOICE_FILE = "voices/af_heart.bin"
BASE_URL = f"https://huggingface.co/{REPO}/resolve/main"

PACKAGE_DIR = Path(__file__).resolve().parent.parent / "models" / "kokoro-en-v1"


def download(url: str, destination: Path) -> None:
    destination.parent.mkdir(parents=True, exist_ok=True)
    print(f"Downloading {url} -> {destination}")
    urllib.request.urlretrieve(url, destination)


def write_manifest() -> None:
    manifest = {
        "$schema": "../../schemas/model.schema.json",
        "schemaVersion": 1,
        "id": "kokoro-en-v1",
        "displayName": "Kokoro-82M (English)",
        "engine": "kokoro-onnx",
        "languages": ["en"],
        "files": {
            "model": "model.onnx",
            "voice": "voices/af_heart.bin",
        },
        "license": {
            "name": "Apache-2.0",
            "url": f"https://huggingface.co/{REPO}",
        },
    }
    manifest_path = PACKAGE_DIR / "model.json"
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(f"Wrote {manifest_path}")


def main() -> None:
    download(f"{BASE_URL}/{MODEL_FILE}", PACKAGE_DIR / "model.onnx")
    download(f"{BASE_URL}/{VOICE_FILE}", PACKAGE_DIR / "voices" / "af_heart.bin")
    write_manifest()


if __name__ == "__main__":
    main()
