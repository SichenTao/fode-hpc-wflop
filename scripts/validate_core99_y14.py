#!/usr/bin/env python3
"""
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: independent Y14 equation, role and schedule validator
Paper DOI: 10.1109/TSTE.2026.3661110
Public asset, missing information, conflict, reconstruction, semantic IDs,
production backend, controlling contract and claim boundary:
hpc/core99_cpp/include/core99/zhang_y14.hpp
Claim boundary: flexible academic reconstruction, not author numeric replay.
Last evidence-audit date: 2026-08-01
END WFLOP IMPLEMENTATION FACT DECLARATION
"""

from __future__ import annotations

import argparse
import json
import math
from pathlib import Path
import subprocess
import tempfile


CASES = (
    "Y14_n16_original", "Y14_n24_original", "Y14_n48_original",
    "Y14_n16_adjusted", "Y14_n24_adjusted", "Y14_n48_adjusted",
)


def call(binary: Path, *arguments: str) -> object:
    completed = subprocess.run(
        [str(binary), *arguments], check=True, capture_output=True, text=True
    )
    return json.loads(completed.stdout)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", type=Path, required=True)
    args = parser.parse_args()
    binary = args.binary.resolve()
    assert tuple(call(binary, "--action", "list-cases")) == CASES
    expected = {
        "Y14_n16_original": (16, 2500.0, 4000.0, 67, -138.229, 45.0, False),
        "Y14_n24_original": (24, 2500.0, 4000.0, 67, -207.350, 50.0, False),
        "Y14_n48_original": (48, 5000.0, 4000.0, 91, -390.158, 50.0, False),
        "Y14_n16_adjusted": (16, 2500.0, 4000.0, 67, -144.726, 43.380, True),
        "Y14_n24_adjusted": (24, 2500.0, 4000.0, 67, -212.741, 47.500, True),
        "Y14_n48_adjusted": (48, 5000.0, 4000.0, 91, -403.033, 47.700, True),
    }
    for case in CASES:
        payload = call(binary, "--action", "evaluate-reference", "--case", case)
        metadata = payload["metadata"]
        observed = (
            metadata["turbine_count"], metadata["length_m"],
            metadata["width_m"], metadata["receiver_count"],
            metadata["reference_negative_aep_gwh"],
            metadata["reference_spl_db"], metadata["adjusted_preference"],
        )
        assert observed == expected[case], (case, observed)
        assert len(payload["layout"]) == expected[case][0]
        assert payload["evaluation"]["feasible"] is True
        assert payload["evaluation"]["aep_gwh"] > 0.0
        assert 20.0 < payload["evaluation"]["spl_db"] < 80.0
        assert len(metadata["wind_probabilities"]) == 16
        assert math.isclose(sum(metadata["wind_probabilities"]), 1.0, abs_tol=1e-14)
        assert math.isclose(
            metadata["minimum_spacing_m"], math.sqrt(8.0)*56.5,
            rel_tol=0.0, abs_tol=1e-12,
        )
    with tempfile.TemporaryDirectory(prefix="core99-y14-") as directory:
        outputs = []
        for workers in (1, 4):
            output = Path(directory) / f"w{workers}.json"
            completed = subprocess.run(
                [str(binary), "--case", "Y14_n16_original", "--seed", "141409",
                 "--workers", str(workers), "--maximum-evaluation-slots", "100",
                 "--output", str(output)],
                check=True, capture_output=True, text=True,
            )
            assert completed.returncode == 0
            outputs.append(json.loads(output.read_text(encoding="utf-8")))
        serial, parallel = outputs
        for key in (
            "method_semantic_id", "problem_semantic_id", "protocol_semantic_id",
            "generations", "nominal_evaluation_slots", "physical_fes",
            "scientific_hash", "front",
        ):
            assert serial[key] == parallel[key], key
        assert serial["nominal_evaluation_slots"] == 100
        assert 50 <= serial["physical_fes"] <= 100
        assert parallel["observed_workers"] >= 2
    print("Y14 independent H5 validation passed")


if __name__ == "__main__":
    main()
