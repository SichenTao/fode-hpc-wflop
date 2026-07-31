#!/usr/bin/env python3
"""
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: independent L0298 semantic, paper-role and HPC validator
Paper/DOI: 10.1109/TSG.2020.3022378.
Public assets, missing fields, conflicts, reconstruction, semantic IDs and
claim boundary: hpc/core99_cpp/include/core99/tao_l0298.hpp.
Last evidence-audit date: 2026-08-01
END WFLOP IMPLEMENTATION FACT DECLARATION
"""

from __future__ import annotations

import argparse
import json
import math
from pathlib import Path
import subprocess


METHOD = "l0298_yarpiz_nsga3_bpso_qp_declared_v1"
PROBLEM = "l0298_offshore_grid_cable_rts24_declared_v1"
PROTOCOL = "l0298_models_turbines_seasons_buses_29roles_v1"
PROFILES = {
    "models-winter-e82-bus3": 7,
    "model1-winter-e115-bus3": 2,
    "model1-winter-ltw101-bus3": 2,
    "model1-summer-e82-bus3": 3,
    "model1-winter-e82-bus5": 3,
    "model1-winter-e82-bus7": 3,
    "model1-winter-e82-bus16": 3,
    "model1-winter-e82-bus21": 3,
    "model1-winter-e82-bus23": 3,
}


def call(binary: Path, *arguments: str) -> dict | list:
    completed = subprocess.run(
        [str(binary), *arguments], check=True, capture_output=True, text=True,
        timeout=600.0,
    )
    return json.loads(completed.stdout)


def validate_payload(payload: dict, profile: str, workers: int) -> None:
    assert payload["profile_id"] == profile
    assert payload["method_semantic_id"] == METHOD
    assert payload["problem_semantic_id"] == PROBLEM
    assert payload["protocol_semantic_id"] == PROTOCOL
    assert payload["requested_workers"] == workers
    assert len(payload["roles"]) == PROFILES[profile]
    assert payload["complete_outer_evaluations"] > len(payload["roles"])
    assert payload["cable_particle_evaluations"] > 0
    assert payload["hourly_wake_evaluations"] \
        == 24 * payload["complete_outer_evaluations"]
    for role in payload["roles"]:
        evaluation = role["evaluation"]
        assert evaluation["feasible"] is True
        assert 60 <= evaluation["turbine_count"] <= 80
        assert evaluation["installed_capacity_mw"] \
            == 3 * evaluation["turbine_count"]
        assert len(role["active_cells"]) == evaluation["turbine_count"]
        assert role["active_cells"] == sorted(set(role["active_cells"]))
        assert len(role["cable_edges"]) == evaluation["turbine_count"]
        assert math.isfinite(evaluation["profit_rate_percent"])
        assert 0 < evaluation["capacity_factor_percent"] <= 100
        assert 0 <= evaluation["variability_percent"] <= 100
        assert evaluation["cable_daily_cost_eur"] > 0
        assert evaluation["cable_length_m"] > 0
        for edge in role["cable_edges"]:
            assert 1 <= edge["cable_type"] <= 5
            assert edge["flow_mw"] > 0
            assert edge["length_m"] > 0
    if profile == "models-winter-e82-bus3":
        expected = [
            "model1_best_profit", "model1_best_capacity_factor",
            "model1_best_variability", "model2_best_profit",
            "model2_best_capacity_factor", "model2_best_variability",
            "model3_best_profit",
        ]
        assert [item["role"] for item in payload["roles"]] == expected
        assert all(
            item["evaluation"]["turbine_count"] == 70
            for item in payload["roles"][3:6]
        )


def scientific_projection(payload: dict) -> dict:
    return {
        key: payload[key] for key in (
            "profile_id", "method_semantic_id", "problem_semantic_id",
            "protocol_semantic_id", "seed", "outer_population",
            "outer_iterations", "inner_population", "inner_iterations",
            "complete_outer_evaluations", "cable_particle_evaluations",
            "hourly_wake_evaluations", "scientific_hash", "roles",
        )
    }


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", required=True, type=Path)
    arguments = parser.parse_args()
    binary = arguments.binary.resolve()
    listed = call(binary, "--action", "list-profiles")
    assert isinstance(listed, list)
    assert {item["profile"]: item["paper_role_count"] for item in listed} \
        == PROFILES
    assert sum(PROFILES.values()) == 29

    common = (
        "--seed", "29801", "--outer-population", "8",
        "--outer-iterations", "1", "--inner-population", "4",
        "--inner-iterations", "1",
    )
    serial = call(
        binary, "--profile", "models-winter-e82-bus3",
        *common, "--workers", "1",
    )
    parallel = call(
        binary, "--profile", "models-winter-e82-bus3",
        *common, "--workers", "4",
    )
    assert isinstance(serial, dict) and isinstance(parallel, dict)
    validate_payload(serial, "models-winter-e82-bus3", 1)
    validate_payload(parallel, "models-winter-e82-bus3", 4)
    assert scientific_projection(serial) == scientific_projection(parallel)
    assert parallel["observed_workers"] >= 2

    role_count = len(parallel["roles"])
    for profile in list(PROFILES)[1:]:
        payload = call(
            binary, "--profile", profile, *common, "--workers", "4",
        )
        assert isinstance(payload, dict)
        validate_payload(payload, profile, 4)
        role_count += len(payload["roles"])
    assert role_count == 29
    print("L0298 independent H5 validation passed: all 29 paper roles")


if __name__ == "__main__":
    main()
