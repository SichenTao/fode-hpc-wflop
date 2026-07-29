#!/usr/bin/env python3
"""Validate the corrected T-MOEA Nysted R4 reconstruction.

WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: independent T-MOEA Nysted scalar, biobjective, and front
oracle
Paper title: A Topology-Driven Multi-Objective Evolutionary Algorithm for
Offshore Wind Farm Layout Optimization
DOI: 10.1109/CPEEE69412.2026.11521465
Paper provides: Eqs. (2)-(3) wake expansion and fixed 2/3 deficit, the
biobjective vector, and Eq. (16) complement-set relocation.
Public author code URL: no T-MOEA source was found; the related Nysted asset
comes from https://github.com/zbh0528/WFLO-GGA
Public author code revision or archive hash:
6ce41326e6c1d3685a01e038baf6d1d07aa46126
Public code/assets provide: the same-author Nysted wind, turbine, candidate,
substation, cable, and balanced-substation radial routing data.
Known missing information: original T-MOEA candidate set, router, controls,
seed, and reference front.
Reconstruction performed here: an independent SciPy not-a-knot T-MOEA wake
and Python cable oracle, plus independent nondominance checks.
Method evidence tier: M3_DECLARED_COMPLETION.
Problem evidence tier: P2_CITATION_SAME_AUTHOR.
Method semantic ID: tmoea_nysted_gga_asset_reconstruction_paper_eq16_v2
Problem semantic ID: tmoea_nysted_paper_wake_gga_router_problem_v1
Controlling contracts:
shared/contracts/tmoea_nysted_paper_eq16_r4_execution_contract.json
Claim boundary: corrected declared reconstruction only; no original-study or
reference-front reproduction claim.
Last evidence audit date: 2026-07-29
END WFLOP IMPLEMENTATION FACT DECLARATION
"""

from __future__ import annotations

import argparse
import json
import math
import subprocess
import tempfile
from pathlib import Path

import numpy as np
from scipy.interpolate import CubicSpline

from validate_gga_cpp_evaluator import load_problem, route_bsr


METHOD_ID = "tmoea_nysted_gga_asset_reconstruction_paper_eq16_v2"
PROBLEM_RECORD_ID = "nysted_tmoea_paper_wake_gga_router_reconstruction"
PROBLEM_ID = "tmoea_nysted_paper_wake_gga_router_problem_v1"
ASSET_SHA256 = (
    "920cb61b0dc1415af4b6908252799d703"
    "c884f2eeaec9ba7b9590c42d40791c4"
)
FROZEN_LAYOUT_AEP_KWH = 1_041_510_900.4830287
FROZEN_LAYOUT_CAPACITY_FACTOR = 0.3931677857516688
FROZEN_LAYOUT_CABLE_COST = 9_306_538.200114219
SCIENTIFIC_FIELDS = [
    "algorithm_id",
    "method_id",
    "method_semantic_id",
    "execution_profile_id",
    "problem_id",
    "problem_semantics_id",
    "problem_asset_sha256",
    "case_id",
    "seed",
    "physical_fes",
    "generations",
    "population_size",
    "best_lcoe",
    "best_capacity_factor",
    "best_aep_kwh",
    "best_cable_cost",
    "best_layout_0based",
    "nondominated_count",
    "front",
    "work_receipt",
    "final_population_hash",
    "nondominated_front_hash",
    "claim_boundary",
]


def tmoea_evaluate(layout: list[int], problem: dict) -> dict:
    coordinates = problem["candidates"][layout]
    cable_cost = route_bsr(coordinates, problem)
    curve = problem["curve"]
    power_curve = CubicSpline(
        curve[:, 0],
        curve[:, 1],
        bc_type="not-a-knot",
    )
    rotor_radius = problem["rotor_diameter_m"] / 2.0
    wake_expansion = 0.5 / math.log(problem["hub_height_m"] / 0.0002)
    expected_power_kw = 0.0
    for direction, angle in enumerate(problem["theta"]):
        rotation = np.asarray([
            [math.cos(angle), -math.sin(angle)],
            [math.sin(angle), math.cos(angle)],
        ])
        rotated = coordinates @ rotation.T
        order = list(np.argsort(-rotated[:, 1], kind="stable"))
        for speed_index, free_speed in enumerate(problem["velocity"]):
            effective = np.full(len(coordinates), free_speed)
            for downstream_position in range(1, len(order)):
                downstream = order[downstream_position]
                squared_deficit = 0.0
                for upstream_position in range(downstream_position):
                    upstream = order[upstream_position]
                    along = (
                        rotated[upstream, 1] - rotated[downstream, 1]
                    )
                    cross = abs(
                        rotated[downstream, 0] - rotated[upstream, 0]
                    )
                    wake_radius = rotor_radius + wake_expansion * along
                    if along > 0.0 and cross < wake_radius:
                        deficit = (
                            (2.0 / 3.0)
                            * (rotor_radius / wake_radius) ** 2
                        )
                        squared_deficit += deficit * deficit
                effective[downstream] = free_speed * max(
                    0.0,
                    1.0 - math.sqrt(squared_deficit),
                )
            farm_power = sum(
                float(power_curve(speed))
                for speed in effective
                if problem["cut_in_mps"]
                <= speed
                <= problem["cut_out_mps"]
            )
            probability_index = (
                direction * len(problem["velocity"]) + speed_index
            )
            expected_power_kw += (
                farm_power * problem["probability"][probability_index]
            )
    aep_kwh = expected_power_kw * 365.0 * 24.0
    capacity_factor = aep_kwh / (
        365.0
        * 24.0
        * problem["pinst_kw"]
        * problem["turbines"]
    )
    return {
        "aep_kwh": aep_kwh,
        "capacity_factor": capacity_factor,
        "cable_cost": cable_cost,
        "objectives": [-aep_kwh, cable_cost],
    }


def close(observed: float, expected: float, label: str) -> None:
    scaled = abs(observed - expected) / max(1.0, abs(expected))
    if scaled > 2e-12:
        raise RuntimeError(
            f"{label} differs: observed={observed} expected={expected}"
        )


def dominates(left: list[float], right: list[float]) -> bool:
    return all(a <= b for a, b in zip(left, right)) and any(
        a < b for a, b in zip(left, right)
    )


def run(
    binary: Path,
    problem: Path,
    output: Path,
    physical_fes: int,
    seed: int,
    workers: int | None,
) -> dict:
    command = [
        str(binary),
        "--algorithm",
        "tmoea",
        "--tmoea-profile",
        "paper-eq16-v2",
        "--execution-mode",
        "auto",
        "--problem",
        str(problem),
        "--physical-fes",
        str(physical_fes),
        "--seed",
        str(seed),
        "--output",
        str(output),
    ]
    if workers is not None:
        command.extend(["--workers", str(workers)])
    subprocess.run(
        command,
        check=True,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    return json.loads(output.read_text(encoding="utf-8"))


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--problem", type=Path, required=True)
    parser.add_argument("--physical-fes", type=int, default=150)
    parser.add_argument("--seed", type=int, default=20260729)
    arguments = parser.parse_args()

    problem = load_problem(arguments.problem)
    alpha = 0.5 / math.log(problem["hub_height_m"] / 0.0002)
    close(alpha, 0.03836258611404279, "Eq. (2) wake expansion")
    scalar_deficit = (
        (2.0 / 3.0)
        * (
            (problem["rotor_diameter_m"] / 2.0)
            / (
                problem["rotor_diameter_m"] / 2.0
                + alpha * 500.0
            )
        ) ** 2
    )
    close(scalar_deficit, 0.378083374882509, "Eq. (3) deficit")

    frozen_layout = list(range(problem["turbines"]))
    expected_frozen = tmoea_evaluate(frozen_layout, problem)
    close(
        expected_frozen["aep_kwh"],
        FROZEN_LAYOUT_AEP_KWH,
        "frozen-layout AEP oracle",
    )
    close(
        expected_frozen["capacity_factor"],
        FROZEN_LAYOUT_CAPACITY_FACTOR,
        "frozen-layout capacity-factor oracle",
    )
    close(
        expected_frozen["cable_cost"],
        FROZEN_LAYOUT_CABLE_COST,
        "frozen-layout cable oracle",
    )

    with tempfile.TemporaryDirectory(prefix="tmoea-r4-oracle-") as temp:
        temp_path = Path(temp)
        one = run(
            arguments.binary,
            arguments.problem,
            temp_path / "one.json",
            arguments.physical_fes,
            arguments.seed,
            1,
        )
        default_all = run(
            arguments.binary,
            arguments.problem,
            temp_path / "all.json",
            arguments.physical_fes,
            arguments.seed,
            None,
        )
        differing = [
            field
            for field in SCIENTIFIC_FIELDS
            if one[field] != default_all[field]
        ]
        if differing:
            raise RuntimeError(
                f"T-MOEA v2 worker semantics differ: {differing}"
            )
        if one["method_semantic_id"] != METHOD_ID:
            raise RuntimeError("T-MOEA v2 method identity differs")
        if one["problem_semantics_id"] != PROBLEM_ID:
            raise RuntimeError("T-MOEA v2 problem identity differs")
        if one["problem_id"] != PROBLEM_RECORD_ID:
            raise RuntimeError("T-MOEA v2 problem record identity differs")
        if one["problem_asset_sha256"] != ASSET_SHA256:
            raise RuntimeError("T-MOEA v2 Nysted asset hash differs")
        if one["physical_fes"] != arguments.physical_fes:
            raise RuntimeError("T-MOEA v2 physical FES is not exact")
        if one["work_receipt"]["complete_layout_evaluations"] != (
            arguments.physical_fes
        ):
            raise RuntimeError("T-MOEA v2 work receipt differs from FES")
        if default_all["requested_workers"] != 0:
            raise RuntimeError("T-MOEA v2 default did not request all workers")
        if default_all["resolved_workers"] < 1:
            raise RuntimeError("T-MOEA v2 default resolved no CPU workers")
        if default_all["resolved_execution_mode"] != "cpu":
            raise RuntimeError("T-MOEA v2 auto mode did not resolve to CPU")
        if one["best_lcoe"] is not None:
            raise RuntimeError("T-MOEA v2 evaluated a foreign LCOE objective")

        independent_front = []
        for index, point in enumerate(one["front"]):
            expected = tmoea_evaluate(point["layout_0based"], problem)
            close(
                point["aep_kwh"],
                expected["aep_kwh"],
                f"front[{index}] AEP",
            )
            close(
                point["cable_cost"],
                expected["cable_cost"],
                f"front[{index}] cable",
            )
            close(
                point["objectives"][0],
                expected["objectives"][0],
                f"front[{index}] objective 0",
            )
            close(
                point["objectives"][1],
                expected["objectives"][1],
                f"front[{index}] objective 1",
            )
            independent_front.append(expected["objectives"])
        for left_index, left in enumerate(independent_front):
            for right_index, right in enumerate(independent_front):
                if left_index != right_index and dominates(left, right):
                    raise RuntimeError(
                        "independent oracle found a dominated front member"
                    )

        for mode in ("hybrid", "gpu"):
            failed = subprocess.run(
                [
                    str(arguments.binary),
                    "--algorithm",
                    "tmoea",
                    "--tmoea-profile",
                    "paper-eq16-v2",
                    "--execution-mode",
                    mode,
                    "--problem",
                    str(arguments.problem),
                    "--physical-fes",
                    "30",
                ],
                check=False,
                capture_output=True,
                text=True,
            )
            if failed.returncode == 0 or mode not in failed.stderr:
                raise RuntimeError(
                    f"T-MOEA v2 did not fail closed for {mode}"
                )

    print(
        "tmoea_nysted_r4_oracle_pass "
        f"physical_fes={arguments.physical_fes} "
        f"front={one['nondominated_count']} "
        f"workers=1,default-all({default_all['resolved_workers']})"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
