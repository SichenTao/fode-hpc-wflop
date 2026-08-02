#!/usr/bin/env python3
"""
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T72 Waffle all-core H6 and 1,440-run formal runner
Paper/DOI: Constrained Multi-Objective Wind Farm Layout Optimization:
Novel Constraint Handling Approach Based on Constraint Programming;
10.1016/j.renene.2018.03.053
Public source: no author implementation or native maps were located
Missing/conflicts/completion:
hpc/core99_cpp/include/core99/sorkhabi_t72.hpp
Formal protocol: nine native land-availability/turbine-count problems by four
paper maximum-distance settings by 20 seeds and two penalty coefficients,
9*4*20*2=1,440 target-method runs
HPC protocol: representative identical trajectories use one worker versus all
cores; a 20-run sequential/all-core campaign proves hierarchical scheduling;
formal campaigns always consume the requested allocation without oversubscription
Method/problem semantic IDs: t72_chcp_nsga2_declared_reconstruction_v1;
t72_energy_noise_voronoi9_declared_reconstruction_v1
Claim boundary: academic declared flexible reproduction, not author code,
IBM CP state, native maps, random states, or exact numerical replay
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
import platform
import statistics
import subprocess
from pathlib import Path
from typing import Any


PAPER_CASES = [
    (availability, turbines)
    for availability in [70, 80, 90]
    for turbines in [5, 10, 15]
]
MAXIMUM_DISTANCES_BIN2 = [50.0, 100.0, 1000.0, 10000.0]
PENALTIES = [10000.0, 40000.0]


def run_json(command: list[str]) -> dict[str, Any]:
    completed = subprocess.run(
        command,
        check=True,
        capture_output=True,
        text=True,
    )
    return json.loads(completed.stdout)


def write_json(path: Path, payload: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(
        json.dumps(payload, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    temporary.replace(path)


def file_sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        while block := stream.read(1024 * 1024):
            digest.update(block)
    return digest.hexdigest()


def optimize(
    binary: Path,
    availability: int,
    turbines: int,
    workers: int,
    runs: int,
    physical_fes: int,
    maximum_distance_bin2: float,
    seed: int,
    paper_run_matrix: bool = False,
) -> dict[str, Any]:
    command = [
        str(binary),
        "--land-availability-percent", str(availability),
        "--turbines", str(turbines),
        "--workers", str(workers),
        "--runs", str(runs),
        "--physical-fes", str(physical_fes),
        "--maximum-repair-distance-bin2", str(maximum_distance_bin2),
        "--seed", str(seed),
        "--penalty-coefficient", "10000",
    ]
    if paper_run_matrix:
        command.append("--paper-run-matrix")
    return run_json(command)


def median_field(receipts: list[dict[str, Any]], field: str) -> float:
    return statistics.median(
        float(receipt["run_receipts"][0][field])
        for receipt in receipts
    )


def exact_identity(
    reference: dict[str, Any],
    candidate: dict[str, Any],
) -> None:
    left = reference["run_receipts"]
    right = candidate["run_receipts"]
    if len(left) != len(right):
        raise SystemExit("T72 H6 run cardinality drift")
    fields = [
        "problem_id",
        "seed",
        "physical_fes",
        "generations",
        "population_size",
        "maximum_repair_distance_bin2",
        "penalty_coefficient",
        "repair_attempts",
        "repair_successes",
        "repair_timeouts",
        "repair_node_limit_hits",
        "repair_search_nodes",
        "converged",
        "scientific_hash",
        "front",
    ]
    for index, (expected, observed) in enumerate(zip(left, right)):
        for field in fields:
            if expected[field] != observed[field]:
                raise SystemExit(
                    f"T72 H6 trajectory drift run={index} field={field}"
                )


def h6(
    binary: Path,
    workers: int,
    observations: int,
) -> dict[str, Any]:
    cases = {}
    for maximum_distance in [100.0, 10000.0]:
        serial = [
            optimize(
                binary, 70, 15, 1, 1, 2000,
                maximum_distance, 72018,
            )
            for _ in range(observations)
        ]
        parallel = [
            optimize(
                binary, 70, 15, workers, 1, 2000,
                maximum_distance, 72018,
            )
            for _ in range(observations)
        ]
        for receipt in parallel:
            exact_identity(serial[0], receipt)
            if (
                receipt["run_receipts"][0]["observed_workers"]
                != workers
            ):
                raise SystemExit("T72 H6 did not observe every CPU worker")
        timing = {}
        for field in [
            "repair_seconds",
            "evaluator_seconds",
            "algorithm_seconds",
            "end_to_end_seconds",
        ]:
            one = median_field(serial, field)
            all_core = median_field(parallel, field)
            timing[field] = {
                "one_worker_median_seconds": one,
                "all_core_median_seconds": all_core,
                "speedup": one / max(all_core, 1.0e-15),
            }
        if timing["end_to_end_seconds"]["speedup"] <= 1.0:
            raise SystemExit("T72 H6 end-to-end all-core execution regressed")
        cases[str(int(maximum_distance))] = {
            "maximum_repair_distance_bin2": maximum_distance,
            "physical_fes": 2000,
            "observations": observations,
            "all_core_observed_workers": workers,
            "scientific_hash":
                serial[0]["run_receipts"][0]["scientific_hash"],
            "timing": timing,
            "status": "pass",
        }

    sequential = optimize(
        binary, 70, 15, 1, 20, 1000, 100.0, 72100
    )
    all_core = optimize(
        binary, 70, 15, workers, 20, 1000, 100.0, 72100
    )
    exact_identity(sequential, all_core)
    if all_core["campaign_observed_workers"] != workers:
        raise SystemExit("T72 H6 campaign did not observe every worker")
    campaign_speedup = (
        sequential["campaign_seconds"]
        / all_core["campaign_seconds"]
    )
    if campaign_speedup <= 1.0:
        raise SystemExit("T72 H6 all-core campaign regressed")
    return {
        "status": "pass",
        "selected_workers": workers,
        "trajectory_cases": cases,
        "twenty_run_campaign": {
            "physical_fes_per_run": 1000,
            "one_worker_seconds": sequential["campaign_seconds"],
            "all_core_seconds": all_core["campaign_seconds"],
            "speedup": campaign_speedup,
            "all_core_observed_workers":
                all_core["campaign_observed_workers"],
            "scientific_hashes": [
                item["scientific_hash"]
                for item in all_core["run_receipts"]
            ],
            "status": "pass",
        },
    }


def dominates(left: tuple[float, float], right: tuple[float, float]) -> bool:
    return (
        left[0] >= right[0]
        and left[1] <= right[1]
        and (left[0] > right[0] or left[1] < right[1])
    )


def merged_front_size(receipts: list[dict[str, Any]]) -> int:
    points = [
        (float(point["aep_gwh"]), float(point["maximum_spl_dba"]))
        for run in receipts
        for point in run["front"]
    ]
    unique = sorted(set(points), key=lambda point: (-point[0], point[1]))
    front = []
    best_noise = math.inf
    for point in unique:
        if point[1] < best_noise:
            front.append(point)
            best_noise = point[1]
    return len(front)


def validate_formal_campaign(
    campaign: dict[str, Any],
    availability: int,
    turbines: int,
    maximum_distance: float,
    physical_fes: int,
    workers: int,
) -> dict[str, Any]:
    runs = campaign["run_receipts"]
    if (
        campaign["runs"] != 40
        or len(runs) != 40
        or campaign["campaign_observed_workers"] != workers
        or not campaign["paper_run_matrix"]
    ):
        raise SystemExit("T72 formal campaign topology drift")
    expected_population = {70: 200, 80: 150, 90: 100}[availability]
    expected_seeds = [20260731 + index % 20 for index in range(40)]
    expected_penalties = [PENALTIES[index // 20] for index in range(40)]
    for index, run in enumerate(runs):
        if (
            run["problem_id"] != f"t72_phi{availability}_n{turbines}"
            or run["seed"] != expected_seeds[index]
            or run["penalty_coefficient"] != expected_penalties[index]
            or run["maximum_repair_distance_bin2"] != maximum_distance
            or run["population_size"] != expected_population
            or run["physical_fes"] > physical_fes
            or run["physical_fes"] < expected_population
            or run["physical_fes"] % expected_population != 0
            or run["repair_timeouts"] != 0
            or not run["front"]
            or not math.isfinite(run["end_to_end_seconds"])
        ):
            raise SystemExit(
                "T72 formal quality gate failed "
                f"phi={availability} n={turbines} "
                f"md={maximum_distance} run={index}"
            )
    attempted = sum(run["repair_attempts"] for run in runs)
    repaired = sum(run["repair_successes"] for run in runs)
    return {
        "problem_id": f"t72_phi{availability}_n{turbines}",
        "maximum_repair_distance_bin2": maximum_distance,
        "runs": 40,
        "total_physical_fes": sum(run["physical_fes"] for run in runs),
        "converged_runs": sum(bool(run["converged"]) for run in runs),
        "repair_attempts": attempted,
        "repair_successes": repaired,
        "cp_percentage": 100.0 * repaired / max(attempted, 1),
        "repair_node_limit_hits": sum(
            run["repair_node_limit_hits"] for run in runs
        ),
        "repair_timeouts": 0,
        "minimum_front_size": min(len(run["front"]) for run in runs),
        "maximum_front_size": max(len(run["front"]) for run in runs),
        "merged_front_size": merged_front_size(runs),
        "campaign_seconds": campaign["campaign_seconds"],
        "campaign_observed_workers":
            campaign["campaign_observed_workers"],
        "run_parallelism": campaign["run_parallelism"],
        "workers_per_run": campaign["workers_per_run"],
        "status": "pass",
    }


def formal(
    binary: Path,
    output_root: Path,
    workers: int,
    physical_fes: int,
) -> dict[str, Any]:
    raw_root = output_root / "formal_raw"
    raw_root.mkdir(parents=True, exist_ok=True)
    summaries = []
    total_runs = 0
    for availability, turbines in PAPER_CASES:
        for maximum_distance in MAXIMUM_DISTANCES_BIN2:
            stem = (
                f"phi{availability}_n{turbines}"
                f"_md{int(maximum_distance)}"
            )
            raw_path = raw_root / f"{stem}.json"
            if raw_path.exists():
                campaign = json.loads(raw_path.read_text(encoding="utf-8"))
            else:
                campaign = optimize(
                    binary,
                    availability,
                    turbines,
                    workers,
                    40,
                    physical_fes,
                    maximum_distance,
                    20260731,
                    paper_run_matrix=True,
                )
                write_json(raw_path, campaign)
            summary = validate_formal_campaign(
                campaign,
                availability,
                turbines,
                maximum_distance,
                physical_fes,
                workers,
            )
            summary["raw_receipt"] = str(raw_path.relative_to(output_root))
            summary["raw_sha256"] = file_sha256(raw_path)
            summaries.append(summary)
            total_runs += 40
            write_json(
                output_root / "formal_progress.json",
                {
                    "status": "running",
                    "completed_campaigns": len(summaries),
                    "required_campaigns": 36,
                    "completed_target_runs": total_runs,
                    "required_target_runs": 1440,
                    "campaigns": summaries,
                },
            )
    if total_runs != 1440:
        raise SystemExit("T72 formal target-run cardinality drift")
    return {
        "status": "pass",
        "required_target_runs": 1440,
        "completed_target_runs": total_runs,
        "physical_fes_cap_per_run": physical_fes,
        "paper_cases": 9,
        "maximum_distance_settings": MAXIMUM_DISTANCES_BIN2,
        "seeds_per_setup": 20,
        "penalty_coefficients": PENALTIES,
        "campaigns": summaries,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--output-root", type=Path, required=True)
    parser.add_argument("--source-commit", required=True)
    parser.add_argument("--workers", type=int, default=20)
    parser.add_argument("--timing-observations", type=int, default=3)
    parser.add_argument("--physical-fes", type=int, default=80000)
    parser.add_argument("--run-formal", action="store_true")
    arguments = parser.parse_args()
    if (
        arguments.workers <= 1
        or arguments.timing_observations < 3
        or arguments.physical_fes != 80000
    ):
        raise SystemExit("invalid T72 H6/formal contract")
    arguments.binary = arguments.binary.resolve()
    arguments.output_root.mkdir(parents=True, exist_ok=True)
    receipt = {
        "schema_version": 1,
        "corpus_id": "T72",
        "source_commit": arguments.source_commit,
        "binary": str(arguments.binary),
        "binary_sha256": file_sha256(arguments.binary),
        "host": platform.node(),
        "platform": platform.platform(),
        "cpu_count": os.cpu_count(),
        "selected_workers": arguments.workers,
        "h6": h6(
            arguments.binary,
            arguments.workers,
            arguments.timing_observations,
        ),
        "formal": {
            "status": "not_requested",
            "required_target_runs": 1440,
        },
    }
    write_json(arguments.output_root / "t72_h6.json", receipt)
    if arguments.run_formal:
        receipt["formal"] = formal(
            arguments.binary,
            arguments.output_root,
            arguments.workers,
            arguments.physical_fes,
        )
        write_json(
            arguments.output_root / "t72_h6_and_formal.json",
            receipt,
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
