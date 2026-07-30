#!/usr/bin/env python3
"""
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: independent L0124 Gaussian equation, Fig. 6, Table 4,
constraint, MI-LXPM lifecycle, multicore-parity and replay H5 validator
Paper/DOI: Wind farm layout optimization using a Gaussian-based wake model;
10.1016/j.renene.2017.02.017
Public source/missing/reconstruction:
hpc/core99_cpp/include/core99/parada_l0124.hpp
Controlling contract: shared/contracts/core99_l0124_parada_2017.json
Claim boundary: academic declared reproduction; not author-source, author
random-state, or exact-layout replay
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
"""
from __future__ import annotations

import argparse
import json
import math
import subprocess


def call(binary: str, arguments: list[str]) -> dict:
    completed = subprocess.run(
        [binary, *arguments],
        check=True,
        text=True,
        capture_output=True,
    )
    return json.loads(completed.stdout)


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


def spaced_layout(grid: int, turbines: int, stride: int) -> str:
    coordinates: list[int] = []
    for y_coord in range(0, grid, stride):
        for x_coord in range(0, grid, stride):
            if len(coordinates) >= 2 * turbines:
                break
            coordinates.extend([x_coord, y_coord])
        if len(coordinates) >= 2 * turbines:
            break
    require(len(coordinates) == 2 * turbines, "validator layout is incomplete")
    return ",".join(str(value) for value in coordinates)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", required=True)
    args = parser.parse_args()

    downstream = 200.0
    rotor_diameter = 40.0
    thrust_coefficient = 0.88
    wake_growth = 0.055
    root = math.sqrt(1.0 - thrust_coefficient)
    beta = 0.5 * (1.0 + root) / root
    epsilon = 0.2 * math.sqrt(beta)
    sigma_ratio = (
        wake_growth * downstream / rotor_diameter + epsilon
    )
    gaussian_oracle = 1.0 - math.sqrt(
        1.0 - thrust_coefficient / (8.0 * sigma_ratio * sigma_ratio)
    )
    gaussian = call(
        args.binary,
        [
            "--mode", "gaussian",
            "--downstream-m", str(downstream),
            "--crosswind-m", "0",
        ],
    )["deficit_ratio"]
    require(
        abs(gaussian - gaussian_oracle) < 1.0e-14,
        "Gaussian centreline equation mismatch",
    )

    cases = {
        "l0124_case_a_grid10": (10, 30, 1, 15552.0),
        "l0124_case_a_grid20": (20, 30, 2, 15552.0),
        "l0124_case_b_grid10": (10, 39, 1, 20217.6),
        "l0124_case_b_grid20": (20, 39, 2, 20217.6),
        "l0124_case_c_grid10": (10, 39, 1, 36506.1879),
        "l0124_case_c_grid20": (20, 39, 2, 36506.1879),
    }
    evaluations: dict[str, dict] = {}
    for case_id, (grid, turbines, stride, expected_no_wake) in cases.items():
        payload = call(
            args.binary,
            [
                "--mode", "evaluate",
                "--problem", case_id,
                "--coordinates", spaced_layout(grid, turbines, stride),
            ],
        )
        require(
            payload["problem_semantic_id"]
            == "l0124_parada_gaussian_grid_v1",
            f"{case_id}: problem semantic ID mismatch",
        )
        require(
            abs(payload["no_wake_power_kw"] - expected_no_wake) < 1.0e-6,
            f"{case_id}: no-wake power identity mismatch",
        )
        evaluation = payload["evaluation"]
        require(evaluation["feasible"], f"{case_id}: oracle layout infeasible")
        require(
            0.0 < evaluation["efficiency"] <= 1.0,
            f"{case_id}: physical efficiency bound violated",
        )
        evaluations[case_id] = evaluation

    table_4_no_wake_kw = 34338.0 / 0.9407
    case_c_no_wake_kw = 36506.1879
    table_4_relative_error = (
        abs(case_c_no_wake_kw - table_4_no_wake_kw)
        / table_4_no_wake_kw
    )
    require(
        table_4_relative_error < 1.0e-4,
        "Fig. 6 digitization does not close against Table 4",
    )

    common = [
        "--problem", "l0124_case_a_grid10",
        "--population", "60",
        "--generations", "5",
        "--seed", "120124",
    ]
    serial = call(args.binary, [*common, "--workers", "1"])
    parallel = call(args.binary, [*common, "--workers", "4"])
    replay = call(args.binary, [*common, "--workers", "4"])
    require(
        serial["method_semantic_id"]
        == "l0124_mi_lxpm_target_survival_completed_v1",
        "MI-LXPM semantic ID mismatch",
    )
    require(
        serial["physical_fes"] == 360
        and parallel["physical_fes"] == 360,
        "MI-LXPM physical FES mismatch",
    )
    require(
        serial["scientific_hash"] == parallel["scientific_hash"]
        == replay["scientific_hash"],
        "one-worker/multicore/replay scientific parity failed",
    )
    require(
        parallel["observed_workers"] >= 2,
        "no persistent-team multicore evidence",
    )
    require(
        parallel["best_evaluation"]["feasible"],
        "smoke MI-LXPM did not retain a feasible solution",
    )

    report = {
        "status": "pass",
        "problem_semantic_id": "l0124_parada_gaussian_grid_v1",
        "method_semantic_id": "l0124_mi_lxpm_target_survival_completed_v1",
        "gaussian_centreline_absolute_error":
            abs(gaussian - gaussian_oracle),
        "validated_paper_case_count": len(cases),
        "case_c_figure6_no_wake_power_kw": case_c_no_wake_kw,
        "case_c_table4_implied_no_wake_power_kw": table_4_no_wake_kw,
        "case_c_table4_relative_error": table_4_relative_error,
        "serial_parallel_replay_scientific_parity": True,
        "smoke_physical_fes": parallel["physical_fes"],
        "observed_inner_workers": parallel["observed_workers"],
        "smoke_scientific_hash": parallel["scientific_hash"],
        "case_efficiencies": {
            case_id: value["efficiency"]
            for case_id, value in evaluations.items()
        },
    }
    print(json.dumps(report, sort_keys=True))


if __name__ == "__main__":
    main()
