"""Compare original native readbacks from the P5.4 offscreen FX fixtures."""

import argparse
import hashlib
import json
import math
from pathlib import Path
import struct


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", type=Path, action="append", required=True)
    parser.add_argument("--require-backends", nargs="+", default=["dx11", "dx12", "vulkan", "metal"])
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    runs = []
    for root in args.input:
        for path in sorted(root.rglob("report.json")):
            data = json.loads(path.read_text(encoding="utf-8"))
            if "mipOperations" not in data:
                continue
            assert data["passed"] and data["strictCopy"] and data["strictRepeat"], path
            assert (data["draws"], data["submissions"], data["mipOperations"]) == (36, 27, 6), path
            assert data["floatAbsoluteTolerance"] == 0.002 and data["toneByteTolerance"] == 1, path
            runs.append((path.parent, data))
    assert runs, "No completed FX executions found"
    assert set(args.require_backends) <= {r[1]["backend"].split("-")[0] for r in runs}, "Missing backend"
    # Prefer the DX11 result for readable cross-API comparisons. Every run has
    # already passed full analytical checks and its own strict copy/repeat gate.
    runs.sort(key=lambda run: (not run[1]["backend"].startswith("dx11"), run[1]["backend"]))
    reference = runs[0][0]
    files = sorted([*reference.glob("*.rgba"), *reference.glob("*.bin")])
    assert len(files) == 12, "Incomplete RGBA8 / native HDR evidence"
    comparisons = []
    for directory, report in runs:
        actual_files = {p.name for p in directory.iterdir() if p.suffix in (".rgba", ".bin")}
        assert actual_files == {p.name for p in files}, directory
        byte_error, float_error = 0, 0.0
        hashes = {}
        for path in files:
            expected, actual = path.read_bytes(), (directory / path.name).read_bytes()
            assert len(expected) == len(actual) and expected, path.name
            hashes[path.name] = hashlib.sha256(actual).hexdigest()
            if path.suffix == ".rgba":
                error = max(abs(a - b) for a, b in zip(expected, actual))
                assert error <= 1, (report["backend"], path.name, error)
                byte_error = max(byte_error, error)
            else:
                assert len(actual) % 8 == 0, path.name
                pairs = zip(struct.iter_unpack("<e", expected), struct.iter_unpack("<e", actual))
                error = 0.0
                for (a,), (b,) in pairs:
                    assert math.isfinite(a) and math.isfinite(b), path.name
                    error = max(error, abs(a - b))
                assert error <= 0.002, (report["backend"], path.name, error)
                float_error = max(float_error, error)
        comparisons.append({**report, "max_rgba_byte_error_vs_reference": byte_error,
                            "max_float_absolute_error_vs_reference": float_error,
                            "image_sha256": hashes})
    result = {"schema": 1, "stage": "P5.4", "reference": runs[0][1]["backend"],
              "tolerances": {"cross_api_rgba_byte": 1, "float_absolute": 0.002,
                             "within_backend_copy_and_repeat": "byte-identical"},
              "executions": comparisons, "passed": True, "qt_mme_integrated": False}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(f"Compared {len(runs)} backend executions / {len(files)} images each: passed")


if __name__ == "__main__":
    main()
