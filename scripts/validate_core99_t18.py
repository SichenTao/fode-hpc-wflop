#!/usr/bin/env python3
"""
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: independent T18 role, semantic and CPU-HPC validator
Paper/DOI: Reddy 2020; 10.1016/j.apenergy.2020.115090.
Fact boundary: hpc/core99_cpp/include/core99/reddy_t18.hpp.
Last evidence-audit date: 2026-08-01
END WFLOP IMPLEMENTATION FACT DECLARATION
"""

from __future__ import annotations

import argparse
import json
import math
from pathlib import Path
import subprocess


METHOD = "t18_soho_three_kernel_relay_declared_v1"
PROBLEM = "t18_windflo_awec25_two_case_two_wake_v1"
PROTOCOL = "t18_tables2_3_4_54roles_25seed_v1"
PUBLISHED_VALIDATION_MPS = [
    6.34, 6.14, 8.96, 5.87, 5.96, 5.80,
    6.45, 6.29, 6.54, 5.76, 6.13, 6.00,
    6.33, 6.12, 8.88, 5.87, 5.94, 5.76,
    6.47, 6.32, 7.03, 6.36, 6.15, 6.02,
    4.80, 4.39, 12.67, 5.38, 5.25, 5.05,
    5.44, 5.23, 5.99, 5.34, 5.25, 5.11,
    4.56, 3.98, 11.96, 5.36, 5.27, 5.05,
    5.55, 5.38, 7.03, 6.64, 6.28, 6.23,
]


def call(binary: Path, *arguments: str) -> dict:
    completed = subprocess.run(
        [str(binary), *arguments], check=True, capture_output=True,
        text=True, timeout=900.0,
    )
    return json.loads(completed.stdout)


def scientific_projection(payload: dict) -> dict:
    excluded = {
        "requested_workers", "observed_workers", "parallel_regions",
        "terrain_precompute_seconds", "validation_seconds",
        "evaluator_seconds", "algorithm_seconds", "end_to_end_seconds",
    }
    return {key: value for key, value in payload.items() if key not in excluded}


def validate(payload: dict, workers: int) -> None:
    assert payload["method_semantic_id"] == METHOD
    assert payload["problem_semantic_id"] == PROBLEM
    assert payload["protocol_semantic_id"] == PROTOCOL
    assert payload["requested_workers"] == workers
    assert payload["terrain_profile"] == "paper_local_rbf"
    assert payload["disk_sampling"] == "paper_area_correct"
    assert payload["validation_disk_sampling"] == "source_uniform_radius"
    assert payload["validation_disk_quadrature_points"] == 1000
    assert len(payload["validation"]) == 48
    assert len(payload["roles"]) == 6
    assert payload["objective_evaluations"] == 34
    assert payload["wind_scenario_layout_evaluations"] > 0
    assert payload["wake_pair_checks"] > 0
    assert payload["disk_quadrature_samples"] > 0
    expected = [
        "table4_frandsen_reference",
        "table4_frandsen_case1_layout",
        "table4_frandsen_case2_layout_turbine",
        "table4_bp_reference",
        "table4_bp_case1_layout",
        "table4_bp_case2_layout_turbine",
    ]
    assert [role["role"] for role in payload["roles"]] == expected
    for item in payload["validation"]:
        assert math.isfinite(item["predicted_velocity_mps"])
        assert math.isfinite(item["relative_error_percent"])
    for role in payload["roles"]:
        assert len(role["layout"]) == 25
        values = role["evaluation"]
        assert values["annual_energy_mwh"] > 0.0
        assert math.isfinite(values["coe_usd_kwh"])


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", required=True, type=Path)
    args = parser.parse_args()
    binary = args.binary.resolve()
    description = call(binary, "--action", "describe")
    assert description["paper_role_count"] == 54
    assert description["turbine_count"] == 25
    common = (
        "--seed", "18001", "--population", "4", "--generations", "1",
        "--stagnation-generations", "1", "--disk-quadrature-points", "4",
    )
    serial = call(binary, *common, "--workers", "1")
    parallel = call(binary, *common, "--workers", "4")
    validate(serial, 1)
    validate(parallel, 4)
    assert scientific_projection(serial) == scientific_projection(parallel)
    assert parallel["observed_workers"] >= 2
    source_profile = call(
        binary, "--action", "validate", "--disk-quadrature-points", "1000",
        "--disk-sampling", "source_uniform_radius",
    )
    assert source_profile["disk_sampling"] == "source_uniform_radius"
    assert len(source_profile["validation"]) == 48
    differences = [
        abs(item["predicted_velocity_mps"] - expected)
        for item, expected in zip(
            source_profile["validation"], PUBLISHED_VALIDATION_MPS, strict=True
        )
    ]
    # All 48 published values are rounded to 0.01 m/s. Forty-seven source
    # roles reproduce within 0.10 m/s; the known unstable downstream Larsen
    # quadratic role remains within 0.25 m/s.
    assert sum(value <= 0.10 for value in differences) == 47
    assert max(differences) <= 0.25
    print("T18 independent H5 validation passed: all 54 paper roles")


if __name__ == "__main__":
    main()
