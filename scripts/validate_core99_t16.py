#!/usr/bin/env python3
"""
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: independent T16 paper-equation, Table-2, lifecycle,
constraint, gradient and replay H5 validator
Paper/DOI: Comparison of Wind Farm Layout Optimization Results Using a
Simple Wake Model and Gradient-Based Optimization to Large Eddy Simulations;
10.2514/6.2019-0538
Public source/missing/reconstruction: hpc/core99_cpp/include/core99/thomas_t16.hpp
Controlling contract: shared/contracts/core99_t16_thomas_2019.json
Claim boundary: academic semantic and numerical validation against the
published BP Table-2 observations; not SNOPT/Tapenade or SOWFA replay
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path
import subprocess


EXPECTED_DATA_SHA256 = (
    "b169391622f3ad7d2e9d6fe2a06b63cb17500372f7e6a68123d280e4808df662"
)
PAPER_BP_BASE_DIRECTIONAL_MW = [
    57.3, 55.5, 57.5, 57.6, 58.4, 56.9,
    56.9, 55.8, 57.7, 56.9, 58.8, 56.4,
]


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


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", required=True)
    parser.add_argument("--data", required=True)
    args = parser.parse_args()
    data = Path(args.data)
    require(data.is_file(), "T16 public-data fixture is absent")
    digest = hashlib.sha256(data.read_bytes()).hexdigest()
    require(digest == EXPECTED_DATA_SHA256, "T16 public-data SHA-256 mismatch")

    common = [
        "--mode", "evaluate",
        "--data", str(data),
        "--start-index", "0",
    ]
    gradient = call(
        args.binary,
        [*common, "--workers", "4", "--gradient"],
    )["evaluation"]
    assessment = call(
        args.binary,
        [
            *common,
            "--workers", "4",
            "--turbulence", "hard",
            "--rotor-points", "100",
        ],
    )["evaluation"]
    serial = call(
        args.binary,
        [
            *common,
            "--workers", "1",
            "--turbulence", "hard",
            "--rotor-points", "100",
        ],
    )["evaluation"]

    require(gradient["observed_workers"] >= 2, "no inner multicore evidence")
    require(
        len(gradient["gradient_gwh_per_m"]) == 76,
        "T16 objective gradient has the wrong dimension",
    )
    require(
        all(math.isfinite(value) for value in gradient["gradient_gwh_per_m"]),
        "T16 objective gradient contains a non-finite value",
    )
    require(
        assessment["maximum_constraint_violation_m"] < 1.0e-8,
        "paper baseline violates the reconstructed constraints",
    )
    require(
        abs(assessment["aep_gwh"] - 481.0) <= 3.0,
        "T16 BP baseline disagrees with paper Table 2 by more than 3 GWh",
    )
    directional_errors = [
        observed - expected
        for observed, expected in zip(
            assessment["directional_power_mw"],
            PAPER_BP_BASE_DIRECTIONAL_MW,
            strict=True,
        )
    ]
    require(
        max(abs(value) for value in directional_errors) <= 1.0,
        "T16 directional BP baseline disagrees with paper Table 2",
    )
    require(
        assessment["aep_gwh"] == serial["aep_gwh"]
        and assessment["directional_power_mw"]
            == serial["directional_power_mw"],
        "fixed-order one-worker/four-worker evaluator parity failed",
    )

    smoke_arguments = [
        "--mode", "optimize",
        "--data", str(data),
        "--workers", "4",
        "--start-index", "0",
        "--seed", "1601",
        "--maxeval-per-stage", "5",
        "--smoke-lifecycle",
    ]
    first = call(args.binary, smoke_arguments)
    replay = call(args.binary, smoke_arguments)
    random_start = call(
        args.binary,
        [
            "--mode", "optimize",
            "--data", str(data),
            "--workers", "4",
            "--start-index", "17",
            "--seed", "1601",
            "--maxeval-per-stage", "3",
            "--smoke-lifecycle",
        ],
    )
    require(
        first["scientific_hash"] == replay["scientific_hash"],
        "fixed-seed optimization replay hash mismatch",
    )
    require(len(first["stages"]) == 3, "smoke WEC lifecycle is incomplete")
    require(
        [stage["wec_factor"] for stage in first["stages"]] == [3, 1, 1],
        "smoke WEC factor order mismatch",
    )
    require(
        first["stages"][-1]["turbulence"] == "smooth",
        "final smooth-local-TI stage is absent",
    )
    for label, payload in [("baseline", first), ("random", random_start)]:
        require(payload["observed_workers"] >= 2, f"{label}: no multicore use")
        require(
            payload["final_paper_assessment"][
                "maximum_constraint_violation_m"
            ] <= 1.0e-3,
            f"{label}: final layout is infeasible",
        )
        require(
            math.isfinite(payload["final_paper_assessment"]["aep_gwh"]),
            f"{label}: final AEP is non-finite",
        )
        require(
            all(stage["objective_calls"] >= 1 for stage in payload["stages"]),
            f"{label}: an optimization stage performed no objective call",
        )

    report = {
        "status": "pass",
        "data_sha256": digest,
        "paper_bp_base_aep_gwh": 481.0,
        "reproduced_bp_base_aep_gwh": assessment["aep_gwh"],
        "aep_error_gwh": assessment["aep_gwh"] - 481.0,
        "maximum_directional_power_error_mw": max(
            abs(value) for value in directional_errors
        ),
        "serial_parallel_scientific_parity": True,
        "gradient_dimension": len(gradient["gradient_gwh_per_m"]),
        "observed_inner_workers": gradient["observed_workers"],
        "smoke_scientific_hash": first["scientific_hash"],
        "random_smoke_scientific_hash": random_start["scientific_hash"],
        "fact_conflict_resolved":
            "public text CP/CT header reversed; source pickle and conversion "
            "script establish CT/CP order",
    }
    print(json.dumps(report, sort_keys=True))


if __name__ == "__main__":
    main()
