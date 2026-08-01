#!/usr/bin/env python3
"""Independent H5 checks for T08 exact-gradient interior-point reproduction.

WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T08 independent equation/oracle and worker-identity H5
Paper/DOI: 10.1016/j.apenergy.2016.06.101
Paper/source/missing/reconstruction declaration:
hpc/core99_cpp/include/core99/guirguis_t08.hpp
This validator independently evaluates Eqs. (3), (5)-(7), (11)-(13) from
binary-emitted layouts, checks exact one/all-worker science identity, exercises
the 100-turbine and both land families, and verifies a bounded optimization.
Controlling contract: shared/contracts/core99_t08_guirguis_2016.json
Last evidence-audit date: 2026-08-01
END WFLOP IMPLEMENTATION FACT DECLARATION
"""

from __future__ import annotations

import argparse
import json
import math
import subprocess
from pathlib import Path


def run(binary: Path, *arguments: str) -> dict:
    completed = subprocess.run(
        [str(binary), *arguments], check=True, text=True,
        stdout=subprocess.PIPE, stderr=subprocess.PIPE,
    )
    return json.loads(completed.stdout)


def case_three_wind() -> list[tuple[float, float, float]]:
    profile17 = [1] * 27 + [1.55, 1.7, 2.7, 3.2, 2.7, 1.7, 1.55, 1.2, 1]
    profile17[26] = 1.2
    profile12 = [1] * 27 + [1.45, 1.75, 1.7, 2.35, 1.7, 1.75, 1.45, 1.2, 1]
    profile12[26] = 1.2
    raw: list[tuple[float, float, float]] = []
    for direction in range(36):
        raw.extend([
            (10.0 * direction, 8.0, 0.005),
            (10.0 * direction, 12.0, 0.008 * profile12[direction]),
            (10.0 * direction, 17.0, 0.011 * profile17[direction]),
        ])
    total = sum(item[2] for item in raw)
    return [(direction, speed, probability / total)
            for direction, speed, probability in raw]


def horns_rev_wind() -> list[tuple[float, float, float]]:
    frequencies = [
        3.597152, 3.948682, 5.167395, 7.000154,
        8.364547, 6.434850, 8.643194, 11.770510,
        15.157570, 14.737920, 10.012050, 5.165975,
    ]
    total = sum(frequencies)
    return [
        ((30.0 * direction + 180.0) % 360.0, 10.0,
         frequency / total)
        for direction, frequency in enumerate(frequencies)
    ]


def paper_efficiency(
    layout: list[list[float]],
    winds: list[tuple[float, float, float]],
    rotor_diameter: float,
    hub_height: float,
    roughness: float,
) -> float:
    radius = 0.5 * rotor_diameter
    entrainment = 1.0 / (2.0 * math.log(hub_height / roughness))
    numerator = 0.0
    denominator = 0.0
    for direction, speed, probability in winds:
        angle = math.radians(direction)
        cosine, sine = math.cos(angle), math.sin(angle)
        downstream = [cosine * x + sine * y for x, y in layout]
        crosswind = [-sine * x + cosine * y for x, y in layout]
        for target in range(len(layout)):
            deficit_squared = 0.0
            for source in range(len(layout)):
                dx = downstream[target] - downstream[source]
                if source == target or dx <= 0.0:
                    continue
                dy = abs(crosswind[target] - crosswind[source])
                theta = math.atan2(dy, dx)
                if theta >= math.pi / 9.0:
                    continue
                modulation = 0.5 * (1.0 + math.cos(9.0 * theta))
                deficit = (2.0 / 3.0) * modulation * (
                    radius / (radius + entrainment * dx)
                ) ** 2
                deficit_squared += deficit * deficit
            effective = speed * max(0.0, 1.0 - math.sqrt(deficit_squared))
            numerator += probability * effective ** 3 / 3.0
            denominator += probability * speed ** 3 / 3.0
    return 100.0 * numerator / denominator


def evaluate(binary: Path, case_id: str, workers: int) -> dict:
    return run(
        binary, "--mode", "evaluate", "--case", case_id,
        "--start-policy", "lhs", "--seed", "801", "--workers", str(workers),
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", type=Path, required=True)
    args = parser.parse_args()

    serial = evaluate(args.binary, "t08_benchmark_c3_n10", 1)
    parallel = evaluate(args.binary, "t08_benchmark_c3_n10", 4)
    assert serial["efficiency_percent"] == parallel["efficiency_percent"]
    assert serial["maximum_abs_gradient_percent_per_m"] \
        == parallel["maximum_abs_gradient_percent_per_m"]
    oracle = paper_efficiency(serial["layout"], case_three_wind(), 40.0, 60.0, 0.3)
    assert abs(oracle - serial["efficiency_percent"]) < 2.0e-11

    large_serial = evaluate(args.binary, "t08_scaling_n100_d6", 1)
    large_parallel = evaluate(args.binary, "t08_scaling_n100_d6", 4)
    assert large_serial["efficiency_percent"] == large_parallel["efficiency_percent"]
    large_oracle = paper_efficiency(
        large_serial["layout"], horns_rev_wind(), 80.0, 70.0, 0.3
    )
    assert abs(large_oracle - large_serial["efficiency_percent"]) < 2.0e-11
    assert large_serial["maximum_constraint_violation"] < 0.0
    assert large_parallel["observed_workers"] >= 2

    for case_id in ("t08_land_copenhagen_n37", "t08_land_ring_n20"):
        receipt = evaluate(args.binary, case_id, 4)
        assert receipt["maximum_constraint_violation"] < 0.0
        assert receipt["efficiency_percent"] > 0.0

    common = (
        "--mode", "optimize", "--case", "t08_benchmark_c2_n10",
        "--start-policy", "usl", "--starts", "1", "--seed", "802",
        "--maximum-evaluations", "60", "--barrier-phases", "2",
    )
    optimized_one = run(args.binary, *common, "--workers", "1")
    optimized_all = run(args.binary, *common, "--workers", "4")
    assert optimized_one["scientific_hash"] == optimized_all["scientific_hash"]
    assert optimized_all["maximum_constraint_violation"] < 0.0
    assert optimized_all["minimum_spacing_m"] > 200.0
    assert optimized_all["best_efficiency_percent"] > 0.0

    print(json.dumps({
        "status": "pass",
        "independent_case3_abs_error": abs(oracle - serial["efficiency_percent"]),
        "evaluation_worker_identity": True,
        "optimization_worker_identity": True,
        "large_case_observed_workers": large_parallel["observed_workers"],
        "land_cases_checked": 2,
    }, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
