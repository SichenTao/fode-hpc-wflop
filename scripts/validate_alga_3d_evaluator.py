#!/usr/bin/env python3
"""Independent fixed-layout oracle for the ALGA Guishan-family P3 evaluator."""

from __future__ import annotations

import argparse
import json
import math
import subprocess
from pathlib import Path


def power(case: dict, velocity: float) -> float:
    cutin = float(case["power_curve_cutin_mps"])
    rated = float(case["power_curve_rated_mps"])
    cutout = float(case["power_curve_cutout_mps"])
    if velocity < cutin or velocity >= cutout:
        return 0.0
    if velocity >= rated:
        return float(case["power_curve_rated_kw"])
    return float(case["power_curve_rated_kw"]) * (
        velocity**3 - cutin**3
    ) / (rated**3 - cutin**3)


def evaluate(case: dict, layout: list[int]) -> float:
    cols = int(case["cols"])
    width = float(case["cell_width"])
    hub = float(case["hub_height"])
    radius = 0.5 * float(case["rotor_diameter"])
    expansion = float(case["gaussian_wake_expansion"])
    coefficient = float(case["wake_deficit_coefficient"])
    elevation = case["terrain_elevation_m"]
    points = []
    for cell in layout:
        zero = cell - 1
        points.append(
            (
                (zero % cols + 0.5) * width,
                (zero // cols + 0.5) * width,
                hub + float(elevation[zero]),
            )
        )
    accumulated = [0.0] * len(layout)
    for direction_index, theta in enumerate(case["wind_directions_rad"]):
        cosine, sine = math.cos(theta), math.sin(theta)
        downwind = [cosine * x + sine * y for x, y, _ in points]
        crosswind = [-sine * x + cosine * y for x, y, _ in points]
        losses = []
        for target in range(len(points)):
            squared = 0.0
            for source in range(len(points)):
                downstream = downwind[target] - downwind[source]
                if source == target or downstream <= 0.0:
                    continue
                wake_radius = radius + expansion * downstream
                sigma = wake_radius / 1.98
                radial = (
                    (crosswind[target] - crosswind[source]) ** 2
                    + (points[target][2] - points[source][2]) ** 2
                )
                fraction = (
                    coefficient
                    * radius**2
                    / wake_radius**2
                    * math.exp(-radial / (2.0 * sigma**2))
                )
                squared += fraction**2
            losses.append(min(1.0, math.sqrt(squared)))
        for target, loss in enumerate(losses):
            shear = (
                points[target][2] / hub
            ) ** float(case["terrain_shear_exponent"])
            for speed_index, speed in enumerate(case["wind_speeds_mps"]):
                accumulated[target] += (
                    float(case["joint_probabilities"][direction_index][speed_index])
                    * power(case, float(speed) * shear * (1.0 - loss))
                )
    return math.fsum(sorted(accumulated))


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--probe", type=Path, required=True)
    parser.add_argument("--cases", type=Path, required=True)
    args = parser.parse_args()
    payload = json.loads(args.cases.read_text(encoding="utf-8"))
    checked = 0
    for index in (0, 12, 23):
        case = payload["cases"][index]
        layout = list(range(1, int(case["turbine_count"]) + 1))
        expected = evaluate(case, layout)
        completed = subprocess.run(
            [
                str(args.probe),
                str(args.cases),
                case["case_id"],
                ",".join(map(str, layout)),
                "20",
            ],
            check=True,
            capture_output=True,
            text=True,
        )
        observed = float(completed.stdout)
        tolerance = 1.0e-10 * max(1.0, abs(expected))
        if abs(observed - expected) > tolerance:
            raise RuntimeError(
                f"{case['case_id']}: C++={observed:.17g} "
                f"oracle={expected:.17g}"
            )
        checked += 1
    print(f"alga_3d_evaluator_oracle_pass cases={checked} tolerance=1e-10")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
