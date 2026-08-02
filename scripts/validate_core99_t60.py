#!/usr/bin/env python3
"""
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: independent T60 equations and case-matrix H5 oracle
Paper/DOI: Solving the Wind Farm Layout Optimization Problem Using Random
Search Algorithm; 10.1016/j.renene.2015.01.005
Public source: PyWake revision 5b07481ec9b3633a74844651648f266ba82a8b32
supplies the declared same-lineage Horns coordinates and V80 arrays
Missing/conflicting fields and completion policy:
hpc/core99_cpp/include/core99/feng_t60.hpp
Independence: this script re-derives exact analytic circle overlap,
Jensen/RSS influence, Weibull quadrature, power integration, paper layouts
and the six-case lifecycle without importing production C++.
Method/problem semantic IDs: t60_improved_rs_incremental_v1;
t60_ideal_continuous_jensen_v1; t60_hornsrev_jensen_v80_v1
Controlling contract: shared/contracts/core99_t60_feng_shen_2015.json
Claim boundary: equation/case oracle for the declared flexible reproduction,
not author-source or unavailable random-state replay
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
"""

from __future__ import annotations

import argparse
import json
import math
import subprocess
from pathlib import Path


DIAMETER = 80.0
RADIUS = 40.0
V80_SPEED = list(range(3, 26))
V80_POWER = [
    0, 66.6, 154, 282, 460, 696, 996, 1341, 1661, 1866, 1958,
    1988, 1997, 1999, 2000, 2000, 2000, 2000, 2000, 2000, 2000,
    2000, 2000,
]
V80_CT = [
    0, .818, .806, .804, .805, .806, .807, .793, .739, .709,
    .409, .314, .249, .202, .167, .14, .119, .102, .088, .077,
    .067, .06, .053,
]
SCALE = [8.71,9.36,9.29,10.27,10.89,10.49,10.94,11.23,11.93,11.94,12.17,10.31]
SHAPE = [2.08,2.22,2.41,2.37,2.51,2.75,2.61,2.51,2.33,2.35,2.58,2.01]
FREQUENCY = [3.8,4.3,5.5,8.3,8.7,6.7,8.4,10.5,11.4,12.2,13.9,6.1]


def run_json(command: list[str]) -> dict:
    return json.loads(subprocess.run(
        command, check=True, capture_output=True, text=True
    ).stdout)


def linear(x: list[float], y: list[float], value: float) -> float:
    if value < x[0] or value > x[-1]:
        return 0.0
    for upper in range(1, len(x)):
        if value <= x[upper]:
            f = (value - x[upper - 1]) / (x[upper] - x[upper - 1])
            return y[upper - 1] + f * (y[upper] - y[upper - 1])
    return y[-1]


def overlap(distance: float, first: float, second: float) -> float:
    if distance >= first + second:
        return 0.0
    if distance <= abs(first - second):
        return math.pi * min(first, second) ** 2
    a = math.acos(max(-1.0, min(1.0, (
        distance**2 + first**2 - second**2
    ) / (2 * distance * first))))
    b = math.acos(max(-1.0, min(1.0, (
        distance**2 + second**2 - first**2
    ) / (2 * distance * second))))
    radicand = max(0.0, (
        -distance + first + second
    ) * (
        distance + first - second
    ) * (
        distance - first + second
    ) * (
        distance + first + second
    ))
    return first**2 * a + second**2 * b - .5 * math.sqrt(radicand)


def influence(
    source: tuple[float, float],
    target: tuple[float, float],
    degrees: float,
    decay: float,
) -> float:
    angle = math.radians(degrees)
    dx = target[0] - source[0]
    dy = target[1] - source[1]
    downstream = math.sin(angle) * dx + math.cos(angle) * dy
    if downstream <= 0:
        return 0.0
    crosswind = abs(math.cos(angle) * dx - math.sin(angle) * dy)
    wake_radius = RADIUS + decay * downstream
    fraction = overlap(crosswind, wake_radius, RADIUS) / (math.pi * RADIUS**2)
    expansion = 1 + decay * downstream / RADIUS
    return fraction**2 / expansion**4


def ideal_layout_case1() -> list[tuple[float, float]]:
    return [
        ((column + .5) * 400, (row + .5) * 400)
        for row in [0, 4, 9]
        for column in range(10)
    ]


def horns_layout() -> list[tuple[float, float]]:
    row_x = [0,68,137,205,273,341,410,478]
    row_y = [0,556,1112,1668,2223,2779,3335,3891]
    return [
        (560 * column + row_x[row], row_y[row])
        for column in range(10)
        for row in range(8)
    ]


def exact_power(
    layout: list[tuple[float, float]],
    directions: list[tuple[float, float, list[tuple[float, float]]]],
    decay: float,
    horns: bool,
) -> float:
    total = 0.0
    for degrees, direction_weight, speeds in directions:
        direction_power = 0.0
        for target, point in enumerate(layout):
            q = sum(
                influence(source, point, degrees, decay)
                for index, source in enumerate(layout)
                if index != target
            )
            root_q = math.sqrt(q)
            turbine = 0.0
            for speed, speed_weight in speeds:
                ct = linear(V80_SPEED, V80_CT, speed) if horns else .88
                coefficient = 1 - math.sqrt(max(0.0, 1 - ct))
                effective = speed * max(0.0, 1 - coefficient * root_q)
                power = (
                    linear(V80_SPEED, V80_POWER, effective)
                    if horns else min(.3 * effective**3, 630)
                )
                turbine += speed_weight * power
            direction_power += turbine
        total += direction_weight * direction_power
    return total


def weibull_bins(scale: float, shape: float) -> list[tuple[float, float]]:
    def cdf(speed: float) -> float:
        return 0.0 if speed <= 0 else 1 - math.exp(-(speed / scale) ** shape)
    values = []
    for index in range(61):
        speed = .5 * index
        values.append((speed, cdf(speed + .25) - cdf(max(0, speed - .25))))
    total = sum(weight for _, weight in values)
    return [(speed, weight / total) for speed, weight in values]


def csv(layout: list[tuple[float, float]]) -> str:
    return ",".join(str(value) for point in layout for value in point)


def fixed(
    binary: Path,
    problem: str,
    layout: list[tuple[float, float]],
    sectors: int,
    workers: int,
) -> dict:
    return run_json([
        str(binary), "--problem", problem,
        "--direction-sectors", str(sectors),
        "--workers", str(workers),
        "--layout-csv", csv(layout),
    ])


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()

    ideal_layout = ideal_layout_case1()
    ideal_receipt = fixed(
        args.binary, "t60_ideal_case1", ideal_layout, 360, 4
    )
    ideal_exact = exact_power(
        ideal_layout,
        [(0.0, 1.0, [(12.0, 1.0)])],
        .5 / math.log(60 / .3),
        False,
    )

    horns = horns_layout()
    frequency_total = sum(FREQUENCY)
    height_scale = math.log(70 / .0002) / math.log(62 / .0002)
    horns_directions = [
        (
            30.0 * sector,
            FREQUENCY[sector] / frequency_total,
            weibull_bins(SCALE[sector] * height_scale, SHAPE[sector]),
        )
        for sector in range(12)
    ]
    horns_receipt = fixed(
        args.binary, "t60_horns_case1", horns, 12, 4
    )
    horns_exact = exact_power(
        horns,
        horns_directions,
        .5 / math.log(70 / .0002),
        True,
    )
    comparisons = {
        "ideal_case1": {
            "independent_exact_kw": ideal_exact,
            "cpp_lookup_kw":
                ideal_receipt["evaluation"]["expected_power_kw"],
        },
        "horns_case1_12_sectors": {
            "independent_exact_kw": horns_exact,
            "cpp_lookup_kw":
                horns_receipt["evaluation"]["expected_power_kw"],
        },
    }
    for values in comparisons.values():
        values["absolute_error_kw"] = abs(
            values["independent_exact_kw"] - values["cpp_lookup_kw"]
        )
        values["relative_error"] = (
            values["absolute_error_kw"]
            / max(abs(values["independent_exact_kw"]), 1e-300)
        )
        if values["relative_error"] > 2e-5:
            raise SystemExit(f"T60 lookup precision drift: {values}")

    six_case_smoke = {}
    for problem in [
        "t60_ideal_case1", "t60_ideal_case2", "t60_ideal_case3",
        "t60_horns_case1", "t60_horns_case2", "t60_horns_case3",
    ]:
        receipt = run_json([
            str(args.binary), "--problem", problem,
            "--workers", "1", "--runs", "1",
            "--physical-fes", "1", "--initial", "paper",
            "--seed", "6000",
        ])
        run = receipt["run_receipts"][0]
        if (
            run["physical_fes"] != 1
            or not run["final_evaluation"]["feasible"]
            or run["final_evaluation"]["expected_power_kw"] <= 0
        ):
            raise SystemExit(f"T60 native-case smoke failed: {problem}")
        six_case_smoke[problem] = {
            "power_kw": run["final_evaluation"]["expected_power_kw"],
            "efficiency": run["final_evaluation"]["efficiency"],
            "status": "pass",
        }

    one = run_json([
        str(args.binary), "--problem", "t60_ideal_case1",
        "--workers", "1", "--runs", "1", "--physical-fes", "200",
        "--initial", "paper", "--seed", "6015",
    ])
    all_core_semantics = run_json([
        str(args.binary), "--problem", "t60_ideal_case1",
        "--workers", "4", "--runs", "1", "--physical-fes", "200",
        "--initial", "paper", "--seed", "6015",
    ])
    left = one["run_receipts"][0]
    right = all_core_semantics["run_receipts"][0]
    for field in [
        "scientific_hash", "physical_fes", "feasible_proposals",
        "rejected_infeasible_proposals", "accepted_moves",
        "final_evaluation", "final_layout",
    ]:
        if left[field] != right[field]:
            raise SystemExit(f"T60 schedule identity drift: {field}")

    preprocessing = {}
    for sectors in [12, 72, 360]:
        receipt = fixed(
            args.binary, "t60_horns_case1", horns, sectors, 4
        )
        if receipt["observed_workers"] != 4:
            raise SystemExit("T60 fixed-layout evaluator did not use workers")
        preprocessing[str(sectors)] = {
            "power_kw": receipt["evaluation"]["expected_power_kw"],
            "observed_workers": receipt["observed_workers"],
        }

    payload = {
        "status": "pass",
        "independent_equation_comparisons": comparisons,
        "paper_native_case_smoke": six_case_smoke,
        "schedule_independent_replay": {
            "scientific_hash": left["scientific_hash"],
            "physical_fes": left["physical_fes"],
            "status": "pass",
        },
        "direction_preprocessing_probe": preprocessing,
        "paper_anchor_context": {
            "ideal_case1_ga_reevaluated_kw": 14304.0,
            "horns_case1_original_kw": 81672.0,
            "claim": "non-blocking external-validity anchors, not exact replay gates",
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
