#!/usr/bin/env python3
"""Independent H5 validator for T25 Rodrigues 2024.

Paper DOI: 10.5194/wes-9-321-2024.
Public data: 10.5281/zenodo.10402450, GBWFLO_dataset.zip MD5
da3192a1b7467ba038c611ec656536b8. PyWake v2.5.0 tag
cd5ff8363ae2615a92860d409e748b4a0431f33d provides the source equations.

The validator independently evaluates the IEA-37 reference layout, checks
one/all-core numerical identity, exercises the complete 360x23 Horns Rev
flow grid, validates incremental SMAST feasibility and compares its initial
AEP with the public NetCDF population-scale anchor. It never claims exact
author optimizer trajectories or timing replay.
"""

from __future__ import annotations

import argparse
import json
import math
import subprocess
from pathlib import Path


FREQUENCY = [
    0.025, 0.024, 0.029, 0.036, 0.063, 0.065, 0.100, 0.122,
    0.063, 0.038, 0.039, 0.083, 0.213, 0.046, 0.032, 0.022,
]


def run(binary: Path, *arguments: str) -> dict:
    process = subprocess.run(
        [str(binary), *arguments],
        check=True,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    return json.loads(process.stdout)


def circular_layout(counts: list[int], radius: float) -> list[tuple[float, float]]:
    result: list[tuple[float, float]] = []
    outer = counts[-1]
    for count in counts:
        if count == 1:
            result.append((0.0, 0.0))
            continue
        orbit_radius = radius * count / outer
        for index in range(count):
            angle = 2.0 * math.pi * index / count
            result.append((orbit_radius * math.cos(angle), orbit_radius * math.sin(angle)))
    return result


def iea_power(speed: float) -> float:
    if speed <= 4.0 or speed > 25.0:
        return 0.0
    if speed >= 9.8:
        return 3.35
    return 3.35 * ((speed - 4.0) / 5.8) ** 3


def independent_iea_aep() -> float:
    # Public iea37-ex16.yaml coordinates, including the source decimals.
    layout = [
        (0.0, 0.0), (650.0, 0.0), (200.861, 618.1867),
        (-525.861, 382.0604), (-525.861, -382.0604),
        (200.861, -618.1867), (1300.0, 0.0),
        (1051.7221, 764.1208), (401.7221, 1236.3735),
        (-401.7221, 1236.3735), (-1051.7221, 764.1208),
        (-1300.0, 0.0), (-1051.7221, -764.1208),
        (-401.7221, -1236.3735), (401.7221, -1236.3735),
        (1051.7221, -764.1208),
    ]
    total = 0.0
    diameter = 130.0
    ct = 8.0 / 9.0
    for direction in range(360):
        angle = math.radians(direction)
        sine, cosine = math.sin(angle), math.cos(angle)
        sector = int(math.floor((direction + 11.25) / 22.5)) % 16
        probability = FREQUENCY[sector] / 22.5
        # PyWake StraightDistance meteorological direction convention.
        down = [-x * sine - y * cosine for x, y in layout]
        cross = [x * cosine - y * sine for x, y in layout]
        for target in range(len(layout)):
            sum_squares = 0.0
            for source in range(len(layout)):
                if source == target:
                    continue
                distance = down[target] - down[source]
                if distance <= 1.0e-10:
                    continue
                offset = cross[target] - cross[source]
                sigma = 0.0324555 * distance + diameter / math.sqrt(8.0)
                centre = 1.0 - math.sqrt(
                    max(1.0e-14, 1.0 - ct * diameter * diameter / (8.0 * sigma * sigma))
                )
                deficit = 9.8 * centre * math.exp(-0.5 * offset * offset / (sigma * sigma))
                sum_squares += deficit * deficit
            total += iea_power(max(0.0, 9.8 - math.sqrt(sum_squares))) * 8760.0 * probability / 1000.0
    return total


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", type=Path, required=True)
    args = parser.parse_args()
    binary = args.binary.resolve()

    described = run(binary, "--action", "describe")
    assert described["paper_role_count"] == 65
    assert described["platform_executable_role_count"] == 55
    assert described["observation_only_role_count"] == 10
    assert described["problem_families"] == ["iea37", "horns_rev"]

    serial = run(
        binary, "--action", "evaluate", "--family", "iea37",
        "--turbines", "16", "--directions", "360", "--workers", "1",
        "--gradient", "exact_reverse",
    )
    parallel = run(
        binary, "--action", "evaluate", "--family", "iea37",
        "--turbines", "16", "--directions", "360", "--workers", "4",
        "--gradient", "exact_reverse",
    )
    expected = independent_iea_aep()
    assert abs(serial["aep_gwh"] - expected) < 1.0e-10
    assert serial["aep_gwh"] == parallel["aep_gwh"]
    assert serial["maximum_abs_gradient_gwh_per_m"] == parallel["maximum_abs_gradient_gwh_per_m"]
    assert parallel["observed_workers"] >= 2

    # Direct oracles generated with the pinned PyWake v2.5.0 source and its
    # public IEA-37 YAML layouts. The small tolerance covers the source
    # package's array/reduction path, not a change in physical semantics.
    source_oracles = {
        16: 373.20664505414635,
        36: 758.6700436686776,
        64: 1343.7933898976326,
    }
    for count, oracle in source_oracles.items():
        value = run(
            binary, "--action", "evaluate", "--family", "iea37",
            "--turbines", str(count), "--directions", "360",
            "--workers", "4", "--gradient", "none",
        )
        assert abs(value["aep_gwh"] - oracle) < 1.0e-5

    smart = run(
        binary, "--action", "smart-start", "--family", "iea37",
        "--turbines", "16", "--directions", "360", "--workers", "4",
        "--random-percent", "0", "--grid-r", "3", "--seed", "25001",
    )
    assert smart["minimum_spacing_m"] >= 260.0 - 1.0e-9
    assert smart["observed_workers"] >= 2
    # Public Storyline3.nc anchor: mean initial AEP for n=16, randompct=0,
    # grid=3R is 371.83077383259945 GWh over 1000 seeds. A distinct stable RNG
    # is expected to differ, but must retain the published physical magnitude.
    assert abs(smart["aep_gwh"] - 371.83077383259945) < 8.0

    horns = run(
        binary, "--action", "evaluate", "--family", "horns_rev",
        "--turbines", "100", "--directions", "360", "--speeds", "23",
        "--workers", "4", "--gradient", "exact_reverse",
    )
    assert horns["flow_cases"] == 8280
    assert horns["pair_interactions"] == 40_986_000
    # Pinned PyWake v2.5.0 + public Hornsrev1_xl.py source oracle.
    assert abs(horns["aep_gwh"] - 849.7766695842039) < 1.0e-9
    assert horns["observed_workers"] >= 2

    optimized = run(
        binary, "--action", "optimize", "--family", "iea37",
        "--turbines", "16", "--directions", "36", "--workers", "4",
        "--random-percent", "0", "--grid-r", "3", "--seed", "25003",
        "--max-evaluations", "2",
    )
    assert optimized["physical_layout_evaluations"] >= 2
    assert optimized["maximum_boundary_violation_m"] <= 1.0e-3
    assert optimized["observed_workers"] >= 2
    print("T25 independent H5 validation passed")


if __name__ == "__main__":
    main()
