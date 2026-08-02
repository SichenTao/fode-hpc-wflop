#!/usr/bin/env python3
"""Reject a Python/Torch production path for the target learning methods."""

from __future__ import annotations

import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
PYTHON_TORCH_TEST_ORACLES = {
    ROOT / "scripts/test_plan004_learning_backends.py",
    ROOT / "scripts/test_plan004_learning_target_artifacts.py",
}
TORCH_IMPORT = re.compile(
    r"(?m)^\s*(?:import\s+torch(?:\s|$)|from\s+torch(?:\.|\s))"
)
CPP_PRODUCTION_UNITS = {
    ROOT / "hpc/learning_libtorch/src/models.cpp",
    ROOT / "hpc/taae_cpp/src/evolution.cpp",
    ROOT / "hpc/wflop_cpp/src/algorithms/rlpso.cpp",
    ROOT / "hpc/wflop_cpp/src/algorithms.cpp",
}


def main() -> int:
    observed_python_torch = {
        path
        for path in (ROOT / "scripts").glob("*.py")
        if TORCH_IMPORT.search(path.read_text(encoding="utf-8"))
    }
    if observed_python_torch != PYTHON_TORCH_TEST_ORACLES:
        observed = sorted(
            str(path.relative_to(ROOT)) for path in observed_python_torch
        )
        raise RuntimeError(
            "Python/Torch use escaped the frozen test-oracle boundary: "
            f"observed={observed}"
        )

    missing_cpp = [
        str(path.relative_to(ROOT))
        for path in sorted(CPP_PRODUCTION_UNITS)
        if not path.is_file()
    ]
    if missing_cpp:
        raise RuntimeError(
            f"LibTorch C++ production units are absent: {missing_cpp}"
        )
    combined = "\n".join(
        path.read_text(encoding="utf-8") for path in CPP_PRODUCTION_UNITS
    )
    required_markers = ("LibTorch", "torch::", "learning_artifact")
    missing_markers = [
        marker for marker in required_markers if marker not in combined
    ]
    if missing_markers:
        raise RuntimeError(
            f"LibTorch C++ production markers are absent: {missing_markers}"
        )

    print(
        "libtorch_cpp_production_audit_pass "
        "python_torch_production=0 python_torch_test_oracles=2 "
        "cpp_production_units=4"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
