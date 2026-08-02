#!/usr/bin/env python3
"""
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T60 H6, formal 840-run and paper-probe campaign
Paper/DOI: Solving the Wind Farm Layout Optimization Problem Using Random
Search Algorithm; 10.1016/j.renene.2015.01.005
Public source, missing/conflicting fields and completion policy:
hpc/core99_cpp/include/core99/feng_t60.hpp
Formal protocol: three ideal cases by two starts by 100 runs plus three Horns
Rev cases by two starts by 40 runs, each at 100000 complete-layout physical
FES; 600+240=840 target-method runs. Direction-preprocessing and robustness
probes are additional paper analyses, not target-run-count substitutions.
HPC protocol: compare O(S*N*N) full evaluation, O(S*N) incremental candidate
evaluation, all-core fixed-layout evaluation and one-versus-all-core
independent-run scheduling, with exact science-hash identity where applicable.
Method/problem semantic IDs: t60_improved_rs_incremental_v1;
t60_ideal_continuous_jensen_v1; t60_hornsrev_jensen_v80_v1
Controlling contract: shared/contracts/core99_t60_feng_shen_2015.json
Claim boundary: source-backed flexible academic reproduction, not author
Fortran, random-state, unavailable-array or exact-number replay
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import platform
import statistics
import subprocess
import time
from pathlib import Path
from typing import Any


IDEAL = ["t60_ideal_case1", "t60_ideal_case2", "t60_ideal_case3"]
HORNS = ["t60_horns_case1", "t60_horns_case2", "t60_horns_case3"]
INITIALS = ["paper", "random"]


def run_json(command: list[str]) -> dict[str, Any]:
    return json.loads(subprocess.run(
        command, check=True, capture_output=True, text=True
    ).stdout)


def write_json(path: Path, payload: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(
        json.dumps(payload, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    temporary.replace(path)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        while block := stream.read(1024 * 1024):
            digest.update(block)
    return digest.hexdigest()


def layout_csv(layout: list[list[float]]) -> str:
    return ",".join(str(value) for point in layout for value in point)


def optimize(
    binary: Path,
    problem: str,
    workers: int,
    runs: int,
    physical_fes: int,
    initial: str,
    seed: int,
    direction_sectors: int = 360,
) -> dict[str, Any]:
    return run_json([
        str(binary), "--problem", problem,
        "--direction-sectors", str(direction_sectors),
        "--workers", str(workers),
        "--runs", str(runs),
        "--physical-fes", str(physical_fes),
        "--initial", initial,
        "--seed", str(seed),
    ])


def fixed(
    binary: Path,
    problem: str,
    layout: list[list[float]],
    workers: int,
    direction_sectors: int = 360,
    repeats: int = 1,
    rotation: float = 0.0,
    scale: float = 1.0,
    shape: float = 1.0,
) -> dict[str, Any]:
    return run_json([
        str(binary), "--problem", problem,
        "--direction-sectors", str(direction_sectors),
        "--workers", str(workers),
        "--evaluation-repeats", str(repeats),
        "--direction-rotation-degrees", str(rotation),
        "--weibull-scale-multiplier", str(scale),
        "--weibull-shape-multiplier", str(shape),
        "--layout-csv", layout_csv(layout),
    ])


def exact_campaign_identity(
    left: dict[str, Any],
    right: dict[str, Any],
) -> None:
    if len(left["run_receipts"]) != len(right["run_receipts"]):
        raise SystemExit("T60 H6 campaign cardinality drift")
    fields = [
        "problem_id", "problem_semantic_id", "method_semantic_id", "seed",
        "physical_fes", "random_initial_layout", "feasible_proposals",
        "rejected_infeasible_proposals", "accepted_moves", "initial_power_kw",
        "final_evaluation", "scientific_hash", "final_layout",
    ]
    for index, (expected, observed) in enumerate(zip(
        left["run_receipts"], right["run_receipts"]
    )):
        for field in fields:
            if expected[field] != observed[field]:
                raise SystemExit(
                    f"T60 H6 science drift run={index} field={field}"
                )


def h6(
    binary: Path,
    workers: int,
    observations: int,
) -> dict[str, Any]:
    initial = optimize(
        binary, "t60_horns_case1", 1, 1, 1, "paper", 6000
    )["run_receipts"][0]["final_layout"]
    serial_full = [
        fixed(
            binary, "t60_horns_case1", initial, 1,
            direction_sectors=360, repeats=10,
        )
        for _ in range(observations)
    ]
    all_core_full = [
        fixed(
            binary, "t60_horns_case1", initial, workers,
            direction_sectors=360, repeats=10,
        )
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
        raise SystemExit("T60 all-core full evaluator changed science")
    if any(
        item["observed_workers"] != workers for item in all_core_full
    ):
        raise SystemExit("T60 all-core fixed evaluator missed CPU workers")

    incremental_receipts = [
        optimize(
            binary, "t60_horns_case1", 1, 1, 2000, "paper", 6015
        )
        for _ in range(observations)
    ]
    incremental_seconds = statistics.median(
        item["run_receipts"][0]["evaluator_seconds"]
        / item["run_receipts"][0]["physical_fes"]
        for item in incremental_receipts
    )

    sequential_campaign = optimize(
        binary, "t60_horns_case1", 1, workers, 1000, "paper", 6020
    )
    start = time.perf_counter()
    all_core_campaign = optimize(
        binary, "t60_horns_case1", workers, workers, 1000, "paper", 6020
    )
    measured_all_core_wall = time.perf_counter() - start
    exact_campaign_identity(sequential_campaign, all_core_campaign)
    if all_core_campaign["campaign_observed_workers"] != workers:
        raise SystemExit("T60 H6 campaign did not consume all workers")
    sequential_sum = sum(
        run["end_to_end_seconds"]
        for run in sequential_campaign["run_receipts"]
    )
    all_core_max = max(
        run["end_to_end_seconds"]
        for run in all_core_campaign["run_receipts"]
    )
    campaign_speedup = sequential_sum / max(measured_all_core_wall, 1e-15)
    if serial_full_seconds <= incremental_seconds:
        raise SystemExit("T60 incremental evaluator did not beat full evaluator")
    if campaign_speedup <= 1.0:
        raise SystemExit("T60 all-core campaign did not beat sequential work")
    return {
        "status": "pass",
        "selected_workers": workers,
        "observations": observations,
        "full_layout_evaluator": {
            "one_worker_seconds": serial_full_seconds,
            "all_core_seconds": all_core_full_seconds,
            "all_core_speedup":
                serial_full_seconds / max(all_core_full_seconds, 1e-15),
            "all_core_observed_workers": workers,
            "science_identity": "exact",
        },
        "paper_incremental_evaluator": {
            "one_worker_seconds_per_physical_fes": incremental_seconds,
            "speedup_over_one_worker_full_recompute":
                serial_full_seconds / max(incremental_seconds, 1e-15),
            "complexity_change": "O(S*N*N) to O(S*N)",
        },
        "independent_run_campaign": {
            "runs": workers,
            "physical_fes_per_run": 1000,
            "sequential_sum_run_seconds": sequential_sum,
            "all_core_max_run_seconds": all_core_max,
            "all_core_observed_wall_seconds": measured_all_core_wall,
            "aggregate_speedup": campaign_speedup,
            "all_core_observed_workers": workers,
            "science_identity": "exact",
        },
    }


def validate_campaign(
    campaign: dict[str, Any],
    problem: str,
    initial: str,
    runs: int,
    physical_fes: int,
    workers: int,
) -> dict[str, Any]:
    receipts = campaign["run_receipts"]
    if (
        campaign["runs"] != runs
        or len(receipts) != runs
        or campaign["campaign_observed_workers"] != min(workers, runs)
    ):
        raise SystemExit(f"T60 campaign topology drift: {problem}/{initial}")
    for index, run in enumerate(receipts):
        if (
            run["problem_id"] != problem
            or run["physical_fes"] != physical_fes
            or run["random_initial_layout"] != (initial == "random")
            or not run["final_evaluation"]["feasible"]
            or run["final_evaluation"]["expected_power_kw"]
                < run["initial_power_kw"]
            or run["feasible_proposals"] != physical_fes - 1
            or run["accepted_moves"] < 0
        ):
            raise SystemExit(
                f"T60 formal quality drift {problem}/{initial}/run={index}"
            )
    powers = sorted(
        run["final_evaluation"]["expected_power_kw"] for run in receipts
    )
    return {
        "problem_id": problem,
        "initial": initial,
        "runs": runs,
        "physical_fes_per_run": physical_fes,
        "total_physical_fes": runs * physical_fes,
        "minimum_power_kw": powers[0],
        "median_power_kw": statistics.median(powers),
        "maximum_power_kw": powers[-1],
        "mean_power_kw": statistics.fmean(powers),
        "standard_deviation_power_kw":
            statistics.stdev(powers) if len(powers) > 1 else 0.0,
        "total_accepted_moves": sum(
            run["accepted_moves"] for run in receipts
        ),
        "total_rejected_infeasible_proposals": sum(
            run["rejected_infeasible_proposals"] for run in receipts
        ),
        "campaign_observed_workers":
            campaign["campaign_observed_workers"],
        "status": "pass",
    }


def preprocessing_probe(
    binary: Path,
    output_root: Path,
    workers: int,
    physical_fes: int,
) -> dict[str, Any]:
    optimized = {}
    for sectors in [12, 72, 360]:
        receipt = optimize(
            binary, "t60_horns_case1", workers, 1, physical_fes,
            "paper", 6060 + sectors, direction_sectors=sectors,
        )
        optimized[str(sectors)] = receipt["run_receipts"][0]
    matrix = {}
    for optimized_sectors, run in optimized.items():
        matrix[optimized_sectors] = {}
        for evaluation_sectors in [12, 72, 360]:
            value = fixed(
                binary, "t60_horns_case1", run["final_layout"], workers,
                direction_sectors=evaluation_sectors,
            )
            matrix[optimized_sectors][str(evaluation_sectors)] = (
                value["evaluation"]["expected_power_kw"]
            )
    payload = {
        "status": "pass",
        "optimization_physical_fes": physical_fes,
        "optimized_layout_hashes": {
            sectors: run["scientific_hash"]
            for sectors, run in optimized.items()
        },
        "power_kw_by_optimization_and_evaluation_sector_count": matrix,
    }
    write_json(output_root / "direction_preprocessing.json", payload)
    return payload


def robustness_probe(
    binary: Path,
    output_root: Path,
    workers: int,
    best_case1: dict[str, dict[str, Any]],
) -> dict[str, Any]:
    scenarios = []
    original_layout = optimize(
        binary, "t60_horns_case1", 1, 1, 1, "paper", 6000
    )["run_receipts"][0]["final_layout"]
    dimensions = [
        ("direction_rotation_degrees", [-20,-10,0,10,20]),
        ("weibull_scale_multiplier", [.8,.9,1,1.1,1.2]),
        ("weibull_shape_multiplier", [.8,.9,1,1.1,1.2]),
    ]
    for initial, run in best_case1.items():
        for dimension, values in dimensions:
            for value in values:
                options = {
                    "rotation": 0.0, "scale": 1.0, "shape": 1.0
                }
                if dimension == "direction_rotation_degrees":
                    options["rotation"] = value
                elif dimension == "weibull_scale_multiplier":
                    options["scale"] = value
                else:
                    options["shape"] = value
                receipt = fixed(
                    binary, "t60_horns_case1", run["final_layout"], workers,
                    rotation=options["rotation"],
                    scale=options["scale"],
                    shape=options["shape"],
                )
                original = fixed(
                    binary, "t60_horns_case1",
                    original_layout,
                    workers,
                    rotation=options["rotation"],
                    scale=options["scale"],
                    shape=options["shape"],
                )
                power = receipt["evaluation"]["expected_power_kw"]
                baseline = original["evaluation"]["expected_power_kw"]
                scenarios.append({
                    "initial": initial,
                    "dimension": dimension,
                    "value": value,
                    "optimized_power_kw": power,
                    "original_power_kw": baseline,
                    "relative_improvement": power / baseline - 1.0,
                })
    payload = {
        "status": "pass",
        "scenario_count": len(scenarios),
        "scenarios": scenarios,
    }
    write_json(output_root / "robustness.json", payload)
    return payload


def formal(
    binary: Path,
    contract: Path,
    output_root: Path,
    workers: int,
    physical_fes: int,
    probe_physical_fes: int,
) -> dict[str, Any]:
    raw_root = output_root / "formal_raw"
    summaries = []
    best_case1: dict[str, dict[str, Any]] = {}
    total_runs = 0
    for problem in IDEAL + HORNS:
        repeats = 100 if problem in IDEAL else 40
        for initial in INITIALS:
            path = raw_root / f"{problem}_{initial}.json"
            if path.exists():
                campaign = json.loads(path.read_text(encoding="utf-8"))
            else:
                campaign = optimize(
                    binary, problem, workers, repeats, physical_fes,
                    initial, 20260731,
                )
                write_json(path, campaign)
            summaries.append(validate_campaign(
                campaign, problem, initial, repeats, physical_fes, workers
            ))
            total_runs += repeats
            if problem == "t60_horns_case1":
                best_case1[initial] = max(
                    campaign["run_receipts"],
                    key=lambda run:
                        run["final_evaluation"]["expected_power_kw"],
                )
    if total_runs != 840:
        raise SystemExit("T60 formal target-run count drift")

    preprocessing = preprocessing_probe(
        binary, output_root, workers, probe_physical_fes
    )
    robustness = robustness_probe(
        binary, output_root, workers, best_case1
    )
    payload = {
        "schema_version": 1,
        "corpus_id": "T60",
        "status": "pass",
        "target_method_runs": total_runs,
        "physical_fes_per_run": physical_fes,
        "total_target_method_physical_fes": total_runs * physical_fes,
        "selected_workers": workers,
        "campaigns": summaries,
        "direction_preprocessing_status": preprocessing["status"],
        "robustness_scenario_count": robustness["scenario_count"],
        "binary": str(binary.resolve()),
        "binary_sha256": sha256(binary),
        "contract": str(contract.resolve()),
        "contract_sha256": sha256(contract),
        "host": platform.node(),
        "platform": platform.platform(),
        "logical_cpu_count": os.cpu_count(),
        "claim_boundary": (
            "source-backed flexible academic reproduction of every target "
            "problem, Algorithm-1 lifecycle and paper repeat matrix; not "
            "author source, random state or exact numerical replay"
        ),
    }
    write_json(output_root / "formal_summary.json", payload)
    return payload


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument(
        "--contract",
        type=Path,
        default=Path("shared/contracts/core99_t60_feng_shen_2015.json"),
    )
    parser.add_argument("--output-root", type=Path, required=True)
    parser.add_argument("--workers", type=int, default=20)
    parser.add_argument("--physical-fes", type=int, default=100000)
    parser.add_argument("--probe-physical-fes", type=int, default=100000)
    parser.add_argument("--observations", type=int, default=5)
    parser.add_argument(
        "--mode", choices=["h6", "formal", "all"], default="all"
    )
    args = parser.parse_args()
    if args.workers <= 0 or args.physical_fes <= 0:
        raise SystemExit("invalid T60 campaign configuration")
    args.output_root.mkdir(parents=True, exist_ok=True)
    if args.mode in {"h6", "all"}:
        write_json(
            args.output_root / "h6.json",
            h6(args.binary, args.workers, args.observations),
        )
    if args.mode in {"formal", "all"}:
        formal(
            args.binary,
            args.contract,
            args.output_root,
            args.workers,
            args.physical_fes,
            args.probe_physical_fes,
        )


if __name__ == "__main__":
    main()
