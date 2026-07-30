#!/usr/bin/env python3
"""Independent equation oracle for the T03 C++ evaluator."""

from __future__ import annotations

import argparse
import json
import math
import subprocess
from pathlib import Path


RADIUS = 38.5
SPACING = 8.0 * RADIUS
INDUCTION = 0.5 * (1.0 - math.sqrt(1.0 - 0.8))
S1_P = (
    0.0, 0.01, 0.01, 0.01, 0.01, 0.2,
    0.6, 0.01, 0.01, 0.01, 0.01, 0.01,
    0.01, 0.01, 0.01, 0.01, 0.01, 0.01,
    0.01, 0.01, 0.01, 0.01, 0.01, 0.0,
)
S2_C = (
    7.0, 5.0, 5.0, 5.0, 5.0, 4.0,
    5.0, 6.0, 7.0, 7.0, 8.0, 9.5,
    10.0, 8.5, 8.5, 6.5, 4.6, 2.6,
    8.0, 5.0, 6.4, 5.2, 4.5, 3.9,
)
S2_P = (
    0.0002, 0.008, 0.0227, 0.0242, 0.0225, 0.0339,
    0.0423, 0.029, 0.0617, 0.0813, 0.0994, 0.1394,
    0.1839, 0.1115, 0.0765, 0.008, 0.0051, 0.0019,
    0.0012, 0.001, 0.0017, 0.0031, 0.0097, 0.0317,
)


def survival(speed: float, scale: float) -> float:
    return math.exp(-(speed / scale) ** 2)


def turbine_power(scale: float) -> float:
    if scale <= 0.0:
        return 0.0
    result = 0.0
    for bin_id in range(21):
        low = 3.5 + 0.5 * bin_id
        high = low + 0.5
        midpoint = 0.5 * (low + high)
        result += (
            survival(low, scale) - survival(high, scale)
        ) * max(0.0, 140.86 * midpoint - 500.0)
    return result + 1500.0 * survival(14.0, scale)


def evaluate(problem: str, layout: list[tuple[float, float]]) -> tuple:
    scenario = int(problem.split("_s", 1)[1].split("_", 1)[0])
    violation = math.fsum(
        max(0.0, x * x + y * y - 500.0**2) for x, y in layout
    )
    violation += math.fsum(
        max(
            0.0,
            SPACING**2
            - (layout[i][0] - layout[j][0]) ** 2
            - (layout[i][1] - layout[j][1]) ** 2,
        )
        for i in range(len(layout))
        for j in range(i + 1, len(layout))
    )
    expected = 0.0
    for direction in range(24):
        probability = S1_P[direction] if scenario == 1 else S2_P[direction]
        scale = 13.0 if scenario == 1 else S2_C[direction]
        if probability <= 0.0:
            continue
        angle = math.radians(7.5 + 15.0 * direction)
        cosine, sine = math.cos(angle), math.sin(angle)
        subtotal = 0.0
        for target, (tx, ty) in enumerate(layout):
            squared = 0.0
            for source, (sx, sy) in enumerate(layout):
                if source == target:
                    continue
                dx, dy = tx - sx, ty - sy
                downstream = cosine * dx + sine * dy
                crosswind = abs(-sine * dx + cosine * dy)
                if (
                    downstream > 0.0
                    and crosswind <= RADIUS + 0.075 * downstream
                ):
                    deficit = 2.0 * INDUCTION / (
                        1.0 + 0.075 * downstream / RADIUS
                    ) ** 2
                    squared += deficit**2
            subtotal += turbine_power(
                scale * max(0.0, 1.0 - math.sqrt(squared))
            )
        expected += probability * subtotal
    return expected, 1.0 / expected, violation


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", type=Path, required=True)
    args = parser.parse_args()
    fixtures = (
        ("t03_kusiak_s1_n2", [(-250.0, 0.0), (250.0, 0.0)]),
        ("t03_kusiak_s1_n3", [(-400.0, 0.0), (0.0, 0.0), (400.0, 0.0)]),
        ("t03_kusiak_s2_n2", [(0.0, -250.0), (0.0, 250.0)]),
        ("t03_kusiak_s2_n3", [(0.0, 0.0), (10.0, 0.0), (600.0, 0.0)]),
    )
    for problem, layout in fixtures:
        completed = subprocess.run(
            [
                str(args.binary),
                "--problem",
                problem,
                "--evaluate-layout",
                ";".join(f"{x},{y}" for x, y in layout),
            ],
            check=True,
            capture_output=True,
            text=True,
        )
        observed = json.loads(completed.stdout)
        expected = evaluate(problem, layout)
        for field, target in zip(
            (
                "expected_power_kw",
                "inverse_power",
                "constraint_violation",
            ),
            expected,
        ):
            if not math.isclose(
                observed[field],
                target,
                rel_tol=3.0e-12,
                abs_tol=1.0e-9,
            ):
                raise RuntimeError(
                    f"{problem}/{field}: C++={observed[field]} oracle={target}"
                )
    print(
        "core99_t03_h5_pass "
        f"fixtures={len(fixtures)} independent_python_oracle=true"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
