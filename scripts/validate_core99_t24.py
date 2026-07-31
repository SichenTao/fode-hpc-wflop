#!/usr/bin/env python3
"""
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: independent T24 paper-equation H5 oracle
Paper/DOI: Optimization of a Wind Farm Layout to Mitigate the Wind Power
Intermittency; 10.1016/j.apenergy.2024.123383
Public source, missing assets, paper-internal data conflict, reconstruction
completion, semantic IDs, production backend, and claim boundary:
hpc/core99_cpp/include/core99/kim_t24.hpp
Independence: this script re-derives Eqs. (1)--(6) and (21)--(26), the
figure-constrained wind and transition contract, V112 interpolation, all six
reference objectives, and the model-problem powers without importing or
linking the production C++ implementation
Controlling contract: shared/contracts/core99_t24_kim_2024.json
Claim boundary: equation and bounded-protocol oracle, not author source,
original Marado data, private arrays, random states, or numerical replay
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path
import subprocess


DIAMETER = 112.0
HUB_HEIGHT = 84.0
SIDE = 20.0 * DIAMETER
ROUGHNESS = 0.0001
WAKE_GROWTH = 0.0256
SPEEDS = [2.0, 5.0, 7.0, 9.0, 11.0, 13.0, 15.0, 18.0, 23.0]
SPEED_PROBABILITY = [0.07, 0.21, 0.19, 0.16, 0.13, 0.10, 0.07, 0.05, 0.02]
REAL_DIRECTION_PROBABILITY = [
    0.025, 0.020, 0.020, 0.020,
    0.025, 0.030, 0.040, 0.060,
    0.180, 0.120, 0.080, 0.060,
    0.080, 0.160, 0.060, 0.040,
]
POWER = [
    0.0, 0.0, 0.0, 0.0, 0.20, 0.38, 0.66, 1.02, 1.50,
    2.10, 2.58, 2.88, 2.98, 3.00, 3.00, 3.00, 3.00,
    3.00, 3.00, 3.00, 3.00, 3.00, 3.00, 3.00, 3.00, 3.00,
]
THRUST = [
    0.0, 0.0, 0.0, 0.0, 0.86, 0.83, 0.81, 0.82, 0.80,
    0.78, 0.70, 0.49, 0.31, 0.24, 0.18, 0.15, 0.13,
    0.115, 0.10, 0.087, 0.076, 0.066, 0.058, 0.051, 0.045,
    0.040,
]
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
CASES = {
    "uniform_p0": (False, 0.0),
    "uniform_p007": (False, 0.07),
    "uniform_p015": (False, 0.15),
    "real_p0": (True, 0.0),
    "real_p007": (True, 0.07),
    "real_p015": (True, 0.15),
}
PAPER_REFERENCE = {
    "uniform_p0": (31.53, 9.74),
    "uniform_p007": (31.53, 7.15),
    "uniform_p015": (31.53, 5.14),
    "real_p0": (31.21, 9.42),
    "real_p007": (31.21, 6.93),
    "real_p015": (31.21, 5.00),
}
PAPER_MODEL_9MPS = {
    (5.0, 0.0): 3.2624,
    (5.0, 30.0): 6.2943,
    (2.1, 0.0): 4.9137,
    (2.1, 30.0): 5.0164,
    (3.6, 0.0): 4.8730,
    (3.6, 30.0): 6.2626,
}


def invoke(binary: Path, *arguments: str) -> dict:
    completed = subprocess.run(
        [str(binary), *arguments],
        check=True,
        capture_output=True,
        text=True,
        timeout=1800,
    )
    return json.loads(completed.stdout)


def interpolate(table: list[float], speed: float) -> float:
    if speed <= 0.0:
        return table[0]
    if speed >= len(table) - 1:
        return table[-1]
    lower = math.floor(speed)
    fraction = speed - lower
    return table[lower] + fraction * (table[lower + 1] - table[lower])


def power(speed: float) -> float:
    return 0.0 if speed < 4.0 or speed > 25.0 else min(
        3.0, max(0.0, interpolate(POWER, speed))
    )


def thrust(speed: float) -> float:
    return 0.0 if speed < 4.0 or speed > 25.0 else min(
        0.95, max(0.0, interpolate(THRUST, speed))
    )


def reversible_kernel(
    target: list[float],
    proposal: list[list[float]],
) -> list[list[float]]:
    count = len(target)
    result = [[0.0] * count for _ in range(count)]
    for source in range(count):
        outgoing = 0.0
        for target_index in range(count):
            if source == target_index:
                continue
            forward = proposal[source][target_index]
            reverse = proposal[target_index][source]
            if forward <= 0.0 or reverse <= 0.0:
                continue
            ratio = (
                target[target_index] * reverse
                / (target[source] * forward)
            )
            accepted = forward * min(1.0, ratio)
            result[source][target_index] = accepted
            outgoing += accepted
        result[source][source] = max(0.0, 1.0 - outgoing)
    return result


def speed_kernel() -> list[list[float]]:
    proposal = [[0.0] * 9 for _ in range(9)]
    for source in range(9):
        for target in range(9):
            difference = abs(target - source)
            if difference <= 2:
                proposal[source][target] = math.exp(
                    -0.5 * difference**2
                )
        total = sum(proposal[source])
        proposal[source] = [value / total for value in proposal[source]]
    return reversible_kernel(SPEED_PROBABILITY, proposal)


def direction_kernel(target: list[float]) -> list[list[float]]:
    proposal = [[0.0] * 16 for _ in range(16)]
    for source in range(16):
        for offset in range(-3, 4):
            target_index = (source + offset) % 16
            angle = 22.5 * offset / 24.8
            proposal[source][target_index] += math.exp(
                -0.5 * angle**2
            )
        total = sum(proposal[source])
        proposal[source] = [value / total for value in proposal[source]]
    return reversible_kernel(target, proposal)


def reference_layout() -> list[tuple[float, float]]:
    return [
        (5.0 * DIAMETER * column, 5.0 * DIAMETER * row)
        for row in range(5)
        for column in range(5)
    ]


def direction_speed_powers(
    layout: list[tuple[float, float]],
    directions: list[float],
    speeds: list[float],
) -> list[float]:
    result: list[float] = []
    for direction in directions:
        angle = math.radians(direction)
        cosine, sine = math.cos(angle), math.sin(angle)
        along = [
            cosine * x + sine * y for x, y in layout
        ]
        across = [
            -sine * x + cosine * y for x, y in layout
        ]
        order = sorted(range(len(layout)), key=lambda item: (along[item], item))
        for reference_speed in speeds:
            inflow = [0.0] * len(layout)
            farm_power = 0.0
            for downstream_position, downstream in enumerate(order):
                rotor_sum = 0.0
                for rotor_cross, rotor_vertical in ROTOR_SAMPLES:
                    sample_cross = (
                        across[downstream]
                        + 0.5 * DIAMETER * rotor_cross
                    )
                    sample_height = (
                        HUB_HEIGHT + 0.5 * DIAMETER * rotor_vertical
                    )
                    free_speed = reference_speed * math.log(
                        (sample_height + ROUGHNESS) / ROUGHNESS
                    ) / math.log((HUB_HEIGHT + ROUGHNESS) / ROUGHNESS)
                    deficit_speed = 0.0
                    for upstream_position in range(downstream_position):
                        upstream = order[upstream_position]
                        distance = along[downstream] - along[upstream]
                        ct = thrust(inflow[upstream])
                        if distance <= 0.0 or ct <= 0.0:
                            continue
                        root = math.sqrt(max(1.0e-12, 1.0 - ct))
                        beta = (1.0 + root) / (2.0 * root)
                        width = (
                            WAKE_GROWTH * distance / DIAMETER
                            + 0.2 * math.sqrt(beta)
                        )
                        radical = max(
                            0.0,
                            min(1.0, 1.0 - ct / (8.0 * width**2)),
                        )
                        cross = (
                            sample_cross - across[upstream]
                        ) / DIAMETER
                        vertical = (
                            sample_height - HUB_HEIGHT
                        ) / DIAMETER
                        fraction = (
                            1.0 - math.sqrt(radical)
                        ) * math.exp(
                            -(cross**2 + vertical**2) / (2.0 * width**2)
                        )
                        deficit_speed += inflow[upstream] * fraction
                    rotor_sum += max(0.0, free_speed - deficit_speed)
                inflow[downstream] = rotor_sum / len(ROTOR_SAMPLES)
                farm_power += power(inflow[downstream])
            result.append(farm_power)
    return result


def oracle(case_name: str) -> dict[str, float]:
    real, threshold_fraction = CASES[case_name]
    direction_probability = (
        REAL_DIRECTION_PROBABILITY if real else [1.0 / 16.0] * 16
    )
    powers = direction_speed_powers(
        reference_layout(),
        [22.5 * index for index in range(16)],
        SPEEDS,
    )
    stationary = [
        direction_probability[direction] * SPEED_PROBABILITY[speed]
        for direction in range(16)
        for speed in range(9)
    ]
    d_kernel = direction_kernel(direction_probability)
    s_kernel = speed_kernel()
    mean_power = sum(
        stationary[state] * powers[state] for state in range(144)
    )
    threshold = threshold_fraction * 75.0
    intermittency = 0.0
    for from_direction in range(16):
        for from_speed in range(9):
            source = from_direction * 9 + from_speed
            for to_direction in range(16):
                for to_speed in range(9):
                    target = to_direction * 9 + to_speed
                    joint = (
                        stationary[source]
                        * d_kernel[from_direction][to_direction]
                        * s_kernel[from_speed][to_speed]
                    )
                    intermittency += joint * max(
                        0.0, abs(powers[source] - powers[target]) - threshold
                    )
    return {
        "mean_power_mw": mean_power,
        "intermittency_mw": intermittency,
    }


def model_oracle(y_over_d: float, direction: float) -> float:
    layout = [
        (0.0, y_over_d * DIAMETER),
        (5.0 * DIAMETER, 5.0 * DIAMETER),
        (10.0 * DIAMETER, 5.0 * DIAMETER),
    ]
    return direction_speed_powers(layout, [direction], [9.0])[0]


def close(left: float, right: float, tolerance: float = 2.0e-12) -> bool:
    return abs(left - right) <= tolerance * max(1.0, abs(left), abs(right))


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--emit-json", type=Path)
    args = parser.parse_args()
    case_receipts = {}
    for case_name in CASES:
        production = invoke(
            args.binary, "--mode", "evaluate", "--case", case_name
        )
        independent = oracle(case_name)
        evaluation = production["evaluation"]
        assert close(
            evaluation["mean_power_mw"],
            independent["mean_power_mw"],
        ), (case_name, evaluation, independent)
        assert close(
            evaluation["intermittency_mw"],
            independent["intermittency_mw"],
        ), (case_name, evaluation, independent)
        paper_power, paper_intermittency = PAPER_REFERENCE[case_name]
        assert (
            abs(evaluation["mean_power_mw"] - paper_power) / paper_power
            < 0.20
        ), (case_name, evaluation, PAPER_REFERENCE[case_name])
        assert (
            abs(
                evaluation["intermittency_mw"] - paper_intermittency
            ) / paper_intermittency < 0.25
        ), (case_name, evaluation, PAPER_REFERENCE[case_name])
        case_receipts[case_name] = {
            "production": evaluation,
            "independent": independent,
            "paper_reference_scale_anchor": {
                "mean_power_mw": paper_power,
                "intermittency_mw": paper_intermittency,
            },
        }

    model_receipts = {}
    for (y_over_d, direction), paper_power in PAPER_MODEL_9MPS.items():
        production = invoke(
            args.binary,
            "--mode", "model",
            "--case", "uniform_p0",
            "--model-y-over-d", str(y_over_d),
            "--model-speed-mps", "9",
            "--model-direction-deg", str(direction),
        )["power_mw"]
        independent = model_oracle(y_over_d, direction)
        assert close(production, independent)
        assert abs(production - paper_power) / paper_power < 0.06
        key = f"y{y_over_d:g}_direction{direction:g}"
        model_receipts[key] = {
            "production_power_mw": production,
            "independent_power_mw": independent,
            "paper_table_1_power_mw": paper_power,
        }

    common = [
        "--mode", "optimize",
        "--case", "uniform_p0",
        "--population", "40",
        "--generations", "2",
        "--seed", "24123383",
    ]
    serial = invoke(args.binary, *common, "--workers", "1")
    parallel = invoke(args.binary, *common, "--workers", "20")
    assert serial["scientific_hash"] == parallel["scientific_hash"]
    assert serial["physical_fes"] == parallel["physical_fes"] == 120
    assert parallel["observed_workers"] == 20
    receipt = {
        "schema_version": 1,
        "corpus_id": "T24",
        "status": "pass",
        "validator": "scripts/validate_core99_t24.py",
        "validator_sha256": hashlib.sha256(
            Path(__file__).read_bytes()
        ).hexdigest(),
        "independence":
            "Python paper-equation and declared-wind re-derivation without "
            "production-code import or linking",
        "cases": case_receipts,
        "model_problem": model_receipts,
        "parallel_equivalence": {
            "population": 40,
            "generations": 2,
            "physical_fes": 120,
            "one_and_twenty_worker_hash": serial["scientific_hash"],
            "observed_parallel_workers": parallel["observed_workers"],
        },
        "claim_boundary":
            "H5 equation, paper-scale, and bounded-protocol equivalence "
            "only; Waffle H6 and formal 25-seed quality remain queued",
    }
    if args.emit_json:
        args.emit_json.parent.mkdir(parents=True, exist_ok=True)
        args.emit_json.write_text(
            json.dumps(receipt, indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
        )
    print(
        "t24_h5_pass cases=6 model_points=6 "
        f"hash={serial['scientific_hash']}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
