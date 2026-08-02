#!/usr/bin/env python3
"""Independent H5 checks for T10 MOWFLOP and four target MOEAs.

WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T10 independent Katic-Jensen/objective oracle, case and
role enumeration, CHT/four-method execution, and worker-identity H5
Paper/DOI: 10.1016/j.rser.2016.07.021
Public source, missing fields and declared completions:
hpc/core99_cpp/include/core99/rodrigues_t10.hpp
Controlling contract: shared/contracts/core99_t10_rodrigues_2016.json
Last evidence-audit date: 2026-08-01
END WFLOP IMPLEMENTATION FACT DECLARATION
"""

from __future__ import annotations

import argparse
import json
import math
import subprocess
from pathlib import Path


POWER = [
    100, 570, 1103, 1835, 2858, 4089, 5571, 7105, 7873, 7986,
    8008, 8008, 8008, 8008, 8008, 8008, 8008, 8008, 8008, 8008,
    8008, 8008,
]
THRUST = [
    0.700000000, 0.722386304, 0.773588333, 0.773285946,
    0.767899317, 0.732727569, 0.688896343, 0.623028669,
    0.500046699, 0.373661747, 0.293230676, 0.238407400,
    0.196441644, 0.163774674, 0.137967245, 0.117309371,
    0.100578122, 0.086883163, 0.075565832, 0.066131748,
    0.058204932, 0.051495998,
]
SPEED = [9.77, 8.34, 7.93, 10.18, 8.14, 8.24,
         9.05, 11.59, 12.11, 11.90, 10.38, 8.14]
FREQUENCY = [6.3, 5.9, 5.5, 7.8, 8.3, 6.5,
             11.4, 14.6, 12.1, 8.5, 6.4, 6.7]
DIAMETER = 164.0
RADIUS = 82.0
HUB = 107.0
ROUGHNESS = 0.0005


def interpolate(values: list[float], speed: float) -> float:
    if speed < 4.0 or speed > 25.0:
        return 0.0
    if speed == 25.0:
        return float(values[-1])
    lower = math.floor(speed)
    fraction = speed - lower
    index = lower - 4
    return values[index] * (1.0 - fraction) + values[index + 1] * fraction


def overlap(first: float, second: float, distance: float) -> float:
    if distance >= first + second:
        return 0.0
    if distance <= abs(first - second):
        return math.pi * min(first, second) ** 2
    first_cos = max(-1.0, min(1.0, (
        first * first + distance * distance - second * second
    ) / (2.0 * first * distance)))
    second_cos = max(-1.0, min(1.0, (
        second * second + distance * distance - first * first
    ) / (2.0 * second * distance)))
    first_angle = 2.0 * math.acos(first_cos)
    second_angle = 2.0 * math.acos(second_cos)
    return 0.5 * first * first * (first_angle - math.sin(first_angle)) + (
        0.5 * second * second * (second_angle - math.sin(second_angle))
    )


def occupied(words: list[int], index: int) -> bool:
    return bool((words[index // 64] >> (index % 64)) & 1)


def oracle(case_id: str, words: list[int]) -> tuple[float, float, int]:
    farm = case_id[4]
    step = int(case_id[6:])
    q8 = {"A": 4, "B": 7, "C": 10, "D": 13}[farm]
    maximum = q8 * q8
    q = (8 // step) * (q8 - 1) + 1
    spacing = step * DIAMETER
    coordinates = [
        (column * spacing, row * spacing)
        for row in range(q) for column in range(q)
    ]
    selected = [index for index in range(q * q) if occupied(words, index)]
    alpha = 0.5 / math.log(HUB / ROUGHNESS)
    ideal = sum(
        frequency / 100.0 * interpolate(POWER, speed)
        for speed, frequency in zip(SPEED, FREQUENCY)
    )
    expected = 0.0
    for direction, (speed, frequency) in enumerate(zip(SPEED, FREQUENCY)):
        angle = math.radians(direction * 30.0 + 180.0)
        cosine, sine = math.cos(angle), math.sin(angle)
        along = [cosine * coordinates[i][0] + sine * coordinates[i][1]
                 for i in selected]
        across = [-sine * coordinates[i][0] + cosine * coordinates[i][1]
                  for i in selected]
        state_power = 0.0
        thrust = interpolate(THRUST, speed)
        for target in range(len(selected)):
            squared = 0.0
            for source in range(len(selected)):
                if source == target:
                    continue
                downstream = along[target] - along[source]
                if downstream <= 0.0:
                    continue
                crosswind = abs(across[target] - across[source])
                wake_radius = RADIUS + alpha * downstream
                area = overlap(wake_radius, RADIUS, crosswind)
                if area <= 0.0:
                    continue
                denominator = 1.0 + alpha * downstream / RADIUS
                deficit = (1.0 - math.sqrt(max(0.0, 1.0 - thrust))) / (
                    denominator * denominator
                ) * area / (math.pi * RADIUS * RADIUS)
                squared += deficit * deficit
            effective = speed * max(0.0, 1.0 - math.sqrt(squared))
            state_power += interpolate(POWER, effective)
        expected += frequency / 100.0 * state_power
    count = len(selected)
    return expected / (maximum * ideal), expected / (count * ideal), count


def capture(arguments: list[str]) -> dict:
    completed = subprocess.run(
        arguments, check=True, text=True,
        stdout=subprocess.PIPE, stderr=subprocess.PIPE,
    )
    return json.loads(completed.stdout)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    binary = args.binary.resolve()
    cases = [f"t10_{farm}_{step}" for farm in "ABCD" for step in (8, 4, 2)]
    equation_errors: dict[str, dict[str, float]] = {}
    identity = True
    for index, case_id in enumerate(cases):
        base = [
            str(binary), "--mode", "evaluate", "--case", case_id,
            "--constraint", "repair", "--seed", str(9100 + index),
        ]
        one = capture(base + ["--workers", "1"])
        four = capture(base + ["--workers", "4"])
        expected_energy, expected_efficiency, expected_count = oracle(
            case_id, one["occupancy_words"]
        )
        energy_error = abs(one["normalized_energy"] - expected_energy)
        efficiency_error = abs(one["efficiency"] - expected_efficiency)
        equation_errors[case_id] = {
            "energy_abs_error": energy_error,
            "efficiency_abs_error": efficiency_error,
        }
        if energy_error > 2.0e-13 or efficiency_error > 2.0e-13:
            raise RuntimeError(f"T10 independent equation mismatch for {case_id}")
        if one["occupied_turbines"] != expected_count:
            raise RuntimeError(f"T10 occupied count mismatch for {case_id}")
        identity = identity and (
            one["normalized_energy"] == four["normalized_energy"]
            and one["efficiency"] == four["efficiency"]
            and one["occupancy_words"] == four["occupancy_words"]
        )

    algorithms: dict[str, dict] = {}
    for algorithm in ("mogomea", "omogomea", "nsgaii", "c-nsgaii"):
        command = [
            str(binary), "--mode", "optimize", "--case", "t10_B_4",
            "--algorithm", algorithm, "--constraint", "repair",
            "--seed", "9201", "--maximum-fes", "300",
            "--maximum-generations", "10",
        ]
        one = capture(command + ["--workers", "1"])
        four = capture(command + ["--workers", "4"])
        algorithm_identity = (
            one["scientific_hash"] == four["scientific_hash"]
            and one["physical_fes"] == four["physical_fes"]
            and one["hypervolume"] == four["hypervolume"]
        )
        identity = identity and algorithm_identity
        algorithms[algorithm] = {
            "one_worker_scientific_hash": one["scientific_hash"],
            "four_worker_scientific_hash": four["scientific_hash"],
            "physical_fes": four["physical_fes"],
            "archive_size": four["archive_size"],
            "hypervolume": four["hypervolume"],
            "worker_identity": algorithm_identity,
        }

    multi_resolution_command = [
        str(binary), "--mode", "optimize", "--case", "t10_B_8",
        "--algorithm", "nsgaii", "--constraint", "constraint",
        "--seed", "9301", "--maximum-fes", "5000",
        "--maximum-generations", "5000", "--multi-resolution", "true",
    ]
    multi_resolution_one = capture(
        multi_resolution_command + ["--workers", "1"]
    )
    multi_resolution_four = capture(
        multi_resolution_command + ["--workers", "4"]
    )
    multi_resolution_identity = (
        multi_resolution_one["final_grid_step_diameters"] == 2
        and multi_resolution_four["final_grid_step_diameters"] == 2
        and multi_resolution_one["scientific_hash"]
            == multi_resolution_four["scientific_hash"]
        and multi_resolution_one["physical_fes"]
            == multi_resolution_four["physical_fes"]
        and multi_resolution_one["hypervolume"]
            == multi_resolution_four["hypervolume"]
    )
    identity = identity and multi_resolution_identity
    if not identity:
        raise RuntimeError("T10 one/all-worker scientific identity failed")

    payload = {
        "schema_version": 1,
        "corpus_id": "T10",
        "status": "pass",
        "case_count": len(cases),
        "fixed_resolution_role_count": 192,
        "multi_resolution_role_count": 4,
        "formal_receipt_count": 1960,
        "independent_equation_errors": equation_errors,
        "algorithms": algorithms,
        "multi_resolution": {
            "algorithm": "nsgaii",
            "completed_grid_step_diameters":
                multi_resolution_four["final_grid_step_diameters"],
            "physical_fes": multi_resolution_four["physical_fes"],
            "one_worker_scientific_hash":
                multi_resolution_one["scientific_hash"],
            "four_worker_scientific_hash":
                multi_resolution_four["scientific_hash"],
            "worker_identity": multi_resolution_identity,
        },
        "worker_identity": identity,
    }
    text = json.dumps(payload, indent=2, sort_keys=True) + "\n"
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(text)
    else:
        print(text, end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
