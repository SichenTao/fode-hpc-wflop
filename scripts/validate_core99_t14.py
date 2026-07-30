#!/usr/bin/env python3
"""
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: independent T14 paper-equation H5 oracle
Paper DOI: 10.5194/wes-4-663-2019
Public source: https://github.com/byuflowlab/stanley2019-variable-reduction
revision 62b590065f9541c4296338b3f1a0ee07cfcd28bc
Provided/missing/conflicting facts and Reconstruction decisions:
hpc/core99_cpp/include/core99/stanley_t14.hpp
Method/problem semantic IDs: t14_boundary_grid_parameterization_v1;
t14_stanley_2019_seven_unique_cases_v1
Controlling contract: shared/contracts/core99_t14_stanley_2019.json
Independence boundary: Python directly evaluates the emitted layout from
paper equations and public wind-resource inputs; no production C++ function
is imported
Claim boundary: evaluator/decoder semantic oracle, not SNOPT replay
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
"""

from __future__ import annotations

import argparse
import json
import math
import subprocess
from pathlib import Path
from typing import Any

import numpy as np

from generate_core99_t14_data import load_public_module


ROTOR_DIAMETER = 130.0
HUB_HEIGHT = 110.0

CT_SPEEDS = np.array([
    0.000001, 0.1, 0.60816327, 1.11632653, 1.6244898, 2.13265306,
    2.64081633, 3.14897959, 3.65714286, 4.16530612, 4.67346939,
    5.18163265, 5.68979592, 6.19795918, 6.70612245, 7.21428571,
    7.72244898, 8.23061224, 8.73877551, 9.24693878, 9.75510204,
    10.26326531, 10.77142857, 11.27959184, 11.7877551, 12.29591837,
    12.80408163, 13.3122449, 13.82040816, 14.32857143, 14.83673469,
    15.34489796, 15.85306122, 16.36122449, 16.86938776, 17.37755102,
    17.88571429, 18.39387755, 18.90204082, 19.41020408, 19.91836735,
    20.42653061, 20.93469388, 21.44285714, 21.95102041, 22.45918367,
    22.96734694, 23.4755102, 23.98367347, 24.49183673, 25.0,
])
CT_VALUES = np.array([
    0.74988552, 0.74988552, 0.74988552, 0.74988552, 0.74988552,
    0.74988552, 0.74988552, 0.74945275, 0.74736838, 0.74578062,
    0.74452166, 0.7432327, 0.74240891, 0.74171844, 0.74113119,
    0.74062551, 0.7401854, 0.7397988, 0.73945643, 0.73915104,
    0.71535516, 0.50902345, 0.42264255, 0.36002829, 0.31616439,
    0.27728908, 0.2449473, 0.2179915, 0.19464155, 0.17388996,
    0.15676952, 0.14116089, 0.12769325, 0.11564223, 0.104593,
    0.09546578, 0.08765315, 0.08043937, 0.07409357, 0.06822311,
    0.06322334, 0.05887784, 0.05481244, 0.05114998, 0.0474271,
    0.04415572, 0.04104199, 0.0383636, 0.03582949, 0.03401271,
    0.03235028,
])
ROTOR = np.array([[0.0, 0.69], [0.0, -0.69], [-0.69, 0.0], [0.69, 0.0]])


def cdf(speed: float, mean: float) -> float:
    if speed <= 0.0 or mean <= 1.0e-12:
        return 0.0
    scale = mean / math.gamma(1.5)
    return 1.0 - math.exp(-((speed / scale) ** 2))


def ct(speed: float) -> float:
    return float(np.interp(speed, CT_SPEEDS, CT_VALUES))


def power(speed: float) -> float:
    if speed < 3.0 or speed >= 25.0:
        return 0.0
    if speed < 10.0:
        return 3.6 * (speed / 10.0) ** 3
    return 3.6


def state_power(layout: np.ndarray, direction: float, speed: float) -> float:
    frame = math.radians(270.0 - direction)
    along = math.cos(frame) * layout[:, 0] + math.sin(frame) * layout[:, 1]
    across = -math.sin(frame) * layout[:, 0] + math.cos(frame) * layout[:, 1]
    order = np.argsort(along, kind="stable")
    velocities = np.zeros(100)
    thrust = np.zeros(100)
    free_stream = speed * (HUB_HEIGHT / 50.0) ** 0.1
    expansion = 0.3837 * 0.11 + 0.003678
    for raw_downstream, downstream in enumerate(order):
        rotor_velocity = 0.0
        for sample_y, sample_z in ROTOR:
            local_y = 0.5 * ROTOR_DIAMETER * sample_y
            local_z = 0.5 * ROTOR_DIAMETER * sample_z
            deficit_speed = 0.0
            for upstream in order[:raw_downstream]:
                distance = along[downstream] - along[upstream]
                if distance <= 0.1:
                    continue
                local_ct = min(
                    0.95,
                    max(1.0e-6, thrust[upstream] if thrust[upstream] > 0.0 else ct(free_stream)),
                )
                x0 = ROTOR_DIAMETER * (1.0 + math.sqrt(1.0 - local_ct)) / (
                    math.sqrt(2.0)
                    * (2.32 * 0.11 + 0.154 * (1.0 - math.sqrt(1.0 - local_ct)))
                )
                a = 2.0 * expansion
                b = 4.0 * expansion**2 * (local_ct - 1.0)
                c = 2.0 * math.sqrt(8.0) * expansion**2
                discontinuity = x0 + ROTOR_DIAMETER * (
                    a - math.sqrt(max(0.0, a * a - b))
                ) / c
                model_x = max(distance, discontinuity)
                sigma = ROTOR_DIAMETER * (
                    expansion * (model_x - x0) / ROTOR_DIAMETER
                    + 1.0 / math.sqrt(8.0)
                )
                argument = min(
                    1.0,
                    max(0.0, local_ct * ROTOR_DIAMETER**2 / (8.0 * sigma**2)),
                )
                magnitude = 1.0 - math.sqrt(1.0 - argument)
                dy = across[downstream] + local_y - across[upstream]
                deficit = magnitude * math.exp(
                    -0.5 * (dy**2 + local_z**2) / sigma**2
                )
                deficit_speed += velocities[upstream] * deficit
            point_velocity = max(0.0, free_stream - deficit_speed) * (
                (HUB_HEIGHT + local_z) / HUB_HEIGHT
            ) ** 0.1
            rotor_velocity += point_velocity
        velocities[downstream] = rotor_velocity / 4.0
        thrust[downstream] = ct(velocities[downstream])
    return sum(power(float(value)) for value in velocities)


def oracle_aep(layout: list[list[float]], source_root: Path) -> float:
    module = load_public_module(source_root / "windRoses.py")
    directions, frequencies, means = module["northIslandRose"](24, nSpeeds=1)
    layout_array = np.asarray(layout)
    weighted = 0.0
    width = 5.0
    for direction, frequency, mean in zip(directions, frequencies, means):
        for bin_index in range(5):
            lower = width * bin_index
            upper = lower + width
            speed = 0.5 * (lower + upper) + (0.001 if bin_index == 0 else 0.0)
            probability = frequency * (cdf(upper, mean) - cdf(lower, mean))
            weighted += probability * state_power(
                layout_array,
                float(direction),
                speed,
            )
    return 8760.0 * weighted * 0.93 / 1000.0


def invoke(
    binary: Path,
    case_id: str,
    algorithm: str,
) -> dict[str, Any]:
    return json.loads(subprocess.check_output(
        [
            str(binary),
            "--case",
            case_id,
            "--algorithm",
            algorithm,
            "--seed",
            "20260731",
            "--evaluate-reference",
        ],
        text=True,
    ))


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--contract", type=Path, required=True)
    parser.add_argument("--source-root", type=Path, required=True)
    arguments = parser.parse_args()
    contract = json.loads(arguments.contract.read_text(encoding="utf-8"))
    algorithms = [entry["id"] for entry in contract["algorithms"]]
    case_ids = [
        "t14_spacing4_amalia_north_island",
        "t14_spacing6_amalia_north_island",
        "t14_spacing8_amalia_north_island",
        "t14_spacing4_amalia_ukiah",
        "t14_spacing4_amalia_victorville",
        "t14_spacing4_circle_north_island",
        "t14_spacing4_square_north_island",
    ]
    receipts: dict[str, Any] = {}
    for case_id in case_ids:
        for algorithm in algorithms:
            first = invoke(arguments.binary, case_id, algorithm)
            second = invoke(arguments.binary, case_id, algorithm)
            if first != second or len(first["layout"]) != 100:
                raise SystemExit("T14 deterministic structural check failed")
            receipts[f"{case_id}:{algorithm}"] = {
                "aep_gwh": first["optimization_aep_gwh"],
                "violation_m": first["constraint_violation_m"],
            }
    reference = invoke(
        arguments.binary,
        "t14_spacing4_amalia_north_island",
        "t14_boundary_grid",
    )
    oracle = oracle_aep(reference["layout"], arguments.source_root)
    absolute_error = abs(oracle - reference["optimization_aep_gwh"])
    tolerance = 1.0e-8
    report = {
        "status": "pass" if absolute_error <= tolerance else "fail",
        "method_semantic_id": contract["method_semantic_id"],
        "problem_semantic_id": contract["problem_semantic_id"],
        "paper_matrix_receipts": receipts,
        "independent_oracle": {
            "cpp_optimization_aep_gwh": reference["optimization_aep_gwh"],
            "python_optimization_aep_gwh": oracle,
            "absolute_error_gwh": absolute_error,
            "tolerance_gwh": tolerance,
        },
    }
    print(json.dumps(report, indent=2, sort_keys=True))
    if report["status"] != "pass":
        raise SystemExit(1)


if __name__ == "__main__":
    main()
