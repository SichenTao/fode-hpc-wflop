#!/usr/bin/env python3
"""
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: independent T11 equations, cases and lifecycle H5 oracle
Paper/DOI: BlockCopy-Based Operators for Evolving Efficient Wind Farm
Layouts; 10.1109/CEC.2016.7743909
Public source: https://github.com/d9w/WindFLO revision
9e85a67bb2ca019768ea51dd0b634a46c8406ba2 (MIT), including the original C++
Kusiak evaluator and 2014 competition XML files.
Missing/conflicting facts and completion policy:
hpc/core99_cpp/include/core99/blockcopy_t11.hpp
Independence: this oracle uses the source's original acos cone geometry,
direct Weibull quadrature and economic equation; it does not import or
translate production C++ functions.
Method/problem semantic IDs: t11_blockcopy_four_es_methods_v1;
t11_kusiak_and_2014_competition_four_cases_v1
Controlling contract: shared/contracts/core99_t11_blockcopy_2016.json
Claim boundary: equation/case oracle for flexible academic reproduction, not
author BlockCopy source, random-state or exact-number replay.
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


RADIUS = 38.5
WAKE_K = 0.075
CT = 0.8

KS1_PROB = [
    0,.01,.01,.01,.01,.20,.60,.01,.01,.01,.01,.01,
    .01,.01,.01,.01,.01,.01,.01,.01,.01,.01,.01,0,
]
KS2_SCALE = [
    7,5,5,5,5,4,5,6,7,7,8,9.5,10,8.5,8.5,6.5,4.6,2.6,
    8,5,6.4,5.2,4.5,3.9,
]
KS2_PROB = [
    .0002,.008,.0227,.0242,.0225,.0339,.0423,.029,.0617,.0813,
    .0994,.1394,.1839,.1115,.0765,.008,.0051,.0019,.0012,.001,
    .0017,.0031,.0097,.0317,
]
COMP1_SCALE = [
    10.443094,11.497257,9.936782,11.960420,9.646003,7.850565,
    10.391076,11.689212,11.306598,10.067243,11.929232,7.213661,
    9.510614,8.435315,7.209497,11.928993,9.096070,8.920670,
    11.822872,9.433660,10.287406,8.840926,8.965424,11.006392,
]
COMP1_SHAPE = [
    2.824893,4.473140,2.668569,4.041643,3.110461,2.667998,
    4.131375,4.355522,2.406574,3.788350,3.252150,3.985223,
    2.969414,3.979477,2.390965,3.563812,3.474557,4.451840,
    2.016430,2.424834,4.199515,3.289647,3.409203,4.241142,
]
COMP1_DENSITY = [
    .115109,.109885,.103791,.096939,.089457,.081482,.073164,.064654,
    .056111,.047693,.039556,.031850,.024717,.018290,.012687,.008012,
    .004352,.001775,.000327,.000036,.000908,.002925,.006051,.010228,
]
COMP3_SCALE = [
    6.620156,7.555507,7.877089,7.783426,7.204585,6.550654,
    7.132810,8.154736,6.852388,8.681447,8.113125,8.997957,
    7.287286,7.660273,6.514132,6.528559,6.804044,6.763974,
    7.002693,8.238606,7.385678,8.075094,8.842956,8.346507,
]
COMP3_SHAPE = [
    3.275544,2.507746,3.454448,3.436239,2.259019,2.930746,
    3.359333,2.187553,2.672880,3.623705,3.490578,3.427143,
    2.841829,2.346667,2.399485,2.918861,3.263791,2.191254,
    2.632711,2.366451,3.049989,3.068045,2.876107,2.723683,
]
COMP3_DENSITY = [
    .055156,.063460,.070582,.076149,.079871,.081553,.081107,.078558,
    .074037,.067781,.060117,.051445,.042218,.032919,.024032,.016022,
    .009307,.004239,.001082,0,.001051,.004179,.009221,.015914,
]

PROBLEMS = {
    "t11_ks1_n100": {
        "n": 100, "scale": [13.0] * 24, "shape": [2.0] * 24,
        "mass": KS1_PROB, "wake_free": None,
    },
    "t11_ks2_n100": {
        "n": 100, "scale": KS2_SCALE, "shape": [2.0] * 24,
        "mass": KS2_PROB, "wake_free": None,
    },
    "t11_comp1_n220": {
        "n": 220, "scale": COMP1_SCALE, "shape": COMP1_SHAPE,
        "mass": [15 * value for value in COMP1_DENSITY],
        "wake_free": 11963.514,
    },
    "t11_comp3_n710": {
        "n": 710, "scale": COMP3_SCALE, "shape": COMP3_SHAPE,
        "mass": [15 * value for value in COMP3_DENSITY],
        "wake_free": 6965.442,
    },
}

ALGORITHMS = [
    "t11_1plus1_blockcopy_mutation",
    "t11_1plus1_blockcopy_both",
    "t11_5comma10_blockcopy_mutation",
    "t11_5comma10_blockcopy_crossover",
]


def run_json(command: list[str]) -> dict[str, Any]:
    return json.loads(subprocess.run(
        command, check=True, capture_output=True, text=True
    ).stdout)


def cdf(speed: float, scale: float, shape: float) -> float:
    if speed <= 0 or scale <= 0:
        return 0.0
    return 1 - math.exp(-(speed / scale) ** shape)


def expected_power(scale: float, shape: float) -> float:
    total = 0.0
    for interval in range(21):
        low = 3.5 + 0.5 * interval
        high = low + 0.5
        speed = (low + high) / 2
        power = max(0.0, 140.86 * speed - 500)
        total += (cdf(high, scale, shape) - cdf(low, scale, shape)) * power
    return total + 1500 * (1 - cdf(14, scale, shape))


def contribution(
    source: list[float],
    target: list[float],
    direction: int,
) -> float:
    if source == target:
        return 0.0
    theta = math.radians(7.5 + 15 * direction)
    cosine, sine = math.cos(theta), math.sin(theta)
    dx, dy = target[0] - source[0], target[1] - source[1]
    ratio = RADIUS / WAKE_K
    numerator = dx * cosine + dy * sine + ratio
    denominator = math.hypot(dx + ratio * cosine, dy + ratio * sine)
    if denominator <= 0:
        return 0.0
    beta = math.acos(max(-1.0, min(1.0, numerator / denominator)))
    if beta >= math.atan(WAKE_K):
        return 0.0
    projected = abs(dx * cosine + dy * sine)
    deficit = (
        1 - math.sqrt(1 - CT)
    ) / (1 + WAKE_K / RADIUS * projected) ** 2
    return deficit * deficit


def independent(layout: list[list[float]], problem: str) -> dict[str, float]:
    spec = PROBLEMS[problem]
    energy = 0.0
    for direction in range(24):
        for target_index, target in enumerate(layout):
            q = sum(
                contribution(source, target, direction)
                for source_index, source in enumerate(layout)
                if source_index != target_index
            )
            retained = max(0.0, 1 - math.sqrt(q))
            energy += (
                spec["mass"][direction]
                * expected_power(
                    spec["scale"][direction] * retained,
                    spec["shape"][direction],
                )
            )
    count = float(spec["n"])
    wake_free = spec["wake_free"]
    if wake_free is None:
        wake_free = sum(
            mass * expected_power(scale, shape)
            for mass, scale, shape in zip(
                spec["mass"], spec["scale"], spec["shape"], strict=True
            )
        )
    capital = (
        (750000 * count + 8000000 * math.floor(count / 30))
        * (2 / 3 + math.exp(-0.00174 * count**2) / 3)
        + 20000 * count
    )
    annuity = (1 - 1.03 ** -20) / 0.03
    cost = capital / annuity / (8760 * energy) + 0.1 / count
    return {
        "energy_output_kw": energy,
        "wake_free_ratio": energy / (wake_free * count),
        "energy_cost": cost,
    }


def csv(layout: list[list[float]]) -> str:
    return ",".join(str(value) for point in layout for value in point)


def optimize(
    binary: Path,
    problem: str,
    algorithm: str,
    workers: int,
    fes: int,
    seed: int,
) -> dict[str, Any]:
    return run_json([
        str(binary), "--problem", problem, "--algorithm", algorithm,
        "--workers", str(workers), "--physical-fes", str(fes),
        "--seed", str(seed),
    ])


def fixed(
    binary: Path,
    problem: str,
    layout: list[list[float]],
    workers: int,
    child: list[list[float]] | None = None,
) -> dict[str, Any]:
    command = [
        str(binary), "--problem", problem, "--workers", str(workers),
        "--layout-csv", csv(layout),
    ]
    if child is not None:
        command.extend(["--incremental-child-csv", csv(child)])
    return run_json(command)


def relative(left: float, right: float) -> float:
    return abs(left - right) / max(abs(left), abs(right), 1e-300)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()

    equation_checks: dict[str, Any] = {}
    layouts: dict[str, list[list[float]]] = {}
    for index, problem in enumerate(PROBLEMS):
        campaign = optimize(
            args.binary, problem, ALGORITHMS[0], 4, 1, 11000 + index
        )
        run = campaign["run_receipts"][0]
        layout = run["final_layout"]
        layouts[problem] = layout
        exact = independent(layout, problem)
        cpp = run["final_evaluation"]
        errors = {
            key: relative(exact[key], cpp[key])
            for key in ["energy_output_kw", "wake_free_ratio", "energy_cost"]
        }
        if max(errors.values()) > 2e-7:
            raise SystemExit(f"T11 equation drift {problem}: {errors}")
        fixed_one = fixed(args.binary, problem, layout, 1)
        fixed_four = fixed(args.binary, problem, layout, 4)
        if fixed_one["evaluation"] != fixed_four["evaluation"]:
            raise SystemExit(f"T11 all-core science drift: {problem}")
        if fixed_four["observed_workers"] != 4:
            raise SystemExit(f"T11 all-core participation drift: {problem}")
        equation_checks[problem] = {
            "independent": exact,
            "cpp": cpp,
            "relative_errors": errors,
            "all_core_science_identity": "exact",
        }

    parent = layouts["t11_ks1_n100"]
    child = [point[:] for point in parent]
    child[0][0] += 0.25
    full_child = fixed(args.binary, "t11_ks1_n100", child, 4)
    incremental = fixed(
        args.binary, "t11_ks1_n100", parent, 4, child=child
    )
    incremental_errors = {
        key: relative(
            full_child["evaluation"][key],
            incremental["evaluation"][key],
        )
        for key in ["energy_output_kw", "wake_free_ratio", "energy_cost"]
    }
    if max(incremental_errors.values()) > 2e-11:
        raise SystemExit(f"T11 incremental drift: {incremental_errors}")

    lifecycle = {}
    for problem in PROBLEMS:
        lifecycle[problem] = {}
        for algorithm in ALGORITHMS:
            receipt = optimize(
                args.binary, problem, algorithm, 1, 15, 11100
            )["run_receipts"][0]
            if (
                receipt["physical_fes"] != 15
                or not receipt["final_evaluation"]["feasible"]
                or len(receipt["final_layout"]) != PROBLEMS[problem]["n"]
            ):
                raise SystemExit(
                    f"T11 lifecycle drift {problem}/{algorithm}"
                )
            lifecycle[problem][algorithm] = {
                "physical_fes": receipt["physical_fes"],
                "feasible": receipt["final_evaluation"]["feasible"],
                "scientific_hash": receipt["scientific_hash"],
            }

    one = optimize(
        args.binary, "t11_ks1_n100", ALGORITHMS[0], 1, 25, 11200
    )["run_receipts"][0]
    four = optimize(
        args.binary, "t11_ks1_n100", ALGORITHMS[0], 4, 25, 11200
    )["run_receipts"][0]
    if (
        one["scientific_hash"] != four["scientific_hash"]
        or four["observed_workers"] != 4
    ):
        raise SystemExit("T11 schedule-independent replay drift")

    payload = {
        "schema_version": 1,
        "corpus_id": "T11",
        "status": "pass",
        "equation_checks": equation_checks,
        "incremental_full_equivalence": {
            "relative_errors": incremental_errors,
            "status": "pass",
        },
        "four_by_four_lifecycle_smoke": lifecycle,
        "schedule_independent_replay": {
            "one_worker_hash": one["scientific_hash"],
            "four_worker_hash": four["scientific_hash"],
            "status": "pass",
        },
    }
    text = json.dumps(payload, indent=2, sort_keys=True) + "\n"
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(text, encoding="utf-8")
    else:
        print(text, end="")


if __name__ == "__main__":
    main()
