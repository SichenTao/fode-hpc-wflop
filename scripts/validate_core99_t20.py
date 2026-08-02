#!/usr/bin/env python3
"""
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: independent T20 equation and paper-layout oracle
Paper/DOI: Comparative Performance of Twelve Metaheuristics for Wind Farm
Layout Optimisation; 10.1007/s11831-021-09586-7
Public source: none located
Missing/conflicts/resolution/claim boundary:
hpc/core99_cpp/include/core99/kunakote_t20.hpp
This validator independently evaluates the reconstructed Fig. 5/6 layout and
checks the declared C++ result against both equations and paper-reported values.
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
"""

from __future__ import annotations

import argparse
import json
import math
import subprocess


ROWS = {
    100: [(100, 1), (700, 14), (900, 21), (1300, 34), (1500, 40)],
    300: [(100, 2), (300, 8), (1100, 30), (1700, 44)],
    500: [
        (300, 9), (500, 15), (900, 25), (1100, 31), (1300, 35),
        (1500, 41), (1700, 45), (1900, 51),
    ],
    700: [(100, 3), (500, 16), (900, 26), (1300, 36), (1700, 46), (1900, 52)],
    900: [
        (100, 4), (300, 10), (500, 17), (1300, 37), (1500, 42),
        (1700, 47), (1900, 53),
    ],
    1100: [(500, 18), (700, 22), (1300, 38)],
    1300: [(500, 19), (700, 23), (1100, 32), (1300, 39), (1500, 48), (1900, 54)],
    1500: [(100, 5), (300, 11), (900, 27), (1500, 49)],
    1700: [(100, 6), (300, 12), (500, 20), (700, 24), (900, 28), (1100, 33), (1500, 43)],
    1900: [(100, 7), (300, 13), (900, 29), (1500, 50)],
}


def layout() -> list[tuple[float, float]]:
    result: list[tuple[float, float] | None] = [None] * 54
    for y_value, entries in ROWS.items():
        for x_value, label in entries:
            result[label - 1] = (float(x_value), float(y_value))
    assert all(point is not None for point in result)
    return [point for point in result if point is not None]


def overlap(distance: float, first: float, second: float) -> float:
    if distance >= first + second:
        return 0.0
    if distance <= abs(first - second):
        return math.pi * min(first, second) ** 2
    angle_first = math.acos(
        max(-1.0, min(1.0, (distance**2 + first**2 - second**2) / (2.0 * distance * first)))
    )
    angle_second = math.acos(
        max(-1.0, min(1.0, (distance**2 + second**2 - first**2) / (2.0 * distance * second)))
    )
    radicand = (
        (-distance + first + second)
        * (distance + first - second)
        * (distance - first + second)
        * (distance + first + second)
    )
    return first**2 * angle_first + second**2 * angle_second - 0.5 * math.sqrt(max(0.0, radicand))


def evaluate(points: list[tuple[float, float]], partial: bool) -> tuple[float, float, float]:
    rotor_diameter = 40.0
    rotor_radius = 20.0
    rotor_area = math.pi * rotor_radius**2
    wake_decay = 0.5 / math.log(60.0 / 0.3)
    axial_deficit = 1.0 - math.sqrt(1.0 - 0.88)
    direction_powers = []
    for degrees in range(10, 361, 10):
        theta = math.radians(degrees)
        along = [x * math.cos(theta) + y * math.sin(theta) for x, y in points]
        across = [-x * math.sin(theta) + y * math.cos(theta) for x, y in points]
        total = 0.0
        for downstream in range(len(points)):
            deficit_squared = 0.0
            for upstream in range(len(points)):
                axial = along[downstream] - along[upstream]
                if axial <= 1.0e-10:
                    continue
                wake_radius = rotor_radius + wake_decay * axial
                radial = abs(across[downstream] - across[upstream])
                if partial:
                    fraction = overlap(radial, wake_radius, rotor_radius) / rotor_area
                else:
                    fraction = 1.0 if radial <= wake_radius else 0.0
                expanded = rotor_diameter + 2.0 * wake_decay * axial
                deficit = axial_deficit * (rotor_diameter / expanded) ** 2 * fraction
                deficit_squared += deficit**2
            speed = 12.0 * max(0.0, 1.0 - math.sqrt(deficit_squared))
            total += 0.5 * 0.4 * 1.225 * rotor_area * speed**3
        direction_powers.append(total / 1000.0)
    power = sum(direction_powers) / len(direction_powers)
    count = len(points)
    cost = count * (2.0 / 3.0 + math.exp(-0.00174 * count**2) / 3.0)
    return cost / power, power, cost


def run(binary: str, problem: str) -> dict:
    completed = subprocess.run(
        [binary, "--problem", problem, "--mode", "figure5"],
        check=True,
        text=True,
        capture_output=True,
    )
    return json.loads(completed.stdout)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", required=True)
    args = parser.parse_args()

    points = layout()
    paper_power = {
        "t20_case1_variable_hub": 23431.5696,
        "t20_case2_variable_partial": 24401.3686,
    }
    report = {}
    for problem, partial in (
        ("t20_case1_variable_hub", False),
        ("t20_case2_variable_partial", True),
    ):
        expected_objective, expected_power, expected_cost = evaluate(points, partial)
        observed = run(args.binary, problem)
        assert abs(observed["objective"] - expected_objective) <= 1.0e-13
        assert abs(observed["average_power_kw"] - expected_power) <= 1.0e-8
        assert abs(observed["cost"] - expected_cost) <= 1.0e-12
        relative_paper_error = abs(expected_power - paper_power[problem]) / paper_power[problem]
        assert relative_paper_error < 0.015
        report[problem] = {
            "independent_power_kw": expected_power,
            "paper_figure_power_kw": paper_power[problem],
            "relative_paper_error": relative_paper_error,
        }

    print(json.dumps({"status": "pass", "cases": report}, sort_keys=True))


if __name__ == "__main__":
    main()
