#!/usr/bin/env python3
"""
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: independent T67 case, catalog, equation, and
one/all-worker H5 validator
Paper DOI: 10.1016/j.renene.2016.10.038
Public source: no target MATLAB source, full turbine table, or fitted curves.
Missing information and declared completion:
hpc/core99_cpp/include/core99/abdulrahman_t67.hpp
Independence: re-derives log-law inflow, fifth-degree power and CT completion,
Frandsen/Jensen partial overlap, quadratic wake superposition, capacity
factor, and TCI/output power from executable inputs without linking C++.
Claim boundary: equation and interface validation of the declared academic
reconstruction, not unavailable author-array or optimum replay.
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
"""

from __future__ import annotations

import argparse
import json
import math
import subprocess
from pathlib import Path


def invoke(binary: Path, *arguments: str) -> dict:
    completed = subprocess.run(
        [str(binary), *arguments],
        check=True,
        capture_output=True,
        text=True,
    )
    return json.loads(completed.stdout)


def overlap(r1: float, r2: float, distance: float) -> float:
    if distance >= r1 + r2:
        return 0.0
    if distance <= abs(r1 - r2):
        return math.pi * min(r1, r2) ** 2
    first = math.acos(max(-1.0, min(
        1.0, (distance**2 + r1**2 - r2**2) / (2 * distance * r1)
    )))
    second = math.acos(max(-1.0, min(
        1.0, (distance**2 + r2**2 - r1**2) / (2 * distance * r2)
    )))
    triangle = 0.5 * math.sqrt(max(
        0.0,
        (-distance + r1 + r2)
        * (distance + r1 - r2)
        * (distance - r1 + r2)
        * (distance + r1 + r2),
    ))
    return r1 * r1 * first + r2 * r2 * second - triangle


def power(turbine: dict, speed: float) -> float:
    if speed < turbine["cut_in_mps"] or speed > turbine["cut_out_mps"]:
        return 0.0
    if speed >= turbine["rated_speed_mps"]:
        return turbine["rated_power_mw"]
    x = (
        (speed - turbine["cut_in_mps"])
        / (turbine["rated_speed_mps"] - turbine["cut_in_mps"])
    )
    smooth = x**3 * (10.0 - 15.0 * x + 6.0 * x**2)
    return turbine["rated_power_mw"] * smooth


def thrust(turbine: dict, speed: float) -> float:
    if speed < turbine["cut_in_mps"] or speed > turbine["cut_out_mps"]:
        return 0.05
    area = math.pi * 0.25 * turbine["diameter_m"] ** 2
    cp = max(0.0, min(
        0.59,
        power(turbine, speed) * 1.0e6
        / max(1.0, 0.5 * 1.225 * area * speed**3),
    ))
    value = (
        0.10 + 2.20 * cp - 2.10 * cp**2 + 0.80 * cp**3
        - 0.20 * cp**4 + 0.02 * cp**5
    )
    return max(0.08, min(0.88, value))


def coordinates(
    layout: str,
    spacing: int,
    decision: dict,
) -> list[tuple[float, float]]:
    if layout == "til":
        return [(0.0, y) for y in decision["y_m"]]
    downwind = spacing * 112.0
    result = []
    for row in range(6):
        for column in range(3):
            x = column * 336.0
            if layout == "staggered" and row % 2:
                x += 168.0
            result.append((x, row * downwind))
    return result


def evaluate(
    catalog: list[dict],
    layout: str,
    spacing: int,
    speed: float,
    terrain: str,
    decision: dict,
) -> dict[str, float]:
    points = coordinates(layout, spacing, decision)
    roughness = 0.3 if terrain == "onshore" else 0.0002
    order = sorted(range(len(points)), key=lambda index: points[index][1])
    outputs = [0.0] * len(points)
    rated = 0.0
    cost = 0.0
    for rank, index in enumerate(order):
        turbine = catalog[decision["turbine_code"][index] - 1]
        hub = decision["hub_height_m"][index]
        ambient = speed * math.log(hub / roughness) / math.log(60 / roughness)
        squared = 0.0
        for prior in range(rank):
            upstream = order[prior]
            downstream_distance = points[index][1] - points[upstream][1]
            if downstream_distance <= 0:
                continue
            source = catalog[decision["turbine_code"][upstream] - 1]
            source_hub = decision["hub_height_m"][upstream]
            source_speed = (
                speed * math.log(source_hub / roughness)
                / math.log(60 / roughness)
            )
            ct = thrust(source, source_speed)
            axial = 0.5 * (1.0 - math.sqrt(max(0.0, 1.0 - ct)))
            source_radius = source["diameter_m"] / 2
            expanded = source_radius * math.sqrt(
                (1.0 - axial) / max(1.0e-9, 1.0 - 2.0 * axial)
            )
            expansion = 0.5 / math.log(source_hub / roughness)
            wake_radius = expanded + expansion * downstream_distance
            target_radius = turbine["diameter_m"] / 2
            center_distance = math.hypot(
                points[index][0] - points[upstream][0],
                hub - source_hub,
            )
            area_overlap = overlap(
                wake_radius, target_radius, center_distance
            )
            deficit = (
                (1.0 - math.sqrt(max(0.0, 1.0 - ct)))
                / (1.0 + expansion * downstream_distance / expanded) ** 2
                * area_overlap / (math.pi * target_radius**2)
            )
            squared += deficit**2
        effective = max(0.0, ambient * (1.0 - math.sqrt(squared)))
        outputs[index] = power(turbine, effective)
        rated += turbine["rated_power_mw"]
        tower = 0.12 if terrain == "onshore" else 0.0565
        cost += turbine["rated_power_mw"] * (
            1.0 + tower / 80.0 * (hub - 80.0)
        )
    cost /= 1.0 - (0.15 if terrain == "onshore" else 0.25)
    total = sum(outputs)
    return {
        "total_power_mw": total,
        "rated_power_mw": rated,
        "capacity_factor": total / rated,
        "total_cost_index": cost,
        "total_cost_index_per_output_power": cost / total,
    }


def close(left: float, right: float) -> bool:
    return abs(left - right) <= 5.0e-11 * max(
        1.0, abs(left), abs(right)
    )


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", required=True, type=Path)
    arguments = parser.parse_args()
    binary = arguments.binary.resolve()

    listing = invoke(binary, "--mode", "list-cases")
    assert listing["role_count"] == 162
    assert listing["unique_instance_count"] == 162
    assert len(set(listing["paper_case_roles"])) == 162

    catalog = invoke(binary, "--mode", "catalog")["turbines"]
    assert len(catalog) == 61
    assert all(
        catalog[index - 1]["rated_power_mw"]
        <= catalog[index]["rated_power_mw"]
        for index in range(1, 61)
    )
    anchors = {
        1: (1.5, 77.0, 13.0),
        6: (1.5, 82.0, 12.0),
        19: (1.8, 100.0, 12.0),
        45: (2.5, 115.0, 12.0),
        56: (3.0, 112.0, 12.0),
        61: (3.075, 112.0, 13.0),
    }
    for code, expected in anchors.items():
        observed = catalog[code - 1]
        assert (
            observed["rated_power_mw"],
            observed["diameter_m"],
            observed["rated_speed_mps"],
        ) == expected

    maximum_error = 0.0
    for layout, spacing, speed, terrain in (
        ("til", 4, 8, "onshore"),
        ("array", 3, 10, "offshore"),
        ("staggered", 5, 12, "onshore"),
    ):
        common = (
            "--layout", layout,
            "--spacing", str(spacing),
            "--reference-speed", str(speed),
            "--terrain", terrain,
            "--objective", "min_tciop",
        )
        fixture = invoke(binary, "--mode", "evaluate", *common)
        expected = evaluate(
            catalog,
            layout,
            spacing,
            float(speed),
            terrain,
            fixture["decision"],
        )
        observed = fixture["evaluation"]
        assert observed["feasible"]
        for field, value in expected.items():
            maximum_error = max(maximum_error, abs(observed[field] - value))
            assert close(observed[field], value), (
                layout, field, observed[field], value
            )

    common_run = (
        "--mode", "optimize",
        "--layout", "staggered",
        "--spacing", "4",
        "--reference-speed", "10",
        "--terrain", "offshore",
        "--objective", "min_tciop",
        "--population", "64",
        "--generations", "20",
        "--stall-generations", "100",
        "--seed", "67017",
    )
    serial = invoke(binary, *common_run, "--workers", "1")
    parallel = invoke(binary, *common_run, "--workers", "4")
    assert serial["physical_fes"] == 64 + 20 * 60
    assert serial["physical_fes"] == parallel["physical_fes"]
    assert serial["scientific_hash"] == parallel["scientific_hash"]
    assert serial["best_evaluation"] == parallel["best_evaluation"]
    assert parallel["observed_workers"] >= 2

    print(json.dumps({
        "status": "pass",
        "paper_case_roles": 162,
        "commercial_turbines": 61,
        "published_catalog_anchors": 6,
        "independent_physics_cases": 3,
        "maximum_absolute_equation_error": maximum_error,
        "schedule_independent": True,
        "scientific_hash": parallel["scientific_hash"],
    }, sort_keys=True))


if __name__ == "__main__":
    main()
