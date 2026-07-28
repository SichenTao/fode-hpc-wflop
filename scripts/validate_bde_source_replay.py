#!/usr/bin/env python3
"""Validate the BDE source-replay C++ profile against an independent scalar oracle."""

from __future__ import annotations

import argparse
import json
import math
import subprocess
import tempfile
from pathlib import Path


ROTOR_RADIUS = 77.0 / 2.0
ENTRAINMENT = 0.5 / math.log(80.0 / (0.25 * 0.001))


def interaction_area(dx: float, wake_radius: float) -> float:
    radius = ROTOR_RADIUS
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
        return math.pi * radius * radius - (
            beta * radius * radius
            + wake_radius * dx * math.sin(alpha)
            - alpha * wake_radius * wake_radius
        )
    return math.pi * radius * radius


def deficiency(dx: float, dy: float) -> float:
    if dy == 0.0:
        return 0.0
    wake_radius = ROTOR_RADIUS + ENTRAINMENT * dy
    return (
        (2.0 / 3.0)
        * ROTOR_RADIUS
        * ROTOR_RADIUS
        / (wake_radius * wake_radius)
        * interaction_area(dx, wake_radius)
        / (math.pi * ROTOR_RADIUS * ROTOR_RADIUS)
    )


def turbine_power(velocity: float) -> float:
    if velocity < 2.0:
        return 0.0
    if velocity < 12.8:
        return 0.3 * velocity**3
    if velocity < 18.0:
        return 629.1
    return 0.0


def evaluate(case: dict[str, object], layout: list[int]) -> float:
    cols = int(case["cols"])
    cell_width = float(case["cell_width"])
    coordinates = []
    for cell_1based in layout:
        cell = cell_1based - 1
        row, col = divmod(cell, cols)
        coordinates.append(
            (
                col * cell_width + 0.5 * cell_width,
                row * cell_width + 0.5 * cell_width,
            )
        )
    theta = [float(value) for value in case["wind_directions_rad"]]
    speeds = [float(value) for value in case["wind_speeds_mps"]]
    probabilities = case["joint_probabilities"]
    accumulated = [0.0] * len(layout)
    for direction, angle in enumerate(theta):
        cosine = math.cos(angle)
        sine = math.sin(angle)
        transformed = [
            (
                cosine * x - sine * y,
                sine * x + cosine * y,
            )
            for x, y in coordinates
        ]
        upstream = sorted(
            range(len(layout)),
            key=lambda turbine: -transformed[turbine][1],
        )
        wake = [0.0] * len(layout)
        for downstream_position in range(1, len(layout)):
            downstream = upstream[downstream_position]
            squared_sum = 0.0
            for upstream_position in range(downstream_position + 1):
                source = upstream[upstream_position]
                dx = abs(
                    transformed[downstream][0] - transformed[source][0]
                )
                dy = abs(
                    transformed[downstream][1] - transformed[source][1]
                )
                loss = deficiency(dx, dy)
                squared_sum += loss * loss
            wake[downstream] = math.sqrt(squared_sum)
        for turbine, loss in enumerate(wake):
            for speed_index, speed in enumerate(speeds):
                accumulated[turbine] += turbine_power((1.0 - loss) * speed) * float(
                    probabilities[direction][speed_index]
                )
    return math.fsum(sorted(accumulated))


def run(
    binary: Path,
    cases: Path,
    case_id: str,
    workers: int,
    output: Path,
) -> dict[str, object]:
    subprocess.run(
        [
            str(binary),
            "--algorithm",
            "bde",
            "--problem",
            "bde2025_source_replay_ws1_ws4",
            "--cases",
            str(cases),
            "--case",
            case_id,
            "--seed",
            "20260729",
            "--physical-fes",
            "75",
            "--workers",
            str(workers),
            "--output",
            str(output),
        ],
        check=True,
    )
    return json.loads(output.read_text())


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--cases", type=Path, required=True)
    parser.add_argument("--workers", type=int, default=4)
    arguments = parser.parse_args()
    manifest = json.loads(arguments.cases.read_text())
    cases_by_id = {case["case_id"]: case for case in manifest["cases"]}
    case_ids = ["BDE-S-WS1-tn30", "BDE-D-WS4-tn40"]

    with tempfile.TemporaryDirectory(prefix="bde-source-replay-") as temporary:
        temporary_root = Path(temporary)
        for case_id in case_ids:
            serial = run(
                arguments.binary,
                arguments.cases,
                case_id,
                1,
                temporary_root / f"{case_id}-serial.json",
            )
            parallel = run(
                arguments.binary,
                arguments.cases,
                case_id,
                arguments.workers,
                temporary_root / f"{case_id}-parallel.json",
            )
            scientific_fields = (
                "effective_semantics_id",
                "problem_id",
                "problem_semantics_id",
                "physical_fes",
                "best_expected_power_kw",
                "best_layout_1based",
            )
            for field in scientific_fields:
                if serial[field] != parallel[field]:
                    raise RuntimeError(
                        f"{case_id}: serial/parallel mismatch in {field}"
                    )
            if serial["problem_semantics_id"] != (
                "bde2025_source_replay_ws1_ws4_v1"
            ):
                raise RuntimeError(f"{case_id}: wrong problem semantics")
            oracle = evaluate(
                cases_by_id[case_id],
                [int(value) for value in serial["best_layout_1based"]],
            )
            cpp = float(serial["best_expected_power_kw"])
            tolerance = 1.0e-9 * max(1.0, abs(oracle))
            if abs(cpp - oracle) > tolerance:
                raise RuntimeError(
                    f"{case_id}: C++ {cpp} differs from oracle {oracle}"
                )
    print(
        "bde_source_replay_validation_pass "
        f"cases={len(case_ids)} workers=1,{arguments.workers} "
        "physical_fes_per_run=75"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
