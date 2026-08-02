#!/usr/bin/env python3
"""Independent equation oracle for the T01/T02 C++ historical-grid evaluator."""

from __future__ import annotations

import argparse
import json
import math
import subprocess
from pathlib import Path


ROWS = 10
COLS = 10
CELL_WIDTH = 200.0
ROTOR_RADIUS = 20.0
HUB_HEIGHT = 60.0
ROUGHNESS = 0.3
CT = 0.88
BASE = (0.836, 0.292, 0.135)
TAIL = (
    (0.836, 0.578, 0.135),
    (0.836, 0.870, 0.135),
    (0.836, 1.161, 0.135),
    (0.836, 1.128, 1.431),
    (0.836, 1.762, 1.431),
    (0.836, 1.128, 1.431),
    (0.836, 1.161, 0.135),
    (0.836, 0.870, 0.135),
    (0.836, 0.578, 0.135),
)


def overlap(distance: float, first: float, second: float) -> float:
    if distance >= first + second:
        return 0.0
    if distance <= abs(first - second):
        return math.pi * min(first, second) ** 2
    first_angle = math.acos(
        max(
            -1.0,
            min(
                1.0,
                (distance**2 + first**2 - second**2)
                / (2.0 * distance * first),
            ),
        )
    )
    second_angle = math.acos(
        max(
            -1.0,
            min(
                1.0,
                (distance**2 + second**2 - first**2)
                / (2.0 * distance * second),
            ),
        )
    )
    radicand = max(
        0.0,
        (-distance + first + second)
        * (distance + first - second)
        * (distance - first + second)
        * (distance + first + second),
    )
    return (
        first**2 * first_angle
        + second**2 * second_angle
        - 0.5 * math.sqrt(radicand)
    )


def states(problem: str) -> list[tuple[int, float, float]]:
    if problem.endswith("_case_a"):
        return [(0, 12.0, 1.0)]
    if problem.endswith("_case_b"):
        return [(direction, 12.0, 1.0 / 36.0) for direction in range(36)]
    raw = [BASE] * 27 + list(TAIL)
    total = math.fsum(math.fsum(row) for row in raw)
    speeds = (8.0, 12.0, 17.0)
    return [
        (direction, speeds[speed], raw[direction][speed] / total)
        for direction in range(36)
        for speed in range(3)
    ]


def evaluate(problem: str, cells_1based: list[int]) -> tuple[float, float]:
    cells = [value - 1 for value in cells_1based]
    axial = 0.5 * (1.0 - math.sqrt(1.0 - CT))
    initial_radius = ROTOR_RADIUS * math.sqrt(
        (1.0 - axial) / (1.0 - 2.0 * axial)
    )
    entrainment = 0.5 / math.log(HUB_HEIGHT / ROUGHNESS)
    full_area = math.pi * ROTOR_RADIUS**2
    expected = 0.0
    for direction, speed, probability in states(problem):
        angle = math.radians(10.0 * direction)
        sine, cosine = math.sin(angle), math.cos(angle)
        state_power = 0.0
        for target in cells:
            target_x = (target % COLS + 0.5) * CELL_WIDTH
            target_y = (target // COLS + 0.5) * CELL_WIDTH
            squared = 0.0
            for source in cells:
                if source == target:
                    continue
                source_x = (source % COLS + 0.5) * CELL_WIDTH
                source_y = (source // COLS + 0.5) * CELL_WIDTH
                dx, dy = target_x - source_x, target_y - source_y
                downstream = sine * dx + cosine * dy
                if downstream <= 0.0:
                    continue
                crosswind = abs(cosine * dx - sine * dy)
                wake_radius = initial_radius + entrainment * downstream
                fraction = (
                    2.0
                    * axial
                    * initial_radius**2
                    / wake_radius**2
                    * overlap(crosswind, wake_radius, ROTOR_RADIUS)
                    / full_area
                )
                squared += fraction * fraction
            local = speed * (1.0 - min(1.0, math.sqrt(squared)))
            state_power += min(0.3 * local**3, 630.0)
        expected += probability * state_power
    count = len(cells)
    cost = count * (
        2.0 / 3.0 + math.exp(-0.00174 * count * count) / 3.0
    )
    return cost / expected, expected


def run_cpp(binary: Path, problem: str, cells: list[int]) -> dict:
    completed = subprocess.run(
        [
            str(binary),
            "--problem",
            problem,
            "--evaluate-layout",
            ",".join(map(str, cells)),
        ],
        check=True,
        capture_output=True,
        text=True,
    )
    return json.loads(completed.stdout)


def close(left: float, right: float, label: str) -> None:
    if not math.isclose(left, right, rel_tol=2.0e-12, abs_tol=1.0e-12):
        raise RuntimeError(f"{label}: C++={left:.17g} oracle={right:.17g}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", type=Path, required=True)
    args = parser.parse_args()
    fixtures = (
        ("t01_mosetti_case_a", [1]),
        ("t01_mosetti_case_a", [1, 11]),
        ("t01_mosetti_case_b", [1, 2, 11, 55, 100]),
        ("t01_mosetti_case_c", [1, 6, 10, 51, 56, 60]),
        ("t02_grady_case_c_body1000", [1, 11, 21, 31, 41]),
        ("t02_grady_case_c_abstract2500", [5, 15, 25, 35, 45, 55]),
    )
    for problem, cells in fixtures:
        objective, power = evaluate(problem, cells)
        observed = run_cpp(args.binary, problem, cells)
        close(observed["objective"], objective, f"{problem}/objective")
        close(
            observed["expected_power_kw"],
            power,
            f"{problem}/expected_power_kw",
        )
        if observed["turbine_count"] != len(cells):
            raise RuntimeError(f"{problem}: turbine count mismatch")
    print(
        "core99_historical_grid_h5_pass "
        f"fixtures={len(fixtures)} independent_python_oracle=true"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
