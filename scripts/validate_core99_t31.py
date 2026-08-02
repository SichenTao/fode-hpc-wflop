#!/usr/bin/env python3
"""
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: independent T31 Mosetti equation and all-core replay oracle
Paper title: Variable Neighborhood Search for Large Offshore Wind Farm Layout
Optimization
Paper DOI: 10.1016/j.cor.2021.105588
Dataset DOI: 10.11583/DTU.13134731.
Public source: the ten large instances are public; no paper-linked VNS source
implementation was found.
Missing/conflicts/reconstruction:
hpc/core99_cpp/include/core99/cazzaro_t31.hpp.
Independence boundary: this Python oracle separately implements the declared
Mosetti DI pairwise Jensen matrix and objective. It does not contribute to the
production optimizer, matrix cache, or reported timings.
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
"""
from __future__ import annotations

import argparse
import json
import math
import subprocess


HOURS_PER_YEAR = 8760.0
ROTOR_RADIUS_M = 20.0
WAKE_EXPANSION = 0.5 / math.log(60.0 / 0.3)
THRUST_COEFFICIENT = 0.88
FREE_SPEED_MPS = 12.0
MINIMUM_SPACING_M = 200.0
SHAKE_MODES = (
    "circular",
    "conic",
    "directional",
    "displacement",
    "random",
    "random_directional",
    "circular_displacement",
    "random_conic",
    "directional_conic",
    "all",
)


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


def overlap(distance: float, first_radius: float, second_radius: float) -> float:
    if distance >= first_radius + second_radius:
        return 0.0
    if distance <= abs(first_radius - second_radius):
        return math.pi * min(first_radius, second_radius) ** 2
    first = math.acos(
        max(
            -1.0,
            min(
                1.0,
                (
                    distance**2
                    + first_radius**2
                    - second_radius**2
                )
                / (2.0 * distance * first_radius),
            ),
        )
    )
    second = math.acos(
        max(
            -1.0,
            min(
                1.0,
                (
                    distance**2
                    + second_radius**2
                    - first_radius**2
                )
                / (2.0 * distance * second_radius),
            ),
        )
    )
    radicand = max(
        0.0,
        (-distance + first_radius + second_radius)
        * (distance + first_radius - second_radius)
        * (distance - first_radius + second_radius)
        * (distance + first_radius + second_radius),
    )
    return (
        first_radius**2 * first
        + second_radius**2 * second
        - 0.5 * math.sqrt(radicand)
    )


def power_mw(speed: float) -> float:
    if speed < 0.0 or speed > 30.0:
        return 0.0
    low_speed = math.floor(speed)
    high_speed = math.ceil(speed)
    low = min(0.0003 * low_speed**3, 0.630)
    high = min(0.0003 * high_speed**3, 0.630)
    if low_speed == high_speed:
        return low
    return low + (speed - low_speed) * (high - low)


def coordinates(index: int) -> tuple[float, float]:
    return ((index % 10 + 0.5) * 200.0, (index // 10 + 0.5) * 200.0)


def pair_loss(first: int, second: int) -> float:
    first_x, first_y = coordinates(first)
    second_x, second_y = coordinates(second)
    total = 0.0
    for target_x, target_y, source_x, source_y in (
        (first_x, first_y, second_x, second_y),
        (second_x, second_y, first_x, first_y),
    ):
        downstream = target_y - source_y
        if downstream <= 0.0:
            continue
        crosswind = abs(target_x - source_x)
        wake_radius = ROTOR_RADIUS_M + WAKE_EXPANSION * downstream
        fraction = overlap(
            crosswind, wake_radius, ROTOR_RADIUS_M
        ) / (math.pi * ROTOR_RADIUS_M**2)
        if fraction <= 0.0:
            continue
        deficit = (
            (1.0 - math.sqrt(1.0 - THRUST_COEFFICIENT))
            * (ROTOR_RADIUS_M / wake_radius) ** 2
            * fraction
        )
        total += HOURS_PER_YEAR * (
            power_mw(FREE_SPEED_MPS * max(0.0, 1.0 - min(1.0, deficit)))
            - power_mw(FREE_SPEED_MPS)
        )
    return total


def evaluate(selected: list[int]) -> tuple[float, float]:
    value = len(selected) * HOURS_PER_YEAR * power_mw(FREE_SPEED_MPS)
    violation = 0.0
    for first_slot, first in enumerate(selected):
        first_x, first_y = coordinates(first)
        for second in selected[first_slot + 1 :]:
            second_x, second_y = coordinates(second)
            distance = math.hypot(first_x - second_x, first_y - second_y)
            violation += max(0.0, MINIMUM_SPACING_M - distance)
            value += pair_loss(first, second)
    return value, violation


def run(binary: str, workers: int, shake_mode: str) -> dict:
    completed = subprocess.run(
        (
            binary,
            "--case",
            "t31_mosetti_di",
            "--seed",
            "311",
            "--workers",
            str(workers),
            "--iterations",
            "2",
            "--time-seconds",
            "30",
            "--shake-mode",
            shake_mode,
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
    hashes: dict[str, str] = {}
    for mode in SHAKE_MODES:
        serial = run(args.binary, 1, mode)
        parallel = run(args.binary, 4, mode)
        require(
            serial["scientific_hash"] == parallel["scientific_hash"],
            f"T31 {mode} one/four-core scientific replay mismatch",
        )
        require(
            serial["best_positions"] == parallel["best_positions"]
            and serial["best_history_mwh"] == parallel["best_history_mwh"],
            f"T31 {mode} one/four-core state mismatch",
        )
        require(
            serial["observed_workers"] == 1
            and parallel["observed_workers"] == 4,
            f"T31 {mode} worker receipt mismatch",
        )
        require(
            serial["completed_vns_iterations"] == 2
            and serial["matrix_pair_evaluations"] == 4950,
            f"T31 {mode} lifecycle accounting mismatch",
        )
        objective, violation = evaluate(serial["best_positions"])
        reported = serial["best"]
        require(
            abs(objective - reported["objective_mwh_equivalent"])
            <= 2.0e-11 * max(1.0, abs(objective)),
            f"T31 {mode} independent objective mismatch",
        )
        require(
            abs(violation - reported["spacing_violation_m"]) <= 1.0e-10,
            f"T31 {mode} independent spacing mismatch",
        )
        require(
            violation <= 1.0e-9
            and reported["objective_mwh_equivalent"]
            >= serial["initial"]["objective_mwh_equivalent"] - 1.0e-8,
            f"T31 {mode} feasibility or retention mismatch",
        )
        hashes[mode] = serial["scientific_hash"]
    print(
        "core99_t31_h5_pass modes=10 "
        f"random_conic_hash={hashes['random_conic']}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
