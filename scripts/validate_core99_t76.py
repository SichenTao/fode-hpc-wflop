#!/usr/bin/env python3
"""
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: independent T76 paper-case, power-law, spacing,
partial-overlap Jensen/RSS, and one/all-worker H5 validator
Paper DOI: 10.1016/j.energy.2018.11.073
Public source: no target source, manufacturer arrays, or Sha Chau array.
Missing information and declared completion:
hpc/core99_cpp/include/core99/sun_t76.hpp
Independence: this script re-derives the published fixed Case-1 physics and
power-anchor completion from executable JSON; it does not link C++ objects.
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


def power(turbine: dict, speed: float) -> float:
    if speed <= turbine["cut_in_mps"] or speed > turbine["cut_out_mps"]:
        return 0.0
    if speed >= turbine["rated_speed_mps"]:
        return turbine["rated_power_kw"]
    speed3 = speed**3
    if speed <= turbine["anchor_speed_mps"]:
        cut3 = turbine["cut_in_mps"] ** 3
        anchor3 = turbine["anchor_speed_mps"] ** 3
        return turbine["anchor_power_kw"] * (speed3 - cut3) / (anchor3 - cut3)
    anchor3 = turbine["anchor_speed_mps"] ** 3
    rated3 = turbine["rated_speed_mps"] ** 3
    return turbine["anchor_power_kw"] + (
        turbine["rated_power_kw"] - turbine["anchor_power_kw"]
    ) * (speed3 - anchor3) / (rated3 - anchor3)


def axial(turbine: dict) -> float:
    area = math.pi * turbine["diameter_m"] ** 2 / 4.0
    cp = max(0.02, min(
        0.58,
        turbine["anchor_power_kw"] * 1000.0
        / (0.5 * 1.225 * area * turbine["anchor_speed_mps"] ** 3),
    ))
    low, high = 0.0, 1.0 / 3.0
    for _ in range(80):
        middle = (low + high) / 2.0
        if 4.0 * middle * (1.0 - middle) ** 2 < cp:
            low = middle
        else:
            high = middle
    return (low + high) / 2.0


def overlap(first: float, second: float, distance: float) -> float:
    if distance >= first + second:
        return 0.0
    if distance <= abs(first - second):
        return math.pi * min(first, second) ** 2
    first_angle = math.acos(max(-1.0, min(
        1.0,
        (distance**2 + first**2 - second**2) / (2.0 * distance * first),
    )))
    second_angle = math.acos(max(-1.0, min(
        1.0,
        (distance**2 + second**2 - first**2) / (2.0 * distance * second),
    )))
    triangle = 0.5 * math.sqrt(max(
        0.0,
        (-distance + first + second)
        * (distance + first - second)
        * (distance - first + second)
        * (distance + first + second),
    ))
    return first**2 * first_angle + second**2 * second_angle - triangle


def case1_evaluation(catalog: list[dict], payload: dict, directional: bool) -> dict:
    turbine = catalog[3]
    layout = [tuple(point) for point in payload["layout"]]
    active = [True] * len(layout)
    if not directional:
        for left in range(len(layout)):
            for right in range(left + 1, len(layout)):
                if math.dist(layout[left], layout[right]) < 5.0 * turbine["diameter_m"]:
                    active[left] = active[right] = False
    else:
        for source, source_point in enumerate(layout):
            for target, target_point in enumerate(layout):
                if source == target:
                    continue
                dx = target_point[0] - source_point[0]
                dy = target_point[1] - source_point[1]
                along, across = dy, -dx
                if math.hypot(dx, dy) < 1.0e-9 or (
                    0.0 < along < 5.0 * turbine["diameter_m"]
                    and abs(across) < 1.5 * turbine["diameter_m"]
                ):
                    active[target] = False
    factors = [1.0] * len(layout)
    induction = axial(turbine)
    source_radius = target_radius = turbine["diameter_m"] / 2.0
    expansion = 0.075
    for target, target_point in enumerate(layout):
        if not active[target]:
            factors[target] = 0.0
            continue
        squared = 0.0
        for source, source_point in enumerate(layout):
            if source == target or not active[source]:
                continue
            downstream = target_point[1] - source_point[1]
            if downstream <= 0.0:
                continue
            across = -(target_point[0] - source_point[0])
            wake_radius = source_radius + expansion * downstream
            fraction = overlap(wake_radius, target_radius, abs(across)) / (
                math.pi * target_radius**2
            )
            deficit = (
                2.0 * induction * source_radius**2 / wake_radius**2 * fraction
            )
            squared += deficit**2
        factors[target] = max(0.0, 1.0 - math.sqrt(squared))
    outputs = [
        power(turbine, 8.0 * factors[index]) if active[index] else 0.0
        for index in range(len(layout))
    ]
    theoretical = len(layout) * power(turbine, 8.0) / 1000.0
    return {
        "expected_power_mw": sum(outputs) / 1000.0,
        "theoretical_no_wake_power_mw": theoretical,
        "utilization_rate": sum(outputs) / 1000.0 / theoretical,
        "minimum_turbine_power_kw": min(outputs),
        "maximum_turbine_power_kw": max(outputs),
        "inactive_turbine_states": sum(not value for value in active),
    }


def close(left: float, right: float) -> bool:
    return abs(left - right) <= 5.0e-11 * max(1.0, abs(left), abs(right))


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", required=True, type=Path)
    arguments = parser.parse_args()
    binary = arguments.binary.resolve()

    listing = invoke(binary, "--mode", "list-cases")
    assert listing["role_count"] == 6
    assert listing["optimized_role_count"] == 4
    assert len(set(listing["paper_case_roles"])) == 6

    catalog = invoke(binary, "--mode", "catalog")["turbines"]
    assert len(catalog) == 5
    for turbine in catalog:
        assert close(
            power(turbine, turbine["anchor_speed_mps"]),
            turbine["anchor_power_kw"],
        )
        assert power(turbine, turbine["rated_speed_mps"]) == turbine["rated_power_kw"]

    maximum_error = 0.0
    for case, directional in (
        ("case1_omnidirectional_aligned", False),
        ("case1_directional_aligned", True),
    ):
        payload = invoke(binary, "--mode", "evaluate", "--case", case)
        expected = case1_evaluation(catalog, payload, directional)
        observed = payload["evaluation"]
        for field, value in expected.items():
            if isinstance(value, int):
                assert observed[field] == value
            else:
                maximum_error = max(maximum_error, abs(observed[field] - value))
                assert close(observed[field], value), (case, field, observed[field], value)

    case4 = invoke(binary, "--mode", "inspect", "--case", "case4_sha_chau_multitype_mpga")
    assert case4["wind_state_count"] == 36 * 27
    assert close(case4["wind_probability_sum"], 1.0)

    common = (
        "--mode", "optimize",
        "--case", "case2_directional_mpga",
        "--demes", "2",
        "--individuals", "4",
        "--unchanged-generations", "10",
        "--max-generations", "5",
        "--migration-period", "2",
        "--seed", "76019",
    )
    serial = invoke(binary, *common, "--workers", "1")
    parallel = invoke(binary, *common, "--workers", "4")
    assert serial["physical_fes"] == 8 + 5 * 6
    assert serial["physical_fes"] == parallel["physical_fes"]
    assert serial["scientific_hash"] == parallel["scientific_hash"]
    assert serial["best_evaluation"] == parallel["best_evaluation"]
    assert parallel["observed_workers"] >= 2

    print(json.dumps({
        "status": "pass",
        "paper_case_roles": 6,
        "published_turbine_types": 5,
        "independent_fixed_physics_cases": 2,
        "maximum_absolute_equation_error": maximum_error,
        "schedule_independent": True,
        "scientific_hash": parallel["scientific_hash"],
    }, sort_keys=True))


if __name__ == "__main__":
    main()
