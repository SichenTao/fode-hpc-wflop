#!/usr/bin/env python3
"""Independent equation oracle for the T04 C++ UWFLO evaluator."""

from __future__ import annotations

import argparse
import json
import math
import subprocess
from pathlib import Path


D0 = 0.12
RATED = 0.385


def overlap(distance: float, first: float, second: float) -> float:
    if distance >= first + second:
        return 0.0
    if distance <= abs(first - second):
        return math.pi * min(first, second) ** 2
    a = math.acos(
        max(-1.0, min(1.0, (distance**2 + first**2 - second**2) / (2 * distance * first)))
    )
    b = math.acos(
        max(-1.0, min(1.0, (distance**2 + second**2 - first**2) / (2 * distance * second)))
    )
    radical = max(
        0.0,
        (-distance + first + second)
        * (distance + first - second)
        * (distance - first + second)
        * (distance + first + second),
    )
    return first**2 * a + second**2 * b - 0.5 * math.sqrt(radical)


def induction(speed: float) -> float:
    value = (
        -0.0163 * speed**2 + 0.1635 * speed - 0.3142
        if speed <= 5.0
        else -0.0063 * speed + 0.1273
    )
    return max(0.0, min(0.5, value))


def power(speed: float, diameter: float, adaptive: bool) -> float:
    if adaptive and speed >= 6.17:
        return RATED * (diameter / D0) ** 2
    cp = max(0.0, -0.0494 * speed**2 + 0.4914 * speed - 0.9097)
    return 0.5 * 1.2 * math.pi * diameter**2 / 4.0 * cp * speed**3


def cost(diameter: float) -> float:
    commercial = diameter * 75.0 / D0
    return 143.85 - 0.32447 * commercial - 1.4841e-3 * commercial**2


def config(problem: str) -> tuple[int, int, float, float, bool]:
    if problem == "t04_uwflo_case1_n9":
        return 1, 9, 1.68, 0.72, False
    if problem == "t04_uwflo_case2_n9":
        return 2, 9, 1.68, 0.72, True
    if "_case3_i_n" in problem:
        return 3, int(problem.rsplit("_n", 1)[1]), 1.68, 0.72, False
    farm = int(problem.rsplit("_f", 1)[1])
    return 3, 18, 7 * farm * D0, 3 * farm * D0, False


def evaluate(problem: str, variables: list[float]) -> tuple[float, float, float]:
    case, count, farm_x, farm_y, variable_d = config(problem)
    diameters = variables[2 * count :] if variable_d else [D0] * count
    lower = [0.0] * (2 * count) + ([0.08] * count if variable_d else [])
    upper = (
        [farm_x] * count
        + [farm_y] * count
        + ([0.16] * count if variable_d else [])
    )
    violation = math.fsum(
        max(0.0, lo - value, value - hi)
        for value, lo, hi in zip(variables, lower, upper)
    )
    violation += math.fsum(
        max(
            0.0,
            0.5 * (diameters[i] + diameters[j])
            - math.hypot(
                variables[i] - variables[j],
                variables[count + i] - variables[count + j],
            ),
        )
        for i in range(count)
        for j in range(i + 1, count)
    )
    if variable_d:
        violation += max(
            0.0,
            math.fsum(cost(value) for value in diameters) / count - cost(D0),
        )
    inflow = 6.2 if case == 3 else 7.0896
    expansion = 0.5 / math.log(0.12 / 0.001)
    speeds = [inflow] * count
    order = sorted(range(count), key=lambda index: variables[index])
    for target_rank, target in enumerate(order):
        squared = 0.0
        for source in order[:target_rank]:
            downstream = variables[target] - variables[source]
            if source == target or downstream <= 0.0:
                continue
            crosswind = abs(variables[count + target] - variables[count + source])
            source_r = 0.5 * diameters[source]
            target_r = 0.5 * diameters[target]
            wake_r = source_r + expansion * downstream
            center_deficit = (
                2.0 * induction(inflow) * source_r**2 / wake_r**2
            )
            wake_speed = (1.0 - center_deficit) * speeds[source]
            absolute_deficit = inflow - wake_speed
            squared += (
                overlap(crosswind, wake_r, target_r)
                / (math.pi * target_r**2)
                * absolute_deficit**2
            )
        speeds[target] = max(0.0, inflow - math.sqrt(squared))
    farm_power = math.fsum(
        power(speed, diameter, case == 3)
        for speed, diameter in zip(speeds, diameters)
    )
    return farm_power, farm_power / (count * RATED), violation


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", type=Path, required=True)
    args = parser.parse_args()
    grid = [
        0.0, 0.84, 1.68, 0.0, 0.84, 1.68, 0.0, 0.84, 1.68,
        0.0, 0.0, 0.0, 0.36, 0.36, 0.36, 0.72, 0.72, 0.72,
    ]
    fixtures = [
        ("t04_uwflo_case1_n9", grid),
        ("t04_uwflo_case2_n9", grid + [0.12] * 9),
        ("t04_uwflo_case3_i_n9", grid),
    ]
    for problem, variables in fixtures:
        completed = subprocess.run(
            [
                str(args.binary),
                "--problem",
                problem,
                "--evaluate-variables",
                ",".join(map(str, variables)),
            ],
            check=True,
            capture_output=True,
            text=True,
        )
        observed = json.loads(completed.stdout)
        expected = evaluate(problem, variables)
        for field, target in zip(
            ("farm_power_w", "farm_efficiency", "constraint_violation"),
            expected,
        ):
            if not math.isclose(
                observed[field],
                target,
                rel_tol=3.0e-12,
                abs_tol=1.0e-12,
            ):
                raise RuntimeError(
                    f"{problem}/{field}: C++={observed[field]} oracle={target}"
                )
    print(
        "core99_t04_h5_pass "
        f"fixtures={len(fixtures)} independent_python_oracle=true"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
