#!/usr/bin/env python3
"""
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: independent L0373 role, receipt and HPC validator
Paper DOI: 10.1016/j.renene.2021.10.032
Public paper source: arXiv 2107.11620 source archive.
Cited public dependency: FLORISSE_M, MIT, commit
36cb0a0295d2a1e05640fdbbcb9bb361ac8d592e.
Public-source search, missing information, conflicts, corrections, declared
reconstruction, semantic IDs, backend, contract and claim boundary:
hpc/core99_cpp/include/core99/chen_l0373.hpp
Claim boundary: flexible academic reconstruction, not author numerical replay.
Last evidence-audit date: 2026-08-01
END WFLOP IMPLEMENTATION FACT DECLARATION
"""

from __future__ import annotations

import argparse
import json
import math
from pathlib import Path
import subprocess


METHOD = "l0373_pso_warm_dbhm_projected_declared_v1"
PROBLEM = "l0373_joint_layout_yaw_induction_floris_declared_v1"
PROTOCOL = "l0373_native_illustrative_16_36_360_80_12_180_v1"


def call(binary: Path, *arguments: str) -> dict | list:
    completed = subprocess.run(
        [str(binary), *arguments], check=True, capture_output=True, text=True
    )
    return json.loads(completed.stdout)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", required=True, type=Path)
    arguments = parser.parse_args()
    binary = arguments.binary.resolve()

    profiles = call(binary, "--action", "list-profiles")
    assert isinstance(profiles, list) and len(profiles) == 6
    assert [item["profile"] for item in profiles] == [
        "illustrative-unrestricted", "illustrative-4d", "n16-w36",
        "n16-w360", "n80-w12", "n80-w180",
    ]
    assert [item["wind_scenarios"] for item in profiles] == [1, 1, 36, 360, 12, 180]
    assert [item["turbines"] for item in profiles] == [3, 3, 16, 16, 80, 80]
    assert profiles[2]["paper_aep_anchors_gwh"] == [
        366.52, 373.96, 376.24, 386.49, 402.96,
    ]
    assert profiles[0]["paper_middle_position_anchors_m"] == [800, 470]
    assert profiles[1]["paper_middle_position_anchors_m"] == [596, 504]
    assert profiles[2]["paper_computation_time_anchors_seconds"] == {
        "isolated_pso": 14443.98,
        "joint_scp": 16006.16,
        "joint_pso": 94064.74,
        "joint_dbhm": 12969.60,
        "dbhm_iterations": 11,
    }
    assert profiles[4]["minimum_spacing_m"] == 504
    assert profiles[4]["paper_computation_time_anchors_seconds"] == {
        "isolated_pso": 300629,
        "joint_scp": 280220,
        "joint_pso_lower_bound": 864000,
        "joint_dbhm": 314188,
    }

    common = (
        "--profile", "n16-w36", "--seed", "37301",
        "--pso-trials", "1", "--pso-population", "8",
        "--pso-iterations", "1", "--control-passes", "1",
        "--dbhm-iterations", "1",
    )
    serial = call(binary, *common, "--workers", "1")
    parallel = call(binary, *common, "--workers", "4")
    expected_roles = [
        "case1_initial_layout_greedy_control",
        "case2_initial_layout_optimized_control",
        "case3_optimized_layout_greedy_control",
        "case4_optimized_layout_sequential_control",
        "case5_joint_layout_control_dbhm",
    ]
    for payload in (serial, parallel):
        assert payload["method_semantic_id"] == METHOD
        assert payload["problem_semantic_id"] == PROBLEM
        assert payload["protocol_semantic_id"] == PROTOCOL
        assert [case["role"] for case in payload["cases"]] == expected_roles
        assert payload["complete_layout_evaluations"] == 17
        assert payload["single_wind_state_evaluations"] > 12_000
        assert payload["dbhm_iterations_completed"] == 1
        for case in payload["cases"]:
            evaluation = case["evaluation"]
            assert evaluation["feasible"] is True
            assert math.isfinite(evaluation["aep_gwh"])
            assert 0.0 < evaluation["efficiency_percent"] <= 100.0 + 1e-10
            assert len(case["layout"]) == 16
            assert len(case["controls_by_wind"]) == 36
        values = [case["evaluation"]["aep_gwh"] for case in payload["cases"]]
        assert values[1] >= values[0]
        assert values[3] >= values[2]
        assert values[4] >= values[3]
    for key in (
        "scientific_hash", "complete_layout_evaluations",
        "single_wind_state_evaluations", "dbhm_iterations_completed",
        "final_consensus_violation_m", "cases",
    ):
        assert serial[key] == parallel[key], key
    assert parallel["observed_workers"] >= 2
    print("L0373 independent H5 validation passed")


if __name__ == "__main__":
    main()
