#!/usr/bin/env python3
"""
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: independent T83 case, anchor, constraint and HPC validator
Paper DOI: 10.1016/j.apenergy.2022.118830
Public source: target article is CC BY; no target source/UK arrays found. The
cited T31 paper and official DOI 10.11583/DTU.13134731 data are consumed.
Independence: this validator fixes all eight Table-1 roles and anchors, the
100-turbine tender constraints, the macro/meso/micro lifecycle, one/all-worker
fixed-work identity and explicit same-lineage-proxy metadata through JSON.
Missing information and completion: hpc/core99_cpp/include/core99/cazzaro_t83.hpp
Claim boundary: academic reconstruction validation, not author-array replay.
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
"""

from __future__ import annotations

import argparse
import json
import math
from pathlib import Path
import subprocess


METHOD = "t83_macro_meso_random_conic_vns_declared_v1"
PROBLEM = "t83_round4_eightseed_same_lineage_proxy_v1"
PROTOCOL = "t83_native_8seed_shape_rectangle_30min_v1"
TABLE = {
    "A": (54.60, 1.55, "Dogger Bank", 4734.0, 4.98, 3.11, 4705.9),
    "B": (54.40, 2.10, "Dogger Bank", 4702.3, 4.97, 3.11, 4675.2),
    "C": (53.51, 1.40, "Eastern", 4504.9, 4.98, 3.10, 4501.4),
    "D": (52.20, 2.00, "Eastern", 4021.6, 4.36, 3.13, 3955.7),
    "E": (50.75, 0.85, "South East", 4045.0, 4.73, 3.08, 4019.6),
    "F": (50.65, 0.40, "South East", 3987.8, 4.77, 3.06, 3929.7),
    "G": (53.65, -3.28, "Northern Wales", 4044.3, 4.71, 3.14, 3991.9),
    "H": (54.50, -3.75, "Northern Wales", 4040.6, 4.60, 3.14, 3932.0),
}


def invoke(binary: Path, *arguments: str) -> dict:
    completed = subprocess.run(
        [str(binary), *arguments], check=True, capture_output=True, text=True
    )
    return json.loads(completed.stdout)


def close(left: float, right: float, tolerance: float = 1.0e-9) -> bool:
    return abs(left - right) <= tolerance * max(1.0, abs(left), abs(right))


def validate_result(payload: dict, role: str, workers: int) -> None:
    assert payload["case_id"] == f"t83_seed_{role}"
    assert payload["method_semantic_id"] == METHOD
    assert payload["problem_semantic_id"] == PROBLEM
    assert payload["protocol_semantic_id"] == PROTOCOL
    assert payload["requested_workers"] == workers
    assert payload["observed_workers"] >= min(2, workers)
    assert payload["turbines"] == 100
    assert payload["source_candidate_positions"] > payload["hpc_candidate_positions"]
    assert payload["hpc_candidate_positions"] >= 100
    assert payload["macro_rectangles_evaluated"] == 30
    assert close(payload["macro_rectangle"]["npv_meur"], TABLE[role][-1])
    for key in ("meso_shape", "optimized_shape", "optimized_rectangle"):
        evaluation = payload[key]
        assert evaluation["feasible"] is True
        assert evaluation["minimum_spacing_m"] >= 1200.0 - 1.0e-6
        assert evaluation["area_km2"] <= 500.0 + 1.0e-9
        assert evaluation["perimeter_to_sqrt_area"] <= 5.0 + 1.0e-9
        assert evaluation["density_mw_km2"] >= 3.0 - 1.0e-9
        assert math.isfinite(evaluation["npv_meur"])
    assert len(payload["meso_positions"]) == 100
    assert len(payload["optimized_shape_positions"]) == 100
    assert len(payload["optimized_rectangle_positions"]) == 100


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--data-root", type=Path, required=True)
    arguments = parser.parse_args()
    binary = arguments.binary.resolve()
    data_root = arguments.data_root.resolve()

    listing = invoke(binary, "--mode", "list-cases")
    assert listing["paper_case_count"] == 8
    assert listing["formal_design_runs"] == 16
    assert [item["seed_role"] for item in listing["paper_cases"]] == list(TABLE)
    for item in listing["paper_cases"]:
        role = item["seed_role"]
        expected = TABLE[role]
        assert close(item["latitude_deg"], expected[0])
        assert close(item["longitude_deg"], expected[1])
        assert item["region"] == expected[2]
        assert close(item["paper_shape_npv_meur"], expected[3])
        assert close(item["paper_pta"], expected[4])
        assert close(item["paper_density_mw_km2"], expected[5])
        assert close(item["paper_rectangle_npv_meur"], expected[6])

    common = (
        "--mode", "optimize", "--data-root", str(data_root), "--case", "A",
        "--seed", "83017", "--micro-seconds", "0", "--micro-cycles", "2",
        "--macro-cell-axis", "1",
    )
    serial = invoke(binary, *common, "--workers", "1")
    parallel = invoke(binary, *common, "--workers", "4")
    validate_result(serial, "A", 1)
    validate_result(parallel, "A", 4)
    for key in (
        "macro_rectangle", "meso_shape", "optimized_shape",
        "optimized_rectangle", "meso_positions", "optimized_shape_positions",
        "optimized_rectangle_positions", "scientific_hash",
    ):
        assert serial[key] == parallel[key], f"schedule mismatch: {key}"

    print(json.dumps({
        "status": "pass",
        "paper_case_roles": 8,
        "paper_design_roles": 16,
        "table_1_anchors": True,
        "tender_constraints": True,
        "schedule_independent": True,
        "scientific_hash": parallel["scientific_hash"],
    }, sort_keys=True))


if __name__ == "__main__":
    main()
