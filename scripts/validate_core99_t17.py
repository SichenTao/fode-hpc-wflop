#!/usr/bin/env python3
"""
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: independent T17 equation, proxy, lifecycle, and replay H5
Paper/DOI: A New Wake Model and Comparison of Eight Algorithms for Layout
Optimization of Wind Farms in Complex Terrain; 10.1016/j.apenergy.2019.114189
Public source/missing/reconstruction: hpc/core99_cpp/include/core99/brogna_t17.hpp
Controlling contract: shared/contracts/core99_t17_brogna_2020.json
Claim boundary: semantic/equation validation, not private-site replay
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
"""
from __future__ import annotations

import argparse
import json
import math
from pathlib import Path
import subprocess


def call(binary: str, arguments: list[str]) -> dict:
    completed = subprocess.run(
        [binary, *arguments],
        check=True,
        text=True,
        capture_output=True,
    )
    return json.loads(completed.stdout)


def gaussian_oracle(x_d: float, r_d: float, ct: float) -> float:
    if x_d < 2.0 or x_d > 40.0 or ct <= 0.0:
        return 0.0
    root = math.sqrt(max(1.0e-12, 1.0 - ct))
    epsilon = 0.2 * math.sqrt(0.5 * (1.0 + root) / root)
    width = 0.042 * x_d + epsilon
    centre = 1.0 - math.sqrt(max(0.0, 1.0 - ct / (8.0 * width**2)))
    deficit = centre * math.exp(-(r_d**2) / (2.0 * width**2))
    return deficit if deficit >= 0.01 else 0.0


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", required=True)
    parser.add_argument("--proxy", required=True)
    args = parser.parse_args()
    proxy = Path(args.proxy)
    assert proxy.is_file() and proxy.stat().st_size == 148800

    equations = []
    for x_d, r_d, ct in [
        (2.0, 0.0, 0.747),
        (5.0, 0.5, 0.747),
        (12.0, 1.0, 0.30),
        (40.0, 0.0, 0.747),
        (41.0, 0.0, 0.747),
    ]:
        observed = call(
            args.binary,
            [
                "--mode", "gaussian",
                "--streamwise-d", str(x_d),
                "--radial-d", str(r_d),
                "--ct", str(ct),
            ],
        )["deficit_ratio"]
        expected = gaussian_oracle(x_d, r_d, ct)
        assert abs(observed - expected) < 1.0e-13
        equations.append(
            {"x_d": x_d, "r_d": r_d, "ct": ct, "deficit_ratio": observed}
        )

    common = ["--proxy", str(proxy), "--workers", "2"]
    wake = call(args.binary, ["--mode", "evaluate", *common])["evaluation"]
    no_wake = call(
        args.binary, ["--mode", "evaluate", *common, "--no-wakes"]
    )["evaluation"]
    assert wake["constraint_violation_m"] == 0.0
    assert no_wake["constraint_violation_m"] == 0.0
    assert 0.0 < wake["objective"] <= no_wake["objective"]
    assert 15.0 < no_wake["objective"] < 35.0

    run_arguments = [
        "--mode", "optimize",
        *common,
        "--stage1-fes", "5",
        "--stage2-fes", "10",
        "--seed", "1701",
    ]
    first = call(args.binary, run_arguments)
    replay = call(args.binary, run_arguments)
    assert first["stage1_physical_fes"] == 5
    assert first["stage2_physical_fes"] == 10
    assert first["physical_fes"] == 15
    assert first["final_evaluation"]["includes_wakes"]
    assert first["final_evaluation"]["constraint_violation_m"] == 0.0
    assert first["scientific_hash"] == replay["scientific_hash"]
    assert first["observed_workers"] >= 1
    print(json.dumps(
        {
            "status": "pass",
            "equation_checks": equations,
            "proxy_size_bytes": proxy.stat().st_size,
            "paper_figure_2_no_wake_objective": no_wake["objective"],
            "paper_figure_2_wake_objective": wake["objective"],
            "smoke_scientific_hash": first["scientific_hash"],
        },
        sort_keys=True,
    ))


if __name__ == "__main__":
    main()
