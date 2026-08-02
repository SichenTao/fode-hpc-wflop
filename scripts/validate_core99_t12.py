#!/usr/bin/env python3
"""
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: independent T12 WindFLO equation/data oracle
Paper DOI: 10.1016/j.renene.2018.03.052
Public source: https://github.com/d9w/WindFLO revision
9e85a67bb2ca019768ea51dd0b634a46c8406ba2, MIT license
Provided assets: five XML scenarios and multi-language evaluators
Missing/conflicts and reconstruction: optimizer conflicts are outside this
oracle; equations below independently transcribe the released C++ evaluator
Method/problem semantic IDs: t12_four_competition_methods_v1;
t12_windflo_2015_five_scenarios_v1
Controlling contract: shared/contracts/core99_t12_windflo_2015.json
Independence boundary: direct Python equations and subprocess JSON only; no
production C++ function is imported
Claim boundary: evaluator semantic oracle, not author optimizer replay
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
"""

from __future__ import annotations

import argparse
import json
import math
import subprocess
from pathlib import Path
from typing import Any


def weibull(value: float, scale: float, shape: float) -> float:
    if value <= 0.0 or scale <= 0.0 or shape <= 0.0:
        return 0.0
    return 1.0 - math.exp(-((value / scale) ** shape))


def power(speed: float) -> float:
    if speed < 3.5:
        return 0.0
    if speed <= 14.0:
        return 140.86 * speed - 500.0
    if speed < 20.0:
        return 1500.0
    return 0.0


def evaluate(
    layout: list[list[float]],
    scenario: dict[str, Any],
) -> dict[str, float]:
    radius = 38.5
    wake_expansion = 0.075
    alpha = math.atan(wake_expansion)
    rk_ratio = radius / wake_expansion
    deficit_scale = 1.0 - math.sqrt(1.0 - 0.8)
    output = 0.0
    for angle in scenario["angles"]:
        theta = math.radians(angle["theta_deg"] + 7.5)
        cosine = math.cos(theta)
        sine = math.sin(theta)
        for turbine, (x, y) in enumerate(layout):
            squared_deficit = 0.0
            for other, (other_x, other_y) in enumerate(layout):
                if turbine == other:
                    continue
                dx = x - other_x
                dy = y - other_y
                numerator = dx * cosine + dy * sine + rk_ratio
                denominator = math.hypot(
                    dx + rk_ratio * cosine,
                    dy + rk_ratio * sine,
                )
                beta = math.acos(
                    max(-1.0, min(1.0, numerator / denominator))
                )
                if beta < alpha:
                    projected = abs(dx * cosine + dy * sine)
                    loss = deficit_scale / (
                        1.0 + wake_expansion / radius * projected
                    ) ** 2
                    squared_deficit += loss * loss
            scale = angle["c"] * (1.0 - math.sqrt(squared_deficit))
            expected = 0.0
            for interval in range(1, 22):
                lower = 3.5 + 0.5 * (interval - 1)
                upper = lower + 0.5
                expected += (
                    weibull(upper, scale, angle["k"])
                    - weibull(lower, scale, angle["k"])
                ) * power((lower + upper) / 2.0)
            expected += 1500.0 * (
                1.0 - weibull(14.0, scale, angle["k"])
            )
            output += expected * 15.0 * angle["omega"]
    turbines = float(len(layout))
    capital = (
        (
            750000.0 * turbines
            + 8000000.0 * math.floor(turbines / 30.0)
        )
        * (0.666667 + 0.333333 * math.exp(-0.00174 * turbines**2))
        + 20000.0 * turbines
    )
    annuity = (1.0 - 1.03**-20.0) / 0.03
    return {
        "energy_output_kw": output,
        "wake_free_ratio": (
            output / scenario["wake_free_energy"] / turbines
        ),
        "energy_cost": capital / annuity / (8760.0 * output) + 0.1 / turbines,
    }


def cpp(
    binary: Path,
    scenario: int,
    layout: list[list[float]],
    workers: int,
) -> dict[str, Any]:
    flat = ",".join(str(value) for point in layout for value in point)
    return json.loads(
        subprocess.check_output(
            [
                str(binary),
                "--scenario",
                str(scenario),
                "--evaluate-layout",
                flat,
                "--workers",
                str(workers),
            ],
            text=True,
        )
    )


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--contract", type=Path, required=True)
    arguments = parser.parse_args()
    contract = json.loads(arguments.contract.read_text(encoding="utf-8"))
    report: dict[str, Any] = {
        "status": "pass",
        "problem_semantic_id": contract["problem_semantic_id"],
        "scenarios": {},
    }
    for scenario_index, scenario in enumerate(contract["scenarios"], start=1):
        layout = [
            [0.0, 0.0],
            [scenario["width_m"], scenario["height_m"]],
        ]
        oracle = evaluate(layout, scenario)
        serial = cpp(arguments.binary, scenario_index, layout, 1)
        parallel = cpp(arguments.binary, scenario_index, layout, 4)
        errors = {
            key: abs(serial[key] - oracle[key])
            for key in (
                "energy_cost",
                "wake_free_ratio",
                "energy_output_kw",
            )
        }
        parallel_errors = {
            key: abs(parallel[key] - serial[key])
            for key in errors
        }
        tolerances = {
            "energy_cost": 1.0e-13,
            "wake_free_ratio": 1.0e-13,
            "energy_output_kw": 1.0e-9,
        }
        if any(errors[key] > tolerances[key] for key in errors):
            report["status"] = "fail"
        if any(
            parallel_errors[key] > tolerances[key]
            for key in parallel_errors
        ):
            report["status"] = "fail"
        report["scenarios"][scenario["id"]] = {
            "python_oracle": oracle,
            "cpp_serial": {
                key: serial[key] for key in errors
            },
            "cpp_python_absolute_error": errors,
            "parallel_absolute_error": parallel_errors,
        }
    print(json.dumps(report, indent=2, sort_keys=True))
    if report["status"] != "pass":
        raise SystemExit(1)


if __name__ == "__main__":
    main()
