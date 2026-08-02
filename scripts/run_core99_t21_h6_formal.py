#!/usr/bin/env python3
"""
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T21 Waffle all-core H6 and 404-run formal runner
Paper/DOI: Topology Optimization of Wind Farm Layouts;
10.1016/j.renene.2022.06.019
Public problem source:
https://github.com/byuflowlab/iea37-wflo-casestudies revision
af88908d22795030ac2dfbe37bc38e912aee8ed6
Missing/conflicts/completion:
hpc/core99_cpp/include/core99/pollini_t21.hpp
Formal protocol: two paper problems, default plus 100 random starts, with
RAMP and the paper's q=0 interpolation ablation: 2*101*2=404 target runs
HPC protocol: fixed-density evaluator 1-worker/all-core comparison plus
identical 20-start single-worker trajectories executed sequentially or across
all cores; formal ensembles use the same hierarchical all-core scheduler
Claim boundary: academic declared reproduction, not author MATLAB wrapper,
random-state, internal-asymptote, or exact numerical replay
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import statistics
import subprocess
from pathlib import Path
from typing import Any


def run_json(command: list[str]) -> dict[str, Any]:
    completed = subprocess.run(
        command,
        check=True,
        text=True,
        capture_output=True,
    )
    return json.loads(completed.stdout)


def write_json(path: Path, payload: dict[str, Any]) -> None:
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


def fixed_receipt(
    binary: Path,
    case_name: str,
    density: float,
    workers: int,
    repeats: int,
) -> dict[str, Any]:
    return run_json(
        [
            str(binary),
            "--case", case_name,
            "--workers", str(workers),
            "--evaluate-density", str(density),
            "--evaluation-repeats", str(repeats),
        ]
    )


def evaluator_identity(
    reference: dict[str, Any],
    candidate: dict[str, Any],
) -> float:
    scalar_fields = [
        "potential_sites",
        "spacing_pairs",
        "aep_gwh",
        "objective",
        "minimum_count_constraint",
        "maximum_count_constraint",
        "maximum_spacing_constraint",
    ]
    maximum_error = 0.0
    for field in scalar_fields:
        left = float(reference[field])
        right = float(candidate[field])
        error = abs(left - right)
        maximum_error = max(maximum_error, error)
        if error > 1.0e-10 * max(1.0, abs(left), abs(right)):
            raise SystemExit(f"T21 H6 evaluator drift in {field}")
    left_gradient = reference["objective_gradient"]
    right_gradient = candidate["objective_gradient"]
    if len(left_gradient) != len(right_gradient):
        raise SystemExit("T21 H6 gradient cardinality drift")
    for left, right in zip(left_gradient, right_gradient):
        error = abs(left - right)
        maximum_error = max(maximum_error, error)
        if error > 1.0e-9 * max(1.0, abs(left), abs(right)):
            raise SystemExit("T21 H6 objective-gradient drift")
    return maximum_error


def campaign_receipt(
    binary: Path,
    workers: int,
    runs: int,
    maximum_evaluations: int,
) -> dict[str, Any]:
    return run_json(
        [
            str(binary),
            "--case", "small",
            "--workers", str(workers),
            "--runs", str(runs),
            "--maximum-evaluations", str(maximum_evaluations),
            "--seed", "20260731",
        ]
    )


def formal_summary(
    receipt: dict[str, Any],
    case_name: str,
) -> dict[str, Any]:
    runs = receipt["runs"]
    if len(runs) != 101:
        raise SystemExit(f"T21 {case_name} formal run cardinality drift")
    expected_sites = 124 if case_name == "small" else 709
    minimum_turbines = 16 if case_name == "small" else 64
    maximum_turbines = 64 if case_name == "small" else 256
    if sorted(run["start_index"] for run in runs) != list(range(101)):
        raise SystemExit(f"T21 {case_name} start-index drift")
    for run in runs:
        if (
            run["potential_sites"] != expected_sites
            or run["objective_evaluations"] <= 0
            or run["objective_evaluations"] > 1000
            or run["gradient_evaluations"] != run["objective_evaluations"]
            or not math.isfinite(run["relaxed_aep_gwh"])
            or not math.isfinite(run["discrete_aep_gwh"])
            or run["relaxed_constraint_violation"] > 1.0e-5
            or not minimum_turbines
                <= run["discrete_turbines"]
                <= maximum_turbines
        ):
            raise SystemExit(f"T21 {case_name} formal quality gate failed")
    values = [run["discrete_aep_gwh"] for run in runs]
    counts = [run["discrete_turbines"] for run in runs]
    return {
        "runs": len(runs),
        "best_discrete_aep_gwh": max(values),
        "mean_discrete_aep_gwh": statistics.mean(values),
        "median_discrete_aep_gwh": statistics.median(values),
        "minimum_discrete_turbines": min(counts),
        "maximum_discrete_turbines": max(counts),
        "maximum_relaxed_constraint_violation": max(
            run["relaxed_constraint_violation"] for run in runs
        ),
        "minimum_objective_evaluations": min(
            run["objective_evaluations"] for run in runs
        ),
        "maximum_objective_evaluations": max(
            run["objective_evaluations"] for run in runs
        ),
        "campaign_seconds": receipt["campaign_seconds"],
        "run_parallelism": receipt["run_parallelism"],
        "workers_per_run": receipt["workers_per_run"],
        "campaign_observed_workers":
            receipt["campaign_observed_workers"],
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--output-root", type=Path, required=True)
    parser.add_argument("--source-commit", required=True)
    parser.add_argument("--workers", type=int, default=20)
    parser.add_argument("--timing-observations", type=int, default=3)
    parser.add_argument("--run-formal", action="store_true")
    arguments = parser.parse_args()
    if arguments.workers <= 1 or arguments.timing_observations < 3:
        raise SystemExit("invalid T21 all-core contract")
    arguments.output_root.mkdir(parents=True, exist_ok=True)

    evaluator_h6: dict[str, Any] = {}
    for case_name, density, repeats in [
        ("small", 0.2, 500),
        ("large", 0.1805, 50),
    ]:
        one_worker = [
            fixed_receipt(
                arguments.binary, case_name, density, 1, repeats
            )
            for _ in range(arguments.timing_observations)
        ]
        all_core = [
            fixed_receipt(
                arguments.binary,
                case_name,
                density,
                arguments.workers,
                repeats,
            )
            for _ in range(arguments.timing_observations)
        ]
        maximum_error = 0.0
        for receipt in one_worker + all_core:
            maximum_error = max(
                maximum_error,
                evaluator_identity(one_worker[0], receipt),
            )
        if any(
            receipt["observed_workers"] != arguments.workers
            for receipt in all_core
        ):
            raise SystemExit(f"T21 {case_name} did not use all workers")
        one_seconds = statistics.median(
            receipt["seconds_per_evaluation"]
            for receipt in one_worker
        )
        all_seconds = statistics.median(
            receipt["seconds_per_evaluation"]
            for receipt in all_core
        )
        evaluator_h6[case_name] = {
            "density": density,
            "repeats_per_observation": repeats,
            "one_worker_median_seconds_per_evaluation": one_seconds,
            "all_core_median_seconds_per_evaluation": all_seconds,
            "speedup": one_seconds / all_seconds,
            "maximum_numerical_error": maximum_error,
            "all_core_observed_workers": arguments.workers,
            "status": "pass",
        }

    sequential = campaign_receipt(
        arguments.binary,
        1,
        20,
        100,
    )
    all_core_campaign = campaign_receipt(
        arguments.binary,
        arguments.workers,
        20,
        100,
    )
    sequential_hashes = [
        run["scientific_hash"] for run in sequential["runs"]
    ]
    all_core_hashes = [
        run["scientific_hash"] for run in all_core_campaign["runs"]
    ]
    if sequential_hashes != all_core_hashes:
        raise SystemExit("T21 multi-start scheduler changed science hashes")
    if (
        all_core_campaign["campaign_observed_workers"]
        != arguments.workers
        or all_core_campaign["run_parallelism"] != arguments.workers
        or all_core_campaign["workers_per_run"] != 1
    ):
        raise SystemExit("T21 multi-start all-core topology drift")

    manifest: dict[str, Any] = {
        "schema_version": 1,
        "campaign": "T21 all-core H6 and paper-native formal",
        "paper_doi": "10.1016/j.renene.2022.06.019",
        "method_semantic_id":
            "t21_ramp_mma_declared_reconstruction_v1",
        "problem_semantic_id":
            "t21_pollini_two_circle_density_wflop_v1",
        "source_commit": arguments.source_commit,
        "workers": arguments.workers,
        "evaluator_h6": evaluator_h6,
        "multi_start_h6": {
            "runs": 20,
            "maximum_objective_evaluations_per_run": 100,
            "one_worker_campaign_seconds":
                sequential["campaign_seconds"],
            "all_core_campaign_seconds":
                all_core_campaign["campaign_seconds"],
            "speedup": (
                sequential["campaign_seconds"]
                / all_core_campaign["campaign_seconds"]
            ),
            "all_core_observed_workers":
                all_core_campaign["campaign_observed_workers"],
            "scientific_identity": "exact_science_hashes",
            "status": "pass",
        },
        "formal": {
            "paper_required_target_runs": 404,
            "completed_target_runs": 0,
            "status": "not_requested",
        },
        "status": "h6_pass_formal_not_requested",
    }
    write_json(arguments.output_root / "manifest.json", manifest)

    if arguments.run_formal:
        completed = 0
        formal_receipts: dict[str, Any] = {}
        for case_name in ["small", "large"]:
            for interpolation in ["ramp", "linear"]:
                output = (
                    arguments.output_root
                    / f"formal-{case_name}-{interpolation}.json"
                )
                if not output.exists():
                    command = [
                        str(arguments.binary),
                        "--case", case_name,
                        "--workers", str(arguments.workers),
                        "--runs", "101",
                        "--maximum-evaluations", "1000",
                        "--seed", "20260731",
                        "--output", str(output),
                    ]
                    if interpolation == "linear":
                        command.append("--linear-interpolation")
                    subprocess.run(command, check=True)
                receipt = json.loads(output.read_text(encoding="utf-8"))
                key = f"{case_name}_{interpolation}"
                summary = formal_summary(receipt, case_name)
                summary["receipt"] = str(output)
                summary["sha256"] = file_sha256(output)
                formal_receipts[key] = summary
                completed += summary["runs"]
                manifest["formal"] = {
                    "paper_required_target_runs": 404,
                    "completed_target_runs": completed,
                    "receipts": formal_receipts,
                    "status": "running",
                }
                write_json(arguments.output_root / "manifest.json", manifest)
                print(
                    "t21_formal"
                    f" completed={completed}/404"
                    f" case={case_name}"
                    f" interpolation={interpolation}"
                    f" seconds={summary['campaign_seconds']:.6f}",
                    flush=True,
                )
        manifest["formal"]["status"] = "pass"
        manifest["status"] = "pass"
        write_json(arguments.output_root / "manifest.json", manifest)

    print(
        "t21_h6_formal"
        f" status={manifest['status']}"
        f" multi_start_speedup="
        f"{manifest['multi_start_h6']['speedup']:.6f}"
        f" formal={manifest['formal']['status']}",
        flush=True,
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
