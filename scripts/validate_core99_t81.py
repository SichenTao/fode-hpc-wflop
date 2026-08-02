#!/usr/bin/env python3
"""
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: independent T81 wave-chart, case, constraint, two-stage,
and one/all-worker H5 validator
Paper DOI: 10.1016/j.apenergy.2021.117947
Public source: FLORIS v2.4 is a cited dependency; no target source or native
NCEP, bathymetry, mesh, MIKE21/SWAN, or wave-chart arrays were located.
Missing information and deterministic completions:
hpc/core99_cpp/include/core99/ti_t81.hpp
Independence: this script reconstructs both frozen wave charts, boundary and
spacing checks from executable layouts without linking the C++ implementation.
Claim boundary: equation, interface, constraint, and schedule validation of
the declared academic reconstruction, not unavailable author-array replay.
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
"""

from __future__ import annotations

import argparse
import json
import math
import subprocess
from pathlib import Path


METHOD_ID = "t81_multistart_slsqp_wave_aep_declared_v1"
PROBLEM_ID = "t81_twofarm_inhomogeneous_wave_declared_v1"
PROTOCOL_ID = "t81_twofarm_4alpha_25seed_15start_v1"


def invoke(binary: Path, *arguments: str) -> dict:
    completed = subprocess.run(
        [str(binary), *arguments],
        check=True,
        capture_output=True,
        text=True,
    )
    return json.loads(completed.stdout)


def close(left: float, right: float, tolerance: float = 2.0e-10) -> bool:
    return abs(left - right) <= tolerance * max(1.0, abs(left), abs(right))


def wave_number(depth: float, period: float) -> float:
    gravity = 9.80665
    omega = 2.0 * math.pi / period
    value = omega * omega / gravity
    for _ in range(40):
        kd = value * depth
        function = gravity * value * math.tanh(kd) - omega * omega
        derivative = gravity * (
            math.tanh(kd) + kd / math.cosh(kd) ** 2
        )
        value = max(1.0e-8, value - function / derivative)
    return value


def mild_wave_raw(x_coordinate: float) -> float:
    x_coordinate = min(3000.0, max(0.0, x_coordinate))
    depth = 5.0 + 35.0 * x_coordinate / 3000.0
    period = 10.5
    deep_height = 6.78
    gravity = 9.80665
    density = 1025.0
    monopile = 6.0
    number = wave_number(depth, period)
    kd = number * depth
    omega = 2.0 * math.pi / period
    phase_speed = omega / number
    group_factor = 0.5 * (1.0 + 2.0 * kd / math.sinh(2.0 * kd))
    group_speed = phase_speed * group_factor
    deep_group_speed = gravity * period / (4.0 * math.pi)
    predicted = deep_height * math.sqrt(deep_group_speed / group_speed)
    breaking = 1.86 * predicted >= 0.78 * depth
    significant = min(predicted, 0.78 * depth / 1.86) if breaking else predicted
    maximum = 1.86 * significant
    samples = 48
    dz = depth / samples
    regular = 0.0
    for sample in range(samples):
        z = -depth + (sample + 0.5) * dz
        profile = math.cosh(number * (z + depth)) / math.sinh(kd)
        velocity = math.pi * maximum / period * profile
        acceleration = (
            2.0 * math.pi**2 * maximum / period**2 * profile
        )
        drag = 0.5 * 1.2 * density * monopile * velocity**2
        inertia = (
            2.0 * density * (math.pi * monopile**2 / 4.0) * acceleration
        )
        regular += (z + depth) * (drag + inertia) * dz
    impact = 0.0
    if breaking:
        eta = 0.75 * maximum
        envelope = math.exp(-0.5 * ((depth - 12.5) / 2.8) ** 2)
        impact = (
            0.5 * eta * density * (0.5 * monopile) * phase_speed**2
            * 2.0 * math.pi * (depth + 0.5 * eta) * envelope
        )
    completion = 1.0 + 0.20 * ((depth - 22.0) / 18.0) ** 2
    return (regular * completion + impact) / 1.0e6


def mild_wave_chart(x_coordinate: float) -> float:
    position = min(3000.0, max(0.0, x_coordinate)) / 0.25
    left = min(int(position), 12000)
    right = min(left + 1, 12000)
    fraction = position - left
    return (
        mild_wave_raw(left * 0.25)
        + fraction * (mild_wave_raw(right * 0.25) - mild_wave_raw(left * 0.25))
    )


def gaussian2(point: tuple[float, float], x: float, y: float, sx: float, sy: float) -> float:
    dx = (point[0] - x) / sx
    dy = (point[1] - y) / sy
    return math.exp(-0.5 * (dx * dx + dy * dy))


def complex_wave_raw(point: tuple[float, float]) -> float:
    x, y = point
    broad = 0.20 * y / 6500.0 + 0.12 * x / 6500.0
    risky = (
        1.10 * gaussian2(point, 3200.0, 4200.0, 650.0, 900.0)
        + 0.85 * gaussian2(point, 4300.0, 3000.0, 550.0, 750.0)
        + 0.55 * gaussian2(point, 1600.0, 4700.0, 700.0, 650.0)
    )
    lagoon = 0.65 * gaussian2(point, 3100.0, 1300.0, 1400.0, 700.0)
    return 20.0 + 55.0 * max(0.08, 0.55 + broad + risky - lagoon)


def complex_wave_chart(point: tuple[float, float]) -> float:
    position_x = min(6500.0, max(0.0, point[0])) / 25.0
    position_y = min(6500.0, max(0.0, point[1])) / 25.0
    left, bottom = min(int(position_x), 260), min(int(position_y), 260)
    right, top = min(left + 1, 260), min(bottom + 1, 260)
    fx, fy = position_x - left, position_y - bottom
    lower = (
        complex_wave_raw((left * 25.0, bottom * 25.0))
        + fx * (
            complex_wave_raw((right * 25.0, bottom * 25.0))
            - complex_wave_raw((left * 25.0, bottom * 25.0))
        )
    )
    upper = (
        complex_wave_raw((left * 25.0, top * 25.0))
        + fx * (
            complex_wave_raw((right * 25.0, top * 25.0))
            - complex_wave_raw((left * 25.0, top * 25.0))
        )
    )
    return lower + fy * (upper - lower)


def minimum_spacing(layout: list[list[float]]) -> float:
    return min(
        math.dist(layout[left], layout[right])
        for left in range(len(layout))
        for right in range(left + 1, len(layout))
    )


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", required=True, type=Path)
    arguments = parser.parse_args()
    binary = arguments.binary.resolve()

    listing = invoke(binary, "--mode", "list-cases")
    assert listing["role_count"] == 2
    assert listing["baseline_roles"] == 2
    assert listing["coupled_roles"] == 8

    maximum_wave_error = 0.0
    for case, expected_turbines, wave in (
        ("case1_mild_slope", 16, lambda p: mild_wave_chart(p[0])),
        ("case2_complex_terrain", 20, complex_wave_chart),
    ):
        inspect = invoke(binary, "--mode", "inspect", "--case", case)
        assert inspect["turbine_count"] == expected_turbines
        assert inspect["wind_state_count"] == 24
        assert close(inspect["wind_probability_sum"], 1.0)
        payload = invoke(binary, "--mode", "evaluate", "--case", case)
        layout = payload["layout"]
        observed = payload["evaluation"]
        expected_wave = sum(wave(tuple(point)) for point in layout)
        maximum_wave_error = max(
            maximum_wave_error,
            abs(expected_wave - observed["total_wave_load_index"]),
        )
        assert close(expected_wave, observed["total_wave_load_index"])
        assert close(minimum_spacing(layout), observed["minimum_spacing_m"])
        assert observed["spacing_violation_m"] == 0.0
        assert observed["boundary_violation_m"] == 0.0
        assert observed["feasible"] is True
        assert 0.0 < observed["aep_gwh"] < expected_turbines * 5.0 * 8.760

    common = (
        "--mode", "optimize",
        "--case", "case1_mild_slope",
        "--multistarts", "2",
        "--maxeval-per-start", "4",
        "--seed", "81019",
    )
    serial = invoke(binary, *common, "--workers", "1")
    parallel = invoke(binary, *common, "--workers", "2")
    for payload, workers in ((serial, 1), (parallel, 2)):
        assert payload["method_semantic_id"] == METHOD_ID
        assert payload["problem_semantic_id"] == PROBLEM_ID
        assert payload["protocol_semantic_id"] == PROTOCOL_ID
        assert payload["requested_workers"] == workers
        assert len(payload["stages"]) == 5
        for stage in payload["stages"]:
            assert stage["best_evaluation"]["feasible"] is True
            assert stage["successful_starts"] >= 1
            if stage["alpha0"] < 1.0:
                assert stage["alpha1"] + 1.0e-6 >= stage["alpha0"]
    assert serial["physical_fes"] == parallel["physical_fes"]
    assert serial["scientific_hash"] == parallel["scientific_hash"]
    assert serial["stages"] != parallel["stages"]  # timings differ
    assert parallel["observed_workers"] == 2

    print(json.dumps({
        "status": "pass",
        "paper_case_roles": 2,
        "independent_wave_chart_cases": 2,
        "maximum_absolute_wave_chart_error": maximum_wave_error,
        "schedule_independent": True,
        "scientific_hash": parallel["scientific_hash"],
    }, sort_keys=True))


if __name__ == "__main__":
    main()
