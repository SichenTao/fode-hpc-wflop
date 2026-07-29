#!/usr/bin/env python3
"""Independent scalar Jensen/Park oracle for every native case contract."""

from __future__ import annotations

import argparse
import csv
import json
import math
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def overlap(dx: float, radius: float, wake_radius: float) -> float:
    if dx >= radius + wake_radius:
        return 0.0
    if dx >= math.sqrt(wake_radius * wake_radius - radius * radius):
        alpha = math.acos(
            (wake_radius * wake_radius + dx * dx - radius * radius)
            / (2.0 * wake_radius * dx)
        )
        beta = math.acos(
            (radius * radius + dx * dx - wake_radius * wake_radius)
            / (2.0 * radius * dx)
        )
        return (
            alpha * wake_radius * wake_radius
            + beta * radius * radius
            - wake_radius * dx * math.sin(alpha)
        )
    if dx >= wake_radius - radius:
        alpha = math.acos(
            (wake_radius * wake_radius + dx * dx - radius * radius)
            / (2.0 * wake_radius * dx)
        )
        beta = math.pi - math.acos(
            (radius * radius + dx * dx - wake_radius * wake_radius)
            / (2.0 * radius * dx)
        )
        return (
            math.pi * radius * radius
            - (
                beta * radius * radius
                + wake_radius * dx * math.sin(alpha)
                - alpha * wake_radius * wake_radius
            )
        )
    return math.pi * radius * radius


def turbine_power(case: dict, velocity: float) -> float:
    cutin = float(case.get("power_curve_cutin_mps", 2.0))
    rated_speed = float(case.get("power_curve_rated_mps", 12.8))
    cutout = float(case.get("power_curve_cutout_mps", 18.0))
    if velocity < cutin:
        return 0.0
    if velocity < rated_speed:
        return float(case.get("power_curve_cubic_coefficient", 0.3)) * velocity**3
    if velocity < cutout:
        return float(case.get("power_curve_rated_kw", 629.1))
    return 0.0


def evaluate(case: dict, layout: list[int]) -> float:
    width = float(case["cell_width"])
    cols = int(case["cols"])
    coordinates = [
        (((cell - 1) % cols + 0.5) * width, ((cell - 1) // cols + 0.5) * width)
        for cell in layout
    ]
    radius = float(case.get("rotor_diameter", 77.0)) / 2.0
    hub = float(case.get("hub_height", 80.0))
    roughness = float(case.get("surface_roughness", 0.00025))
    coefficient = float(case.get("wake_deficit_coefficient", 2.0 / 3.0))
    entrainment = 0.5 / math.log(hub / roughness)
    directions = case["wind_directions_rad"]
    speeds = case["wind_speeds_mps"]
    probabilities = case["joint_probabilities"]
    accumulated = [0.0] * len(layout)
    for direction_index, theta in enumerate(directions):
        cosine, sine = math.cos(theta), math.sin(theta)
        transformed = [
            (cosine * x - sine * y, sine * x + cosine * y)
            for x, y in coordinates
        ]
        order = sorted(range(len(layout)), key=lambda i: -transformed[i][1])
        losses = [0.0] * len(layout)
        for downstream_position in range(1, len(layout)):
            downstream = order[downstream_position]
            squared = 0.0
            for upstream_position in range(downstream_position + 1):
                upstream = order[upstream_position]
                dx = abs(transformed[downstream][0] - transformed[upstream][0])
                dy = abs(transformed[downstream][1] - transformed[upstream][1])
                if dy == 0.0:
                    deficiency = 0.0
                else:
                    wake_radius = radius + entrainment * dy
                    area = overlap(dx, radius, wake_radius)
                    deficiency = (
                        coefficient * radius * radius / (wake_radius * wake_radius)
                        * area / (math.pi * radius * radius)
                    )
                squared += deficiency * deficiency
            losses[downstream] = math.sqrt(squared)
        for turbine, loss in enumerate(losses):
            for speed_index, speed in enumerate(speeds):
                accumulated[turbine] += (
                    turbine_power(case, (1.0 - loss) * float(speed))
                    * float(probabilities[direction_index][speed_index])
                )
    return math.fsum(sorted(accumulated))


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--probe", required=True)
    args = parser.parse_args()
    with (ROOT / "docs/scalar_problem_package_registry.tsv").open(
        encoding="utf-8", newline=""
    ) as handle:
        rows = list(csv.DictReader(handle, delimiter="\t"))
    seen: set[str] = set()
    checked = 0
    for row in rows:
        contract_name = row["case_contract"]
        if contract_name in seen:
            continue
        seen.add(contract_name)
        contract_path = ROOT / contract_name
        contract = json.loads(contract_path.read_text(encoding="utf-8"))
        case = contract["cases"][0]
        blocked = set(case["unavailable_cells_1based"])
        layout = [
            cell
            for cell in range(1, int(case["rows"]) * int(case["cols"]) + 1)
            if cell not in blocked
        ][: int(case["turbine_count"])]
        oracle = evaluate(case, layout)
        completed = subprocess.run(
            [
                args.probe,
                str(contract_path),
                case["case_id"],
                ",".join(str(value) for value in layout),
                "2",
            ],
            check=True,
            capture_output=True,
            text=True,
        )
        cpp = float(completed.stdout)
        tolerance = 1.0e-10 * max(1.0, abs(oracle))
        if abs(cpp - oracle) > tolerance:
            raise RuntimeError(
                f"{contract_name}/{case['case_id']}: "
                f"C++={cpp:.17g} oracle={oracle:.17g}"
            )
        checked += 1
    print(
        "scalar_native_evaluator_oracle_pass "
        f"contracts={checked} absolute_relative_tolerance=1e-10"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
