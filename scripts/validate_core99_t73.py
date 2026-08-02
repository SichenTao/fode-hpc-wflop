#!/usr/bin/env python3
"""
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: independent T73 role, semantic and HPC validator
Paper/DOI: Song et al.; 10.1016/j.cie.2018.04.051.
Public assets, missing fields, conflicts, reconstruction and claim boundary:
hpc/core99_cpp/include/core99/song_t73.hpp.
Last evidence-audit date: 2026-08-01
END WFLOP IMPLEMENTATION FACT DECLARATION
"""

from __future__ import annotations

import argparse
import json
import math
from pathlib import Path
import subprocess


METHOD = "t73_ga_pattern_kmeans_ocbm_declared_v1"
PROBLEM = "t73_nj342_layout_maintenance_declared_v1"
PROTOCOL = "t73_table3_table5_12roles_25seed_v1"
INTERVALS = [200, 250, 332, 350, 400]


def call(binary: Path, *arguments: str) -> dict:
    completed = subprocess.run(
        [str(binary), *arguments], check=True, capture_output=True,
        text=True, timeout=600.0,
    )
    return json.loads(completed.stdout)


def scientific_projection(payload: dict) -> dict:
    excluded = {
        "requested_workers", "observed_workers", "parallel_regions",
        "scenario_precompute_seconds", "layout_evaluator_seconds",
        "algorithm_seconds", "maintenance_simulation_seconds",
        "end_to_end_seconds",
    }
    return {key: value for key, value in payload.items() if key not in excluded}


def validate_payload(payload: dict, workers: int, clusters: int) -> None:
    assert payload["method_semantic_id"] == METHOD
    assert payload["problem_semantic_id"] == PROBLEM
    assert payload["protocol_semantic_id"] == PROTOCOL
    assert payload["requested_workers"] == workers
    assert payload["cluster_count"] == clusters
    assert len(payload["roles"]) == 12
    assert len(payload["discrete_layout"]) > 0
    assert len(payload["continuous_layout"]) == len(payload["discrete_layout"])
    assert len(payload["cluster_assignment"]) == len(payload["continuous_layout"])
    assert set(payload["cluster_assignment"]) == set(range(clusters))
    assert payload["layout_evaluations"] == 36
    assert payload["wind_scenario_turbine_evaluations"] > 0
    assert payload["component_life_events"] > 0
    expected_roles = [
        "table3_discrete_premaintenance",
        "table3_continuous_premaintenance",
        *[f"table5_discrete_V{value}" for value in INTERVALS],
        *[f"table5_continuous_V{value}" for value in INTERVALS],
    ]
    assert [role["role"] for role in payload["roles"]] == expected_roles
    for role in payload["roles"]:
        layout = role["layout"]
        assert layout["feasible"] is True
        assert layout["turbine_count"] == len(payload["discrete_layout"])
        assert layout["minimum_spacing_m"] >= 100.0
        assert layout["annual_energy_mwh"] > 0.0
        assert math.isfinite(role["integrated_profit_usd"])
    for role in payload["roles"][2:]:
        maintenance = role["maintenance"]
        assert maintenance["inspection_interval_days"] in INTERVALS
        assert maintenance["mean_cost_usd"] > 0.0
        assert maintenance["inspection_cost_usd"] > 0.0


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", required=True, type=Path)
    args = parser.parse_args()
    binary = args.binary.resolve()
    description = call(binary, "--action", "describe")
    assert description == {
        "case_id": "nj342",
        "candidate_count": 342,
        "wind_samples": 40,
        "paper_role_count": 12,
        "inspection_intervals": INTERVALS,
        "cluster_profiles": ["equation_text_four", "figure_caption_two"],
    }
    common = (
        "--seed", "73001", "--ga-population", "8",
        "--ga-generations", "1", "--pattern-iterations", "1",
        "--maintenance-replications", "5",
    )
    serial = call(binary, *common, "--workers", "1")
    parallel = call(binary, *common, "--workers", "4")
    validate_payload(serial, 1, 4)
    validate_payload(parallel, 4, 4)
    assert scientific_projection(serial) == scientific_projection(parallel)
    assert parallel["observed_workers"] >= 2
    conflict = call(
        binary, *common, "--workers", "4",
        "--cluster-profile", "figure_caption_two",
    )
    validate_payload(conflict, 4, 2)
    print("T73 independent H5 validation passed: all 12 paper roles")


if __name__ == "__main__":
    main()
