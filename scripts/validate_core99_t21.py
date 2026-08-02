#!/usr/bin/env python3
"""
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: independent T21 paper-equation H5 oracle
Paper/DOI: Topology Optimization of Wind Farm Layouts;
10.1016/j.renene.2022.06.019
Public problem source:
https://github.com/byuflowlab/iea37-wflo-casestudies revision
af88908d22795030ac2dfbe37bc38e912aee8ed6
Missing/conflicts/completion:
hpc/core99_cpp/include/core99/pollini_t21.hpp
Independence: this script re-derives grids, spacing constraints, Gaussian
pair deficits, RAMP objective, and gradients without importing production code
Claim boundary: equation oracle, not an optimization-result oracle
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
"""

import argparse
import json
import math
import subprocess


DIRECTIONS = [22.5 * index for index in range(16)]
FREQUENCIES = [
    0.025, 0.024, 0.029, 0.036,
    0.063, 0.065, 0.100, 0.122,
    0.063, 0.038, 0.039, 0.083,
    0.213, 0.046, 0.032, 0.022,
]
WIND_SPEED = 9.8
DIAMETER = 130.0
THRUST = 8.0 / 9.0
TURBULENCE = 0.075
WAKE_EXPANSION = 0.3837 * TURBULENCE + 0.003678
RATED_POWER_W = 3_370_000.0


def grid(case_name):
    radius = 1300 if case_name == "small" else 3000
    start = -1100 if case_name == "small" else -3000
    finish = 1100 if case_name == "small" else 3000
    return [
        (float(x), float(y))
        for x in range(start, finish + 1, 200)
        for y in range(start, finish + 1, 200)
        if x * x + y * y <= radius * radius
    ]


def near_wake_distance():
    root = math.sqrt(1.0 - THRUST)
    x0 = DIAMETER * (1.0 + root) / (
        math.sqrt(2.0)
        * (2.32 * TURBULENCE + 0.154 * (1.0 - root))
    )
    a_value = 2.0 * WAKE_EXPANSION
    b_value = 4.0 * WAKE_EXPANSION**2 * (THRUST - 1.0)
    c_value = 2.0 * math.sqrt(8.0) * WAKE_EXPANSION**2
    return x0 + DIAMETER * (
        a_value - math.sqrt(max(0.0, a_value**2 - b_value))
    ) / c_value


def deficit_squared(upstream, downstream, direction):
    angle = math.radians(270.0 - direction)
    cosine = math.cos(angle)
    sine = math.sin(angle)
    up_along = cosine * upstream[0] + sine * upstream[1]
    down_along = cosine * downstream[0] + sine * downstream[1]
    distance = down_along - up_along
    if distance <= 0.0:
        return 0.0
    up_across = -sine * upstream[0] + cosine * upstream[1]
    down_across = -sine * downstream[0] + cosine * downstream[1]
    root = math.sqrt(1.0 - THRUST)
    x0 = DIAMETER * (1.0 + root) / (
        math.sqrt(2.0)
        * (2.32 * TURBULENCE + 0.154 * (1.0 - root))
    )
    sigma = (
        WAKE_EXPANSION * (max(distance, near_wake_distance()) - x0)
        + DIAMETER / math.sqrt(8.0)
    )
    radical = max(
        0.0,
        min(
            1.0,
            1.0 - THRUST / (8.0 * sigma**2 / DIAMETER**2),
        ),
    )
    lateral = down_across - up_across
    deficit = (
        1.0 - math.sqrt(radical)
    ) * math.exp(-0.5 * lateral**2 / sigma**2)
    return deficit**2


def power(speed):
    if speed < 4.0 or speed >= 25.0:
        return 0.0
    if speed < 9.8:
        return RATED_POWER_W * ((speed - 4.0) / 5.8) ** 3
    return RATED_POWER_W


def power_derivative(speed):
    if speed < 4.0 or speed >= 9.8:
        return 0.0
    fraction = (speed - 4.0) / 5.8
    return 3.0 * RATED_POWER_W * fraction**2 / 5.8


def oracle(case_name, density):
    points = grid(case_name)
    sites = len(points)
    effective = [density] * sites
    objective_gradient = [0.0] * sites
    aep = 0.0
    for direction, frequency in zip(DIRECTIONS, FREQUENCIES):
        annual_scale = 8760.0 * frequency / 1.0e9
        for downstream in range(sites):
            losses = [
                deficit_squared(points[upstream], points[downstream], direction)
                for upstream in range(sites)
            ]
            root_loss = math.sqrt(
                sum(effective[index] * losses[index] for index in range(sites))
            )
            speed = max(0.0, WIND_SPEED * (1.0 - root_loss))
            turbine_power = power(speed)
            aep += annual_scale * effective[downstream] * turbine_power
            objective_gradient[downstream] -= annual_scale * turbine_power
            slope = power_derivative(speed)
            if slope != 0.0 and root_loss > 0.0:
                common = (
                    annual_scale
                    * effective[downstream]
                    * slope
                    * (-0.5 * WIND_SPEED / root_loss)
                )
                for upstream in range(sites):
                    objective_gradient[upstream] -= common * losses[upstream]
    adjacent = sum(
        1
        for left in range(sites)
        for right in range(left + 1, sites)
        if (
            (points[left][0] - points[right][0]) ** 2
            + (points[left][1] - points[right][1]) ** 2
            <= 260.0**2 + 1.0e-9
        )
    )
    minimum = 16 if case_name == "small" else 64
    maximum = 64 if case_name == "small" else 256
    count = density * sites
    return {
        "potential_sites": sites,
        "spacing_pairs": adjacent,
        "aep_gwh": aep,
        "objective": -aep,
        "minimum_count_constraint": minimum / sites - count / sites,
        "maximum_count_constraint": count / sites - maximum / sites,
        "maximum_spacing_constraint": 2.0 * density - 1.0,
        "objective_gradient": objective_gradient,
    }


def close(left, right, relative=2.0e-11):
    return abs(left - right) <= relative * max(1.0, abs(left), abs(right))


def validate_case(binary, case_name, density):
    completed = subprocess.run(
        [
            binary,
            "--case", case_name,
            "--workers", "4",
            "--evaluate-density", str(density),
            "--evaluation-repeats", "2",
        ],
        check=True,
        capture_output=True,
        text=True,
    )
    observed = json.loads(completed.stdout)
    expected = oracle(case_name, density)
    for key in [
        "potential_sites",
        "spacing_pairs",
        "aep_gwh",
        "objective",
        "minimum_count_constraint",
        "maximum_count_constraint",
        "maximum_spacing_constraint",
    ]:
        if not close(float(observed[key]), float(expected[key])):
            raise SystemExit(
                f"T21 {case_name} {key} mismatch: "
                f"{observed[key]} != {expected[key]}"
            )
    observed_gradient = observed["objective_gradient"]
    expected_gradient = expected["objective_gradient"]
    if len(observed_gradient) != len(expected_gradient):
        raise SystemExit(f"T21 {case_name} gradient cardinality mismatch")
    for index, (left, right) in enumerate(
        zip(observed_gradient, expected_gradient)
    ):
        if not close(left, right, 5.0e-10):
            raise SystemExit(
                f"T21 {case_name} gradient[{index}] mismatch: "
                f"{left} != {right}"
            )
    if observed["observed_workers"] < 1:
        raise SystemExit(f"T21 {case_name} did not record worker execution")
    return {
        "case": case_name,
        "aep_gwh": observed["aep_gwh"],
        "potential_sites": observed["potential_sites"],
        "spacing_pairs": observed["spacing_pairs"],
        "maximum_absolute_gradient_error": max(
            abs(left - right)
            for left, right in zip(observed_gradient, expected_gradient)
        ),
    }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", required=True)
    args = parser.parse_args()
    receipts = [
        validate_case(args.binary, "small", 0.2),
        validate_case(args.binary, "large", 0.1805),
    ]
    print(json.dumps({"status": "pass", "cases": receipts}, indent=2))


if __name__ == "__main__":
    main()
