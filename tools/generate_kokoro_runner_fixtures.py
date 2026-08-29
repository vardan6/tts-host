"""Generate the placeholder ONNX Runtime CTest fixture.

Dev-only: run this once from the venv described in README.md "Dev-only
Python tooling" whenever tests/fixtures/kokoro_runner/placeholder.onnx needs
to be regenerated. The output is checked into git; the host, runners, and
release package never run this script or need Python installed.
"""

from pathlib import Path

import onnx
from onnx import TensorProto, helper

OUTPUT_PATH = Path(__file__).resolve().parent.parent / "tests" / "fixtures" / "kokoro_runner" / "placeholder.onnx"


def build_model() -> onnx.ModelProto:
    input_tensor = helper.make_tensor_value_info("input", TensorProto.FLOAT, [1])
    output_tensor = helper.make_tensor_value_info("output", TensorProto.FLOAT, [1])
    node = helper.make_node("Identity", inputs=["input"], outputs=["output"])
    graph = helper.make_graph([node], "placeholder", [input_tensor], [output_tensor])
    model = helper.make_model(graph, opset_imports=[helper.make_opsetid("", 17)])
    model.ir_version = 8
    onnx.checker.check_model(model)
    return model


def main() -> None:
    OUTPUT_PATH.parent.mkdir(parents=True, exist_ok=True)
    onnx.save(build_model(), OUTPUT_PATH)
    print(f"Wrote {OUTPUT_PATH}")


if __name__ == "__main__":
    main()
