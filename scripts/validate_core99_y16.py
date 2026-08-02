#!/usr/bin/env python3
"""
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: independent Y16 role, equation and schedule validator
Paper DOI: 10.1109/TSTE.2026.3686029
First-party supporting patent: CN121683298A/CN121683298B
Public asset, missing information, conflicts, corrections, reconstruction,
semantic IDs, production backend, controlling contract and claim boundary:
hpc/core99_cpp/include/core99/huang_y16.hpp
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


METHOD = "y16_imm_bmm_bounded_dinkelbach_highs_reconstruction_v1"
PROBLEM = "y16_regular_seabed_ti_lcoe_figure_proxy_v1"
PROTOCOL = "y16_native_31role_deterministic_v1"


def call(binary: Path, *arguments: str) -> dict | list:
    completed = subprocess.run(
        [str(binary), *arguments], check=True, capture_output=True, text=True
    )
    return json.loads(completed.stdout)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", type=Path, required=True)
    arguments = parser.parse_args()
    binary = arguments.binary.resolve()
    cases = call(binary, "--action", "list-cases")
    assert isinstance(cases, list) and len(cases) == 31
    assert sum(case["model"] == "bmm" for case in cases) == 9
    assert sum(case["model"] == "imm" for case in cases) == 22
    assert {case["objective"] for case in cases} == {
        "minimum_lcoe", "minimum_annual_cost", "maximum_aep",
        "minimum_capital_lcoe",
    }
    case = next(
        item for item in cases
        if item["case_id"] == "Y16_case1_type3_n40_g2p5_imm_ac_i3"
    )
    assert case["rotor_diameter_m"] == 155
    assert case["turbine_count"] == 40
    assert case["grid_spacing_diameters"] == 2.5
    common = (
        "--case", case["case_id"], "--angle-start", "1",
        "--angle-count", "2", "--pattern-start", "0",
        "--pattern-count", "1", "--mip-time-limit-seconds", "30",
    )
    serial = call(binary, *common, "--workers", "1")
    parallel = call(binary, *common, "--workers", "4")
    for payload in (serial, parallel):
        assert payload["method_semantic_id"] == METHOD
        assert payload["problem_semantic_id"] == PROBLEM
        assert payload["protocol_semantic_id"] == PROTOCOL
        assert payload["evaluation"]["feasible"] is True
        assert len(payload["layout"]) == 40
        assert payload["evaluation"]["annual_energy_mwh"] > 0.0
        assert payload["evaluation"]["minimum_spacing_m"] >= 775.0-1e-5
        assert payload["evaluator_rejected_subproblems"] == 0
        assert math.isfinite(payload["evaluation"]["annual_cost_cny"])
    for key in (
        "scientific_hash", "selected_angle_degrees", "selected_pattern",
        "layout", "evaluation",
    ):
        assert serial[key] == parallel[key], key
    assert parallel["observed_workers"] >= 2
    print("Y16 independent H5 validation passed")


if __name__ == "__main__":
    main()
