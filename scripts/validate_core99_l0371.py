#!/usr/bin/env python3
"""
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: independent L0371 H5 case, grid, scale, FES, all-core
precomputation and deterministic replay validator
Paper/DOI: Guo et al.; 10.1016/j.jweia.2021.104548
Source/reconstruction/claim:
hpc/core99_cpp/include/core99/guo_l0371.hpp
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import subprocess


EXPECTED_FIXTURE_SHA256 = (
    "e327d31b787bdabdbdb5c820e452ca9d7f970be48cd183d245d009aa3d9ae58e"
)


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


def call(binary: str, arguments: list[str]) -> dict:
    completed = subprocess.run(
        [binary, *arguments],
        check=True,
        text=True,
        capture_output=True,
    )
    return json.loads(completed.stdout)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", required=True)
    parser.add_argument("--data", type=Path, required=True)
    args = parser.parse_args()
    require(
        hashlib.sha256(args.data.read_bytes()).hexdigest()
        == EXPECTED_FIXTURE_SHA256,
        "L0371 fixture SHA-256 mismatch",
    )
    cases = call(
        args.binary,
        ["--mode", "list-cases", "--data", str(args.data)],
    )["paper_case_ids"]
    require(len(cases) == 29, "paper-native case registry is incomplete")
    require(len(set(cases)) == 29, "paper-native case IDs are not unique")

    ideal = call(
        args.binary,
        [
            "--mode", "inspect",
            "--case", "l0371_ideal_a_n",
            "--data", str(args.data),
            "--workers", "4",
        ],
    )
    require(
        ideal["candidate_count"] == 100
        and ideal["turbine_count"] == 30
        and ideal["state_count"] == 1
        and ideal["minimum_spacing_m"] == 200.0,
        "ideal case contract mismatch",
    )
    require(
        ideal["observed_precomputation_workers"] >= 2,
        "ideal wake-table precomputation did not use multiple workers",
    )
    layout = ",".join(
        str(y * 10 + x) for x in (0, 4, 9) for y in range(10)
    )
    oracle = call(
        args.binary,
        [
            "--mode", "evaluate",
            "--case", "l0371_ideal_a_n",
            "--data", str(args.data),
            "--indices", layout,
            "--workers", "4",
        ],
    )["evaluation"]
    require(
        oracle["feasible"]
        and 13000.0 < oracle["average_power_kw"] < 15552.0
        and 0.8 < oracle["efficiency"] < 1.0,
        "ideal paper-scale scalar oracle failed",
    )

    actual = call(
        args.binary,
        [
            "--mode", "inspect",
            "--case", "l0371_horns_actual",
            "--data", str(args.data),
            "--workers", "4",
        ],
    )
    require(
        actual["candidate_count"] == 531
        and actual["turbine_count"] == 80
        and actual["state_count"] == 504
        and actual["minimum_spacing_m"] == 400.0,
        "Horns actual case contract mismatch",
    )
    require(
        actual["observed_precomputation_workers"] >= 2,
        "Horns wake-table precomputation did not use multiple workers",
    )

    common = [
        "--mode", "optimize",
        "--case", "l0371_ideal_c_vs",
        "--data", str(args.data),
        "--max-physical-fes", "500",
        "--seed", "371371",
    ]
    serial = call(args.binary, [*common, "--workers", "1"])
    parallel = call(args.binary, [*common, "--workers", "4"])
    replay = call(args.binary, [*common, "--workers", "4"])
    require(
        serial["physical_fes"]
        == parallel["physical_fes"]
        == replay["physical_fes"]
        == 500,
        "physical-FES accounting mismatch",
    )
    require(
        serial["scientific_hash"]
        == parallel["scientific_hash"]
        == replay["scientific_hash"],
        "serial/multicore/replay scientific parity failed",
    )
    require(
        parallel["best_evaluation"]["feasible"]
        and parallel["best_evaluation"]["average_power_kw"]
        >= parallel["initial_evaluation"]["average_power_kw"],
        "DEEM smoke optimization failed",
    )
    report = {
        "status": "pass",
        "fixture_sha256": EXPECTED_FIXTURE_SHA256,
        "paper_case_count": len(cases),
        "ideal_candidate_count": ideal["candidate_count"],
        "horns_candidate_count": actual["candidate_count"],
        "horns_actual_state_count": actual["state_count"],
        "paper_scale_neutral_case_a_power_kw": oracle["average_power_kw"],
        "paper_scale_neutral_case_a_efficiency": oracle["efficiency"],
        "smoke_physical_fes": parallel["physical_fes"],
        "serial_parallel_replay_scientific_parity": True,
        "smoke_scientific_hash": parallel["scientific_hash"],
    }
    print(json.dumps(report, sort_keys=True))


if __name__ == "__main__":
    main()
