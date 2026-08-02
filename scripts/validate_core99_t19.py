#!/usr/bin/env python3
"""Independent H5 validator for T19 Dhoot et al. 2021.

Paper DOI: 10.1016/j.energy.2021.120035. The target uses official SRMP
v1.01 TRW-S but does not publish beta, its modified GEMPLP triplet generator,
numeric WR-36 array or wrapper. The production code declares deterministic
completions. This validator independently reconstructs Eq. (7) and the
posterior Jensen/RSS power for a returned historical layout, checks one/all
worker identity, paper-scale power, exact-K/spacing repair, and the 2,500-cell
no-triplet path. It does not claim author numerical replay.
"""

from __future__ import annotations

import argparse
import json
import math
from pathlib import Path
import subprocess


def run(binary: Path, *arguments: str) -> dict:
    process = subprocess.run(
        [str(binary), *arguments], check=True, text=True,
        stdout=subprocess.PIPE, stderr=subprocess.PIPE,
    )
    return json.loads(process.stdout)


def historical_oracle(layout: list[int]) -> tuple[float, float]:
    radius = 20.0
    decay = 0.1
    ct = 0.88
    induction = 0.5 * (1.0 - math.sqrt(1.0 - ct))
    speed = 12.0
    cells = [((i % 10 + 0.5) * 200.0, (i // 10 + 0.5) * 200.0)
             for i in range(100)]

    def deficit(source: int, target: int) -> float:
        # from=0 degrees gives flow=(0,-1), matching the C++/platform
        # meteorological convention used throughout this benchmark.
        dx = cells[target][0] - cells[source][0]
        dy = cells[target][1] - cells[source][1]
        downstream = -dy
        if downstream <= 1.0e-12 or abs(dx) > radius + decay * downstream:
            return 0.0
        return 2.0 * induction / (1.0 + decay * downstream / radius) ** 2

    expected_power = 0.0
    for target in layout:
        sum_squares = sum(deficit(source, target) ** 2 for source in layout)
        effective = speed * max(0.0, 1.0 - math.sqrt(sum_squares))
        expected_power += 0.3 * effective ** 3
    qip = 0.0
    for left, source in enumerate(layout):
        for target in layout[left + 1:]:
            qip += speed * (
                deficit(source, target) ** 2 + deficit(target, source) ** 2
            )
    return expected_power, qip


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", type=Path, required=True)
    args = parser.parse_args()
    binary = args.binary.resolve()

    described = run(binary, "--action", "describe")
    assert described["paper_role_count"] == 112
    assert described["deterministic_role_count"] == 112
    assert described["cell_counts"] == [100, 400, 2500]

    common = (
        "--action", "solve", "--family", "historical", "--wind", "wr1",
        "--cells", "100", "--turbines", "26", "--iterations", "100",
        "--time-limit", "60", "--triplets", "200",
    )
    serial = run(binary, *common, "--workers", "1")
    parallel = run(binary, *common, "--workers", "4")
    assert serial["layout"] == parallel["layout"]
    assert serial["scientific_hash"] == parallel["scientific_hash"]
    assert serial["aep_gwh"] == parallel["aep_gwh"]
    assert serial["qip_wake_objective"] == parallel["qip_wake_objective"]
    assert parallel["observed_workers"] >= 2
    assert serial["exact_cardinality"] and serial["spacing_feasible"]
    assert serial["physical_fes"] == 1
    expected_power, qip = historical_oracle(serial["layout"])
    assert abs(serial["expected_power_kw"] - expected_power) < 1.0e-9
    assert abs(serial["qip_wake_objective"] - qip) < 1.0e-12
    # Paper Table 2 reports 12,486 kW for MP. Different declared beta,
    # clusters and order are expected, but the physical result must retain
    # the source benchmark scale.
    assert 11_500.0 < serial["expected_power_kw"] < 14_000.0

    fine = run(
        binary, "--action", "solve", "--family", "realistic",
        "--wind", "wr1", "--cells", "2500", "--turbines", "10",
        "--workers", "4", "--iterations", "1", "--time-limit", "60",
        "--triplets", "0", "--one-swap", "off",
    )
    assert fine["requested_triplets"] == 0
    assert fine["generated_triplets"] == 0
    assert fine["repaired_cardinality"] == 10
    assert fine["exact_cardinality"] and fine["spacing_feasible"]
    assert fine["minimum_spacing_m"] >= 315.0 - 1.0e-9
    assert fine["observed_workers"] >= 2
    print("T19 independent H5 validation passed")


if __name__ == "__main__":
    main()
