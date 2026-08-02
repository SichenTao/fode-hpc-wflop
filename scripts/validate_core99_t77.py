#!/usr/bin/env python3
"""
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: independent T77 paper-equation and HPC replay oracle
Paper title: A Data-Driven Evolutionary Algorithm for Wind Farm Layout
Optimization
Paper DOI: 10.1016/j.energy.2020.118310
Public source: no paper-linked author code or data archive found.
Missing/conflicts/reconstruction:
hpc/core99_cpp/include/core99/long_t77.hpp.
Independence boundary: this validator reimplements the wake, Weibull-power,
constraint, budget, and one/all-core checks in Python. It does not contribute
to the production optimizer or formal timings.
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
"""
from __future__ import annotations

import argparse
import json
import math
import subprocess


RADIUS_M = 40.0
MINIMUM_SPACING_M = 200.0
THRUST_COEFFICIENT = 0.8
WAKE_EXPANSION = 0.01
CUT_IN_MPS = 3.5
RATED_MPS = 14.0
CUT_OUT_MPS = 25.0
RATED_POWER_KW = 1500.0
POWER_A = 6.0268
POWER_B = 0.0007
WS1_SCALE = (
    7, 5, 5, 5, 5, 4, 5, 6, 7, 7, 8, 9.5,
    10, 8.5, 8.5, 6.5, 4.6, 2.6, 8, 5, 6.4, 5.2, 4.5, 3.9,
)
WS1_PROBABILITY = (
    .0003, .0072, .0237, .0242, .0222, .0301,
    .0397, .0268, .0626, .0801, .1025, .1445,
    .1909, .1162, .0793, .0082, .0041, .0008,
    .001, .0005, .0013, .0031, .0085, .0222,
)
WS2_PROBABILITY = (
    0, .01, .01, .01, .01, .20, .60, .01, .01, .01, .01, .01,
    .01, .01, .01, .01, .01, .01, .01, .01, .01, .01, .01, 0,
)


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


def survival(speed_mps: float, shape: float, scale: float) -> float:
    return math.exp(-((speed_mps / scale) ** shape))


def logistic_power(speed_mps: float) -> float:
    exponential = math.exp(speed_mps)
    return exponential / (POWER_A + POWER_B * exponential)


def expected_turbine_power(scale: float) -> float:
    bins = 21
    width = (RATED_MPS - CUT_IN_MPS) / bins
    result = 0.0
    for index in range(bins):
        low = CUT_IN_MPS + width * index
        high = low + width
        probability = survival(low, 2.0, scale) - survival(
            high, 2.0, scale
        )
        result += probability * logistic_power(0.5 * (low + high))
    result += RATED_POWER_KW * (
        survival(RATED_MPS, 2.0, scale)
        - survival(CUT_OUT_MPS, 2.0, scale)
    )
    return result


def evaluate(
    layout: list[list[float]],
    scenario: int,
    farm_side_m: float,
) -> tuple[float, float]:
    violation = 0.0
    for target, (x_target, y_target) in enumerate(layout):
        violation += max(0.0, RADIUS_M - x_target)
        violation += max(0.0, x_target - (farm_side_m - RADIUS_M))
        violation += max(0.0, RADIUS_M - y_target)
        violation += max(0.0, y_target - (farm_side_m - RADIUS_M))
        for x_source, y_source in layout[:target]:
            violation += max(
                0.0,
                MINIMUM_SPACING_M
                - math.hypot(x_target - x_source, y_target - y_source),
            )
    induction = 0.5 * (
        1.0 - math.sqrt(1.0 - THRUST_COEFFICIENT)
    )
    probabilities = WS1_PROBABILITY if scenario == 1 else WS2_PROBABILITY
    scales = WS1_SCALE if scenario == 1 else (13.0,) * 24
    total = 0.0
    for index, probability in enumerate(probabilities):
        if probability <= 0.0:
            continue
        angle = math.radians(7.5 + 15.0 * index)
        flow_x = math.cos(angle)
        flow_y = math.sin(angle)
        cross_x = -flow_y
        cross_y = flow_x
        direction_power = 0.0
        for target, (x_target, y_target) in enumerate(layout):
            squared_deficit = 0.0
            for source, (x_source, y_source) in enumerate(layout):
                if target == source:
                    continue
                dx = x_target - x_source
                dy = y_target - y_source
                downstream = dx * flow_x + dy * flow_y
                if downstream <= 0.0:
                    continue
                crosswind = abs(dx * cross_x + dy * cross_y)
                if (
                    crosswind
                    > RADIUS_M + WAKE_EXPANSION * downstream
                ):
                    continue
                deficit = 2.0 * induction / (
                    1.0
                    + WAKE_EXPANSION * downstream / RADIUS_M
                ) ** 2
                squared_deficit += deficit * deficit
            retained = max(
                0.0,
                1.0 - min(1.0, math.sqrt(squared_deficit)),
            )
            direction_power += expected_turbine_power(
                scales[index] * retained
            )
        total += probability * direction_power
    return total, violation


def run(binary: str, workers: int, scenario: int) -> dict:
    completed = subprocess.run(
        (
            binary,
            "--case",
            f"t77_ws{scenario}_n15",
            "--seed",
            "771",
            "--workers",
            str(workers),
            "--generations",
            "4",
            "--stage1-generations",
            "2",
        ),
        text=True,
        capture_output=True,
        timeout=120,
    )
    if completed.returncode:
        raise RuntimeError(completed.stderr or completed.stdout)
    return json.loads(completed.stdout)["runs"][0]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", required=True)
    args = parser.parse_args()
    rows = {
        scenario: {
            workers: run(args.binary, workers, scenario)
            for workers in (1, 4)
        }
        for scenario in (1, 2)
    }
    for scenario, worker_rows in rows.items():
        serial = worker_rows[1]
        parallel = worker_rows[4]
        require(
            serial["scientific_hash"] == parallel["scientific_hash"],
            f"T77 WS{scenario} one/four-core trajectory mismatch",
        )
        require(
            serial["physical_exact_fes"] == 160
            and serial["candidate_proposals"] == 160
            and serial["surrogate_inferences"] == 80,
            f"T77 WS{scenario} lifecycle accounting mismatch",
        )
        require(
            len(serial["best_history_kw"]) == 4
            and serial["observed_workers"] == 1
            and parallel["observed_workers"] == 4,
            f"T77 WS{scenario} history or worker receipt mismatch",
        )
        independent_power, independent_violation = evaluate(
            serial["best_layout"],
            scenario,
            serial["farm_side_m"],
        )
        reported = serial["best_evaluation"]
        tolerance = 2.0e-11 * max(1.0, abs(independent_power))
        require(
            abs(independent_power - reported["expected_power_kw"])
            <= tolerance,
            f"T77 WS{scenario} independent power mismatch",
        )
        require(
            abs(
                independent_violation
                - reported["constraint_violation_m"]
            ) <= 1.0e-10,
            f"T77 WS{scenario} independent constraint mismatch",
        )
        require(
            reported["constraint_violation_m"] <= 1.0e-9
            and reported["expected_power_kw"]
            >= serial["initial_best_power_kw"],
            f"T77 WS{scenario} feasibility or retention mismatch",
        )
    print(
        "core99_t77_h5_pass "
        f"ws1_hash={rows[1][1]['scientific_hash']} "
        f"ws2_hash={rows[2][1]['scientific_hash']}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
