#!/usr/bin/env python3
"""
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T11 H6 performance admission and 480-run formal campaign
Paper/DOI: BlockCopy-Based Operators for Evolving Efficient Wind Farm
Layouts; 10.1109/CEC.2016.7743909
Public source, missing/conflicting facts and completion policy:
hpc/core99_cpp/include/core99/blockcopy_t11.hpp
Formal scope: four target BlockCopy ES methods by four paper-native problems
by 30 independent seeds, each using 2000 complete layout evaluations.
Method/problem semantic IDs: t11_blockcopy_four_es_methods_v1;
t11_kusiak_and_2014_competition_four_cases_v1
Controlling contract: shared/contracts/core99_t11_blockcopy_2016.json
HPC admission: full evaluator all-core scaling, full-to-incremental complexity
gain, single-trajectory all-core speedup, independent-run campaign speedup,
and exact worker-count science identity.
Claim boundary: formal results of the declared flexible reproduction, not
author exact-number replay.
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
"""

from __future__ import annotations

import argparse
import json
import statistics
import subprocess
import time
from pathlib import Path
from typing import Any


PROBLEMS = [
    "t11_ks1_n100",
    "t11_ks2_n100",
    "t11_comp1_n220",
    "t11_comp3_n710",
]
ALGORITHMS = [
    "t11_1plus1_blockcopy_mutation",
    "t11_1plus1_blockcopy_both",
    "t11_5comma10_blockcopy_mutation",
    "t11_5comma10_blockcopy_crossover",
]
TURBINES = {
    "t11_ks1_n100": 100,
    "t11_ks2_n100": 100,
    "t11_comp1_n220": 220,
    "t11_comp3_n710": 710,
}


def write_json(path: Path, payload: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(
        json.dumps(payload, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )


def run_json(command: list[str]) -> dict[str, Any]:
    return json.loads(subprocess.run(
        command, check=True, capture_output=True, text=True
    ).stdout)


def csv(layout: list[list[float]]) -> str:
    return ",".join(str(value) for point in layout for value in point)


def optimize(
    binary: Path,
    problem: str,
    algorithm: str,
    workers: int,
    runs: int,
    fes: int,
    seed: int,
    output: Path | None = None,
) -> dict[str, Any]:
    command = [
        str(binary), "--problem", problem, "--algorithm", algorithm,
        "--workers", str(workers), "--runs", str(runs),
        "--physical-fes", str(fes), "--seed", str(seed),
    ]
    if output:
        output.parent.mkdir(parents=True, exist_ok=True)
        command.extend(["--output", str(output)])
        subprocess.run(command, check=True)
        return json.loads(output.read_text(encoding="utf-8"))
    return run_json(command)


def fixed(
    binary: Path,
    problem: str,
    layout: list[list[float]],
    workers: int,
    repeats: int,
    child: list[list[float]] | None = None,
) -> dict[str, Any]:
    command = [
        str(binary), "--problem", problem, "--workers", str(workers),
        "--layout-csv", csv(layout),
        "--evaluation-repeats", str(repeats),
    ]
    if child is not None:
        command.extend(["--incremental-child-csv", csv(child)])
    return run_json(command)


def exact_campaign_identity(
    left: dict[str, Any],
    right: dict[str, Any],
) -> None:
    left_runs = left["run_receipts"]
    right_runs = right["run_receipts"]
    if len(left_runs) != len(right_runs):
        raise SystemExit("T11 H6 campaign cardinality drift")
    for index, (first, second) in enumerate(zip(
        left_runs, right_runs, strict=True
    )):
        if (
            first["seed"] != second["seed"]
            or first["scientific_hash"] != second["scientific_hash"]
            or first["final_evaluation"] != second["final_evaluation"]
        ):
            raise SystemExit(f"T11 H6 science drift at run {index}")


def h6(
    binary: Path,
    workers: int,
    observations: int,
) -> dict[str, Any]:
    initial = optimize(
        binary, "t11_comp3_n710", ALGORITHMS[0], 1, 1, 1, 11600
    )["run_receipts"][0]["final_layout"]
    child = [point[:] for point in initial]
    child[0][0] += 0.25

    serial_full = [
        fixed(binary, "t11_comp3_n710", child, 1, 10)
        for _ in range(observations)
    ]
    all_core_full = [
        fixed(binary, "t11_comp3_n710", child, workers, 10)
        for _ in range(observations)
    ]
    serial_full_seconds = statistics.median(
        item["seconds_per_evaluation"] for item in serial_full
    )
    all_core_full_seconds = statistics.median(
        item["seconds_per_evaluation"] for item in all_core_full
    )
    if any(
        item["evaluation"] != serial_full[0]["evaluation"]
        for item in all_core_full
    ):
        raise SystemExit("T11 H6 full-evaluator science drift")
    if any(
        item["observed_workers"] != workers for item in all_core_full
    ):
        raise SystemExit("T11 H6 full evaluator missed workers")

    incremental = [
        fixed(
            binary, "t11_comp3_n710", initial, 1, 50, child=child
        )
        for _ in range(observations)
    ]
    incremental_seconds = statistics.median(
        item["seconds_per_evaluation"] for item in incremental
    )
    if any(
        item["evaluation"] != serial_full[0]["evaluation"]
        for item in incremental
    ):
        raise SystemExit("T11 H6 incremental science drift")

    serial_trajectory = optimize(
        binary, "t11_comp3_n710", ALGORITHMS[0], 1, 1, 500, 11610
    )
    all_core_trajectory = optimize(
        binary, "t11_comp3_n710", ALGORITHMS[0], workers, 1, 500, 11610
    )
    exact_campaign_identity(serial_trajectory, all_core_trajectory)
    serial_run = serial_trajectory["run_receipts"][0]
    all_core_run = all_core_trajectory["run_receipts"][0]
    if all_core_run["observed_workers"] != workers:
        raise SystemExit("T11 H6 trajectory missed workers")

    sequential_campaign = optimize(
        binary, "t11_comp3_n710", ALGORITHMS[0],
        1, workers, 300, 11620,
    )
    start = time.perf_counter()
    all_core_campaign = optimize(
        binary, "t11_comp3_n710", ALGORITHMS[0],
        workers, workers, 300, 11620,
    )
    measured_all_core_wall = time.perf_counter() - start
    exact_campaign_identity(sequential_campaign, all_core_campaign)
    if all_core_campaign["campaign_observed_workers"] != workers:
        raise SystemExit("T11 H6 run campaign missed workers")

    full_speedup = serial_full_seconds / max(all_core_full_seconds, 1e-15)
    incremental_speedup = (
        serial_full_seconds / max(incremental_seconds, 1e-15)
    )
    trajectory_speedup = (
        serial_run["end_to_end_seconds"]
        / max(all_core_run["end_to_end_seconds"], 1e-15)
    )
    campaign_speedup = (
        sequential_campaign["campaign_seconds"]
        / max(measured_all_core_wall, 1e-15)
    )
    if min(
        full_speedup,
        incremental_speedup,
        trajectory_speedup,
        campaign_speedup,
    ) <= 1.0:
        raise SystemExit(
            "T11 H6 admission failed: "
            f"full={full_speedup}, incremental={incremental_speedup}, "
            f"trajectory={trajectory_speedup}, campaign={campaign_speedup}"
        )
    return {
        "status": "pass",
        "selected_workers": workers,
        "observations": observations,
        "full_layout_evaluator": {
            "one_worker_seconds": serial_full_seconds,
            "all_core_seconds": all_core_full_seconds,
            "all_core_speedup": full_speedup,
            "science_identity": "exact",
        },
        "blockcopy_incremental_evaluator": {
            "one_worker_seconds_per_physical_fes": incremental_seconds,
            "speedup_over_one_worker_full_recompute": incremental_speedup,
            "complexity_change": "O(S*N*N) to O(S*N*K)",
            "science_identity": "exact",
        },
        "single_trajectory": {
            "physical_fes": 500,
            "one_worker_seconds": serial_run["end_to_end_seconds"],
            "all_core_seconds": all_core_run["end_to_end_seconds"],
            "all_core_speedup": trajectory_speedup,
            "all_core_observed_workers": all_core_run["observed_workers"],
            "science_identity": "exact",
        },
        "independent_run_campaign": {
            "runs": workers,
            "physical_fes_per_run": 300,
            "one_worker_campaign_seconds":
                sequential_campaign["campaign_seconds"],
            "all_core_measured_wall_seconds": measured_all_core_wall,
            "all_core_speedup": campaign_speedup,
            "all_core_observed_workers":
                all_core_campaign["campaign_observed_workers"],
            "science_identity": "exact",
        },
    }


def validate_formal(
    campaign: dict[str, Any],
    problem: str,
    algorithm: str,
    repeats: int,
    fes: int,
    workers: int,
) -> dict[str, Any]:
    runs = campaign["run_receipts"]
    if (
        campaign["runs"] != repeats
        or len(runs) != repeats
        or campaign["campaign_observed_workers"] != min(workers, repeats)
    ):
        raise SystemExit(f"T11 formal topology drift {problem}/{algorithm}")
    for index, run in enumerate(runs):
        if (
            run["problem_id"] != problem
            or run["algorithm_id"] != algorithm
            or run["physical_fes"] != fes
            or not run["final_evaluation"]["feasible"]
            or len(run["final_layout"]) != TURBINES[problem]
        ):
            raise SystemExit(
                f"T11 formal quality drift {problem}/{algorithm}/{index}"
            )
    costs = [run["final_evaluation"]["energy_cost"] for run in runs]
    return {
        "problem_id": problem,
        "algorithm_id": algorithm,
        "runs": repeats,
        "physical_fes_per_run": fes,
        "total_physical_fes": repeats * fes,
        "minimum_energy_cost": min(costs),
        "median_energy_cost": statistics.median(costs),
        "maximum_energy_cost": max(costs),
        "mean_energy_cost": statistics.fmean(costs),
        "standard_deviation_energy_cost":
            statistics.stdev(costs) if len(costs) > 1 else 0.0,
        "campaign_seconds": campaign["campaign_seconds"],
        "campaign_observed_workers":
            campaign["campaign_observed_workers"],
        "status": "pass",
    }


def formal(
    binary: Path,
    output_root: Path,
    workers: int,
    repeats: int,
    fes: int,
) -> dict[str, Any]:
    combinations = []
    for problem_index, problem in enumerate(PROBLEMS):
        for algorithm_index, algorithm in enumerate(ALGORITHMS):
            seed = 120000 + 10000 * problem_index + 1000 * algorithm_index
            path = output_root / "raw" / f"{problem}__{algorithm}.json"
            campaign = optimize(
                binary, problem, algorithm, workers, repeats, fes, seed, path
            )
            combinations.append(validate_formal(
                campaign, problem, algorithm, repeats, fes, workers
            ))
    payload = {
        "schema_version": 1,
        "corpus_id": "T11",
        "status": "pass",
        "workers": workers,
        "problems": PROBLEMS,
        "algorithms": ALGORITHMS,
        "independent_runs_per_combination": repeats,
        "physical_fes_per_run": fes,
        "formal_runs": len(combinations) * repeats,
        "formal_physical_fes": len(combinations) * repeats * fes,
        "combinations": combinations,
    }
    write_json(output_root / "formal_summary.json", payload)
    return payload


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--output-root", type=Path, required=True)
    parser.add_argument("--workers", type=int, default=20)
    parser.add_argument("--observations", type=int, default=3)
    parser.add_argument("--repeats", type=int, default=30)
    parser.add_argument("--physical-fes", type=int, default=2000)
    parser.add_argument(
        "--mode", choices=["all", "h6", "formal"], default="all"
    )
    args = parser.parse_args()
    if (
        args.workers <= 0 or args.observations <= 0
        or args.repeats <= 0 or args.physical_fes < 5
    ):
        raise SystemExit("invalid T11 H6/formal configuration")
    args.output_root.mkdir(parents=True, exist_ok=True)
    manifest: dict[str, Any] = {
        "schema_version": 1,
        "corpus_id": "T11",
        "mode": args.mode,
        "workers": args.workers,
    }
    if args.mode in {"all", "h6"}:
        manifest["h6"] = h6(
            args.binary, args.workers, args.observations
        )
        write_json(args.output_root / "h6_performance.json", manifest["h6"])
    if args.mode in {"all", "formal"}:
        manifest["formal"] = formal(
            args.binary,
            args.output_root,
            args.workers,
            args.repeats,
            args.physical_fes,
        )
    manifest["status"] = "pass"
    write_json(args.output_root / "manifest.json", manifest)


if __name__ == "__main__":
    main()
