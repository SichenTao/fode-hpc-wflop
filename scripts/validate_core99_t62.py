#!/usr/bin/env python3
"""
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: independent T62 equation, cost, and table validator
Paper/DOI: Optimization of Wind Turbine Layout Position in a Wind Farm Using
a Newly-Developed Two-Dimensional Wake Model;
10.1016/j.apenergy.2016.04.098
Public source/missing/resolution/claim: hpc/core99_cpp/include/core99/gao_t62.hpp
This validator identifies the paper's N=38/39 fitness-table inconsistency
instead of fitting the implementation to inconsistent values.
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
        [binary, *arguments], check=True, text=True, capture_output=True
    )
    return json.loads(completed.stdout)


def wake_oracle(x_d: float, r_d: float, ct: float, i0: float) -> float:
    diameter, radius = 40.0, 20.0
    downstream, crosswind = x_d * diameter, r_d * diameter
    a = 0.5 * (1.0 - math.sqrt(1.0 - ct))
    r1 = radius * math.sqrt((1.0 - a) / (1.0 - 2.0 * a))
    i_wake = (0.4 * ct / math.sqrt(x_d) + math.sqrt(i0)) ** 2
    k = 0.5 / math.log(60.0 / 0.3)
    k_wake = k * i_wake / i0
    rx = r1 + k_wake * downstream
    if abs(crosswind) > rx:
        return 1.0
    deficit = 2.0 * a / (1.0 + k_wake * downstream / r1) ** 2
    deficit *= 5.16 / math.sqrt(2.0 * math.pi)
    deficit *= math.exp(-(crosswind**2) / (2.0 * (rx / 2.58) ** 2))
    return 1.0 - min(1.0, max(0.0, deficit))


def paper_cost(turbines: int) -> float:
    return turbines * (
        2.0 / 3.0 + math.exp(-0.00174 * turbines * turbines) / 3.0
    )


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", required=True)
    args = parser.parse_args()
    wake_checks = []
    for x_d, r_d, ct in [
        (2.5, 0.0, 0.62),
        (5.0, 0.5, 0.62),
        (7.5, 0.0, 0.82),
        (10.0, 1.0, 0.62),
    ]:
        observed = call(
            args.binary,
            [
                "--mode", "wake", "--x-d", str(x_d), "--r-d", str(r_d),
                "--ct", str(ct), "--i0", "0.1",
            ],
        )["speed_ratio"]
        expected = wake_oracle(x_d, r_d, ct, 0.1)
        assert abs(observed - expected) < 1.0e-12
        wake_checks.append({"x_d": x_d, "r_d": r_d, "speed_ratio": observed})

    isolated = call(
        args.binary,
        ["--mode", "evaluate", "--turbines", "1", "--sites", "0"],
    )["evaluation"]
    assert abs(isolated["average_power_kw"] - 518.4) < 1.0e-10
    assert abs(isolated["efficiency"] - 1.0) < 1.0e-12

    printed = [
        (38, 15333.0, 1.756),
        (39, 15866.0, 1.661),
        (40, 16979.0, 1.619),
    ]
    table_audit = []
    for turbines, power_kw, printed_fitness_milli in printed:
        derived = 1000.0 * paper_cost(turbines) / power_kw
        table_audit.append(
            {
                "turbines": turbines,
                "derived_fitness_milli": derived,
                "printed_fitness_milli": printed_fitness_milli,
                "derived_efficiency": power_kw / (turbines * 518.4),
            }
        )
    assert abs(table_audit[2]["derived_fitness_milli"] - 1.619) < 5.0e-4
    assert abs(table_audit[0]["derived_fitness_milli"] - 1.756) > 0.02
    assert abs(table_audit[1]["derived_fitness_milli"] - 1.661) > 0.02

    smoke = call(
        args.binary,
        [
            "--mode", "optimize", "--turbines", "38", "--workers", "2",
            "--demes", "2", "--individuals", "4", "--stagnation", "2",
            "--max-generations", "2", "--migration-period", "1",
            "--seed", "6201",
        ],
    )
    assert smoke["physical_fes"] == 24
    assert smoke["observed_workers"] >= 1
    assert smoke["best_evaluation"]["constraint_violation"] == 0.0
    print(json.dumps(
        {
            "status": "pass",
            "wake_checks": wake_checks,
            "table_audit": table_audit,
            "smoke_scientific_hash": smoke["scientific_hash"],
        },
        sort_keys=True,
    ))


if __name__ == "__main__":
    main()
