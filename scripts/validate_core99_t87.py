#!/usr/bin/env python3
"""
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: independent T87 fixture, four-case, spacing, unique-FES,
IGA-PSO lifecycle, multicore parity and replay H5 validator
Paper/DOI: Wind Farm Layout Optimization in Complex Terrain Based on CFD and
IGA-PSO; 10.1016/j.energy.2023.129745
Public source/missing/reconstruction:
hpc/core99_cpp/include/core99/hu_t87.hpp
Controlling contract: shared/contracts/core99_t87_hu_iga_pso_2024.json
Claim boundary: academic declared reproduction on a hashed figure-derived
proxy; not author CFD/data/source/random state/exact numerical replay
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path
import random
import struct
import subprocess


EXPECTED_PROXY_SHA256 = (
    "e8fbb28bd24f97aaef4923907e03d91afd4b4e07adb4a370c2d2c9bc2f13ea8a"
)


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


def call(binary: str, arguments: list[str]) -> dict:
    completed = subprocess.run(
        [binary, *arguments],
        check=True,
        text=True,
        capture_output=True,
    )
    return json.loads(completed.stdout)


def read_proxy(path: Path) -> tuple[list[tuple[float, float, float]], int, int]:
    payload = path.read_bytes()
    require(
        hashlib.sha256(payload).hexdigest() == EXPECTED_PROXY_SHA256,
        "T87 proxy SHA-256 mismatch",
    )
    require(payload[:8] == b"T87PXY2\0", "T87 proxy magic mismatch")
    candidate_count, state_count, curve_count = struct.unpack_from(
        "<III", payload, 8
    )
    offset = 20
    candidates = [
        struct.unpack_from("<fff", payload, offset + 12 * index)
        for index in range(candidate_count)
    ]
    expected_size = 20 + 12 * (
        candidate_count + state_count + curve_count
    )
    require(len(payload) == expected_size, "T87 proxy size mismatch")
    return candidates, state_count, curve_count


def paper_scale_layout(
    candidates: list[tuple[float, float, float]],
) -> list[int]:
    order = list(range(len(candidates)))
    random.Random(0).shuffle(order)
    selected: list[int] = []
    for candidate in order:
        x_coord, y_coord, _ = candidates[candidate]
        if all(
            math.hypot(
                x_coord - candidates[other][0],
                y_coord - candidates[other][1],
            )
            >= 4.0
            for other in selected
        ):
            selected.append(candidate)
    return sorted(selected)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", required=True)
    parser.add_argument("--data", type=Path, required=True)
    args = parser.parse_args()

    candidates, state_count, curve_count = read_proxy(args.data)
    require(len(candidates) == 522, "T87 candidate count mismatch")
    require(
        abs(len(candidates) / (41 * 185) - 0.069) < 0.0002,
        "T87 candidate fraction does not round to paper's 6.9%",
    )
    require(state_count == 49, "T87 wind-state count mismatch")
    require(curve_count == 38, "T87 turbine-curve count mismatch")
    require(
        abs(min(item[2] for item in candidates) - 2000.0) < 1.0e-5
        and abs(max(item[2] for item in candidates) - 2500.0) < 1.0e-5,
        "T87 AEH proxy anchor mismatch",
    )
    layout = paper_scale_layout(candidates)
    require(
        15 <= len(layout) <= 18,
        "independent 4D packing does not close against Table 2 scale",
    )
    indices = ",".join(str(value) for value in layout)
    single = call(
        args.binary,
        [
            "--mode", "evaluate-grid",
            "--case", "t87_case1_jensen_aep",
            "--data", str(args.data),
            "--indices", "0",
            "--workers", "4",
        ],
    )["evaluation"]
    require(
        abs(single["aep_mwh"] / 3.3 - candidates[0][2]) < 1.0e-5,
        "candidate AEH-to-speed calibration mismatch",
    )
    case_ids = [
        "t87_case1_jensen_aep",
        "t87_case2_gwm_aep",
        "t87_case3_dgwm_aep",
        "t87_case4_jensen_nav",
    ]
    case_evaluations: dict[str, dict] = {}
    for case_id in case_ids:
        payload = call(
            args.binary,
            [
                "--mode", "evaluate-grid",
                "--case", case_id,
                "--data", str(args.data),
                "--indices", indices,
                "--workers", "4",
            ],
        )
        require(
            payload["problem_semantic_id"]
            == "t87_qianjiang_figure_proxy_v1",
            f"{case_id}: semantic ID mismatch",
        )
        evaluation = payload["evaluation"]
        require(evaluation["feasible"], f"{case_id}: oracle infeasible")
        require(
            evaluation["turbine_count"] == len(layout)
            and evaluation["aep_mwh"] > 0.0
            and 0.0 < evaluation["wake_efficiency"] <= 1.0,
            f"{case_id}: physical bounds mismatch",
        )
        if case_id.endswith("_nav"):
            require(
                evaluation["fitness"] == evaluation["nav_rmb_per_year"],
                "NAV objective mapping mismatch",
            )
        else:
            require(
                evaluation["fitness"] == evaluation["aep_mwh"],
                "AEP objective mapping mismatch",
            )
        case_evaluations[case_id] = evaluation

    common = [
        "--mode", "optimize",
        "--case", "t87_case1_jensen_aep",
        "--data", str(args.data),
        "--iga-population", "50",
        "--iga-generations", "4",
        "--pso-population", "20",
        "--pso-iterations", "3",
        "--seed", "870087",
    ]
    serial = call(args.binary, [*common, "--workers", "1"])
    parallel = call(args.binary, [*common, "--workers", "4"])
    replay = call(args.binary, [*common, "--workers", "4"])
    require(
        serial["method_semantic_id"]
        == "t87_iga_pso_predecessor_completed_v1",
        "T87 method semantic ID mismatch",
    )
    require(
        serial["proposed_fes"] == 330
        and parallel["proposed_fes"] == 330,
        "T87 proposed FES mismatch",
    )
    require(
        serial["physical_unique_fes"] <= serial["proposed_fes"]
        and parallel["physical_unique_fes"] <= parallel["proposed_fes"],
        "T87 unique FES accounting mismatch",
    )
    require(
        serial["scientific_hash"]
        == parallel["scientific_hash"]
        == replay["scientific_hash"],
        "T87 serial/multicore/replay scientific parity failed",
    )
    require(
        parallel["observed_workers"] >= 2,
        "T87 has no persistent-team multicore evidence",
    )
    require(
        parallel["best_grid_evaluation"]["feasible"]
        and parallel["best_continuous_evaluation"]["feasible"],
        "T87 smoke optimizer did not preserve feasibility",
    )
    require(
        parallel["best_continuous_evaluation"]["fitness"] + 1.0e-8
        >= parallel["best_grid_evaluation"]["fitness"],
        "T87 PSO did not retain the IGA starting point",
    )

    report = {
        "status": "pass",
        "problem_semantic_id": "t87_qianjiang_figure_proxy_v1",
        "method_semantic_id": "t87_iga_pso_predecessor_completed_v1",
        "proxy_sha256": EXPECTED_PROXY_SHA256,
        "candidate_count": len(candidates),
        "candidate_fraction": len(candidates) / (41 * 185),
        "paper_scale_4d_packing_count": len(layout),
        "wind_state_count": state_count,
        "turbine_curve_point_count": curve_count,
        "validated_paper_case_count": len(case_ids),
        "serial_parallel_replay_scientific_parity": True,
        "smoke_proposed_fes": parallel["proposed_fes"],
        "smoke_physical_unique_fes": parallel["physical_unique_fes"],
        "observed_inner_workers": parallel["observed_workers"],
        "smoke_scientific_hash": parallel["scientific_hash"],
        "case_fitness": {
            case_id: value["fitness"]
            for case_id, value in case_evaluations.items()
        },
    }
    print(json.dumps(report, sort_keys=True))


if __name__ == "__main__":
    main()
