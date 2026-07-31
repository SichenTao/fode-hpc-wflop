#!/usr/bin/env python3
"""
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: independent T82 paper-equation H5 oracle
Paper/DOI: Wind Farm Layout Optimization to Minimize the Wake-Induced
Turbulence Effect on Wind Turbines; 10.1016/j.apenergy.2022.119599
Public source, missing assets, conflicts, reconstruction completion,
semantic IDs, and claim boundary:
hpc/core99_cpp/include/core99/cao_t82.hpp
Independence: this script re-derives the wind states, rotor quadrature,
Eqs. (1)--(18), constraints, and all three reference-layout objectives
without importing or linking the production C++ implementation
Controlling contract: shared/contracts/core99_t82_cao_2022.json
Claim boundary: equation and protocol oracle, not author-result replay
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
"""

from __future__ import annotations

import argparse
import json
import math
from pathlib import Path
import subprocess


SQRT_TWO_LOG_TWO = math.sqrt(2.0 * math.log(2.0))
ROTOR_SAMPLES = [
    (
        math.sqrt((ring + 0.5) / 2.0)
        * math.cos((angle + 0.5 * ring) * math.pi / 2.0),
        math.sqrt((ring + 0.5) / 2.0)
        * math.sin((angle + 0.5 * ring) * math.pi / 2.0),
    )
    for ring in range(2)
    for angle in range(4)
]


def invoke(binary: Path, *arguments: str) -> dict:
    completed = subprocess.run(
        [str(binary), *arguments],
        check=True,
        capture_output=True,
        text=True,
        timeout=600,
    )
    return json.loads(completed.stdout)


def turbine_types(case_name: str) -> list[tuple[float, float, float, float]]:
    if case_name != "zhuanghe":
        return [(40.0, 60.0, 0.88, 0.0)]
    return [
        (121.0, 100.0, 0.8, 3000.0),
        (140.0, 100.0, 0.8, 3300.0),
        (171.0, 100.0, 0.8, 6450.0),
    ]


def wind_states(case_name: str) -> list[tuple[float, float, float, float]]:
    if case_name == "ideal_i":
        return [(0.0, 12.0, 0.1, 1.0)]
    if case_name == "ideal_ii":
        totals = [0.024] * 27 + [
            0.026, 0.033, 0.036, 0.048, 0.059,
            0.047, 0.036, 0.032, 0.025,
        ]
        middles = [0.008] * 27 + [
            0.010, 0.012, 0.015, 0.014, 0.020,
            0.014, 0.015, 0.012, 0.010,
        ]
        normalization = sum(totals)
        result = []
        for direction, (total, middle) in enumerate(zip(totals, middles)):
            low = 0.004
            high = total - low - middle
            angle = 10.0 * direction
            result.extend([
                (angle, 8.0, 0.1, low / normalization),
                (angle, 12.0, 0.1, middle / normalization),
                (angle, 17.0, 0.1, high / normalization),
            ])
        return result
    direction_weights = [
        0.140, 0.060, 0.030, 0.025,
        0.025, 0.030, 0.040, 0.055,
        0.060, 0.060, 0.045, 0.025,
        0.020, 0.040, 0.075, 0.120,
    ]
    edges = [0.0, 3.0, 5.0, 7.0, 9.0, 12.0, 25.0]
    representatives = [1.8, 4.1, 6.0, 8.0, 10.4, 14.5]
    direction_total = sum(direction_weights)
    scale = 6.9 / math.gamma(1.5)

    def cdf(speed: float) -> float:
        return 0.0 if speed <= 0.0 else 1.0 - math.exp(-(speed / scale) ** 2)

    truncation = cdf(25.0)
    result = []
    for direction, weight in enumerate(direction_weights):
        for speed_bin, speed in enumerate(representatives):
            speed_probability = (
                cdf(edges[speed_bin + 1]) - cdf(edges[speed_bin])
            ) / truncation
            turbulence = max(
                0.06,
                min(
                    0.95,
                    0.07
                    + 0.88 * math.exp(-speed / 2.0)
                    + 0.018
                    * math.exp(-0.5 * ((speed - 22.5) / 1.2) ** 2),
                ),
            )
            result.append((
                22.5 * direction,
                speed,
                turbulence,
                weight / direction_total * speed_probability,
            ))
    return result


def mtg_deficit(
    downstream: float,
    radial: float,
    rotor_radius: float,
    thrust: float,
    expansion: float,
) -> float:
    if downstream <= 0.0:
        return 0.0
    width = expansion * downstream / rotor_radius + 1.0
    radical = max(0.0, min(1.0, 1.0 - 2.0 * thrust / width**2))
    return (
        1.0 - math.sqrt(radical)
    ) * math.exp(-2.0 * radial**2 / (width**2 * rotor_radius**2))


def added_turbulence(
    downstream: float,
    crosswind: float,
    vertical: float,
    diameter: float,
    thrust: float,
    ambient: float,
    upstream_turbulence: float,
) -> float:
    root = math.sqrt(max(1.0e-12, 1.0 - thrust))
    epsilon = 0.2 * diameter * math.sqrt(0.5 * (1.0 - root) / root)
    growth = 0.3837 * upstream_turbulence + 0.003678
    sigma_y = growth * downstream + epsilon
    half_width = SQRT_TWO_LOG_TWO * sigma_y
    sigma_t = sigma_y / SQRT_TWO_LOG_TWO
    normalized = downstream / diameter
    maximum = 1.0 / (
        2.3 * thrust ** -1.2
        + ambient**0.1 * normalized
        + 0.7 * thrust ** -3.2 * ambient ** -0.45
        * (1.0 + normalized) ** -2.0
    )
    radius = math.hypot(crosswind, vertical)
    if radius < half_width:
        shape = 1.0 - 0.15 * (
            1.0 + math.cos(math.pi * radius / half_width)
        )
        k_one = math.sin(0.5 * math.pi * radius / half_width)
    else:
        shape = math.exp(
            -(radius - half_width) ** 2 / (2.0 * sigma_t**2)
        )
        k_one = 1.0
    gaussian = math.exp(
        -(radius - half_width) ** 2 / (2.0 * sigma_t**2)
    )
    sine_alpha = vertical / radius if radius > 0.0 else 0.0
    ground = (
        ambient * (0.23 if vertical >= 0.0 else -1.23)
        * sine_alpha * k_one * gaussian
    )
    return max(0.0, maximum * shape + ground)


def power(case_name: str, type_id: int, speed: float) -> float:
    if case_name != "zhuanghe":
        return 0.3 * speed**3 if speed > 0.0 else 0.0
    rated = turbine_types(case_name)[type_id][3]
    if speed < 3.0 or speed > 25.0:
        return 0.0
    if speed >= 12.0:
        return rated
    return rated * ((speed - 3.0) / 9.0) ** 3


def oracle(case_name: str, layout: list[list[float]]) -> dict[str, float]:
    types = turbine_types(case_name)
    count = len(layout)
    comprehensive = [0.0] * count
    expected_power = 0.0
    for direction, speed, ambient, probability in wind_states(case_name):
        angle = math.radians(270.0 - direction)
        cosine, sine = math.cos(angle), math.sin(angle)
        along = [cosine * row[0] + sine * row[1] for row in layout]
        across = [-sine * row[0] + cosine * row[1] for row in layout]
        order = sorted(range(count), key=lambda index: (along[index], index))
        inflow_turbulence = [ambient] * count
        state_power = 0.0
        for downstream_position, downstream in enumerate(order):
            down_type = types[int(layout[downstream][2])]
            maximum_added = [0.0] * len(ROTOR_SAMPLES)
            velocity_deficit_squared = 0.0
            for upstream in order[:downstream_position]:
                distance = along[downstream] - along[upstream]
                cross = across[downstream] - across[upstream]
                up_type = types[int(layout[upstream][2])]
                expansion = 2.0 * (
                    0.3837 * inflow_turbulence[upstream] + 0.003678
                )
                pair = 0.0
                for sample, unit in enumerate(ROTOR_SAMPLES):
                    sample_cross = cross + unit[0] * 0.5 * down_type[0]
                    sample_vertical = (
                        down_type[1] - up_type[1]
                        + unit[1] * 0.5 * down_type[0]
                    )
                    radial = math.hypot(sample_cross, sample_vertical)
                    deficit = mtg_deficit(
                        distance, radial, 0.5 * up_type[0],
                        up_type[2], expansion,
                    )
                    pair += deficit**2 / len(ROTOR_SAMPLES)
                    maximum_added[sample] = max(
                        maximum_added[sample],
                        added_turbulence(
                            distance, sample_cross, sample_vertical,
                            up_type[0], up_type[2], ambient,
                            inflow_turbulence[upstream],
                        ),
                    )
                velocity_deficit_squared += pair
            mean_turbulence = sum(
                math.hypot(value, ambient) for value in maximum_added
            ) / len(ROTOR_SAMPLES)
            inflow_turbulence[downstream] = mean_turbulence
            comprehensive[downstream] += probability * mean_turbulence
            inflow_speed = speed * (
                1.0 - min(0.999, math.sqrt(velocity_deficit_squared))
            )
            state_power += power(
                case_name, int(layout[downstream][2]), inflow_speed
            )
        expected_power += probability * state_power
    return {
        "expected_power_kw": expected_power,
        "maximum_comprehensive_turbulence": max(comprehensive),
    }


def close(left: float, right: float) -> bool:
    return abs(left - right) <= 2.0e-12 * max(
        1.0, abs(left), abs(right)
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", type=Path, required=True)
    arguments = parser.parse_args()
    for case_name in ("ideal_i", "ideal_ii", "zhuanghe"):
        payload = invoke(
            arguments.binary, "--mode", "evaluate", "--case", case_name
        )
        expected = oracle(case_name, payload["layout"])
        actual = payload["evaluation"]
        if not actual["feasible"]:
            raise RuntimeError(f"{case_name}: reference layout infeasible")
        for field in (
            "expected_power_kw",
            "maximum_comprehensive_turbulence",
        ):
            if not close(expected[field], actual[field]):
                raise RuntimeError(
                    f"{case_name}: {field} mismatch "
                    f"{expected[field]} != {actual[field]}"
                )
    smoke = invoke(
        arguments.binary,
        "--mode", "optimize",
        "--case", "ideal_i",
        "--workers", "4",
        "--population", "8",
        "--generations", "1",
        "--seed", "82119599",
    )
    if (
        smoke["physical_fes"] != 16
        or smoke["observed_workers"] < 2
        or not smoke["front"]
    ):
        raise RuntimeError("T82 bounded optimization/HPC smoke failed")
    print("core99_t82_h5_pass cases=3 equation_oracle=independent")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
