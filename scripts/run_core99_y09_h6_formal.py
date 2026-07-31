#!/usr/bin/env python3
"""
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: resumable Y09 Waffle H6 and paper-native campaign
Paper/DOI: Li et al.; 10.1016/j.renene.2025.124386
Formal work: all twelve unique paper-native composition, direction, fatigue
threshold and cost-ratio cases with first-party patent GA parameters.
HPC work: one/all-worker fixed-work comparison for every case; formal work is
all-core only. Missing repeat count is completed as one run per unique case.
Facts, conflicts, completion and claim boundary:
hpc/core99_cpp/include/core99/li_y09.hpp
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import time
from typing import Any


CASES = (
    "Y09_west_five_only",
    "Y09_west_multi",
    "Y09_west_fifteen_only",
    "Y09_northwest_multi",
    "Y09_southwest_multi",
    "Y09_fatigue_008_multi",
    "Y09_fatigue_012_multi",
    "Y09_fatigue_016_multi",
    "Y09_cost_020_multi",
    "Y09_cost_030_multi",
    "Y09_cost_040_multi",
    "Y09_cost_050_multi",
)
METHOD = "y09_ternary_variable_mutation_ga_declared_v1"
PROBLEM = "y09_multitype_mqi_fatigue_lcoe_declared_v1"
PROTOCOL = "y09_native_12case_single_run_declared_v1"


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


def write_json(path: Path, payload: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(
        json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    os.replace(temporary, path)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        while block := stream.read(1024 * 1024):
            digest.update(block)
    return digest.hexdigest()


def execute(
    *, binary: Path, output: Path, case: str, workers: int, seed: int,
    population: int, maximum_generations: int, convergence: bool,
    source_commit: str, timeout_seconds: float,
) -> dict[str, Any]:
    expected = {
        "source_commit": source_commit,
        "case_id": case,
        "requested_workers": workers,
        "seed": seed,
        "population": population,
        "configured_maximum_generations": maximum_generations,
        "convergence_enabled": convergence,
    }
    if output.exists():
        previous = json.loads(output.read_text(encoding="utf-8"))
        if all(previous.get(key) == value for key, value in expected.items()):
            return previous
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = output.with_suffix(".binary.tmp")
    command = [
        str(binary), "--case", case, "--workers", str(workers),
        "--seed", str(seed), "--population", str(population),
        "--maximum-generations", str(maximum_generations),
        "--crossover-rate", "0.08", "--mutation-rate", "0.01",
        "--output", str(temporary),
    ]
    if not convergence:
        command.append("--disable-convergence")
    started = time.monotonic()
    completed = subprocess.run(
        command, text=True, capture_output=True, timeout=timeout_seconds
    )
    if completed.returncode:
        raise RuntimeError(completed.stderr or completed.stdout)
    payload = json.loads(temporary.read_text(encoding="utf-8"))
    temporary.unlink()
    payload.update({
        "source_commit": source_commit,
        "binary_sha256": sha256(binary),
        "configured_maximum_generations": maximum_generations,
        "convergence_enabled": convergence,
        "runner_wall_seconds": time.monotonic() - started,
    })
    write_json(output, payload)
    return payload


def validate(payload: dict[str, Any], case: str, workers: int, population: int) -> None:
    require(payload["case_id"] == case, "case identity")
    require(payload["method_semantic_id"] == METHOD, "method identity")
    require(payload["problem_semantic_id"] == PROBLEM, "problem identity")
    require(payload["protocol_semantic_id"] == PROTOCOL, "protocol identity")
    require(payload["requested_workers"] == workers, "workers")
    require(payload["population"] == population, "population")
    require(payload["physical_fes"] >= population, "physical FES")
    require(len(payload["best_layout"]) == 100, "one-hundred-cell layout")
    require(payload["best_evaluation"]["feasible"] is True, "feasibility")
    require(payload["parallel_regions"] > 0, "parallel work")
    if workers > 1:
        require(payload["observed_workers"] >= 2, "worker participation")


def run_h6(arguments: argparse.Namespace, root: Path) -> dict[str, Any]:
    rows = []
    for case_index, case in enumerate(CASES):
        pair = {}
        for workers in (1, arguments.total_workers):
            payload = execute(
                binary=arguments.binary,
                output=root / "h6" / f"{case}-w{workers:02d}.json",
                case=case,
                workers=workers,
                seed=arguments.seed_base - 100 + case_index,
                population=arguments.population,
                maximum_generations=arguments.h6_generations,
                convergence=False,
                source_commit=arguments.source_commit,
                timeout_seconds=3600.0,
            )
            validate(payload, case, workers, arguments.population)
            pair[workers] = payload
        serial = pair[1]
        parallel = pair[arguments.total_workers]
        for key in (
            "generations", "physical_fes", "convergence_reason",
            "final_mutation_probabilities", "best_evaluation", "best_layout",
            "scientific_hash",
        ):
            require(serial[key] == parallel[key], f"{case} differs: {key}")
        speedup = {
            "evaluator": serial["evaluator_seconds"]
                / parallel["evaluator_seconds"],
            "end_to_end": serial["end_to_end_seconds"]
                / parallel["end_to_end_seconds"],
        }
        require(speedup["evaluator"] > 1.0, f"{case} evaluator speedup")
        require(speedup["end_to_end"] > 1.0, f"{case} end-to-end speedup")
        rows.append({
            "case_id": case,
            "fixed_population": arguments.population,
            "fixed_generations": arguments.h6_generations,
            "physical_fes": serial["physical_fes"],
            "one_worker": serial,
            "all_worker": parallel,
            "speedup": speedup,
            "same_layout_metrics_fes_and_hash": True,
        })
    summary = {
        "status": "pass",
        "source_commit": arguments.source_commit,
        "case_count": len(CASES),
        "rows": rows,
    }
    write_json(root / "h6" / "summary.json", summary)
    return summary


def run_formal(arguments: argparse.Namespace, root: Path) -> dict[str, Any]:
    rows = []
    for case_index, case in enumerate(CASES):
        payload = execute(
            binary=arguments.binary,
            output=root / "formal" / f"{case}.json",
            case=case,
            workers=arguments.total_workers,
            seed=arguments.seed_base + case_index,
            population=arguments.population,
            maximum_generations=arguments.formal_maximum_generations,
            convergence=False,
            source_commit=arguments.source_commit,
            timeout_seconds=14400.0,
        )
        validate(payload, case, arguments.total_workers, arguments.population)
        evaluation = payload["best_evaluation"]
        rows.append({
            "case_id": case,
            "seed": payload["seed"],
            "population": payload["population"],
            "generations": payload["generations"],
            "physical_fes": payload["physical_fes"],
            "convergence_reason": payload["convergence_reason"],
            "five_mw_turbines": evaluation["five_mw_turbines"],
            "fifteen_mw_turbines": evaluation["fifteen_mw_turbines"],
            "total_power_mw": evaluation["total_power_mw"],
            "lcoe_units_per_mw": evaluation["lcoe_units_per_mw"],
            "fatigue_standard_deviation":
                evaluation["fatigue_standard_deviation"],
            "average_maintenance_cost_units":
                evaluation["average_maintenance_cost_units"],
            "end_to_end_seconds": payload["end_to_end_seconds"],
            "scientific_hash": payload["scientific_hash"],
        })
    require(len(rows) == len(CASES), "complete twelve-case formal matrix")
    summary = {
        "status": "pass",
        "source_commit": arguments.source_commit,
        "paper_native_case_count": len(CASES),
        "native_repeats": 1,
        "population": arguments.population,
        "maximum_generations": arguments.formal_maximum_generations,
        "workers_per_case": arguments.total_workers,
        "rows": rows,
        "claim_boundary": (
            "source-backed flexible academic reproduction; one declared run "
            "per unique paper-native case because no repeat count is published; "
            "not author numeric replay"
        ),
    }
    write_json(root / "formal" / "summary.json", summary)
    return summary


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--output-root", type=Path, required=True)
    parser.add_argument("--source-commit", required=True)
    parser.add_argument("--total-workers", type=int, required=True)
    parser.add_argument("--seed-base", type=int, default=909100)
    parser.add_argument("--population", type=int, default=100)
    parser.add_argument("--h6-generations", type=int, default=50)
    parser.add_argument("--formal-maximum-generations", type=int, default=1000)
    args = parser.parse_args()
    args.binary = args.binary.resolve()
    root = args.output_root.resolve()
    root.mkdir(parents=True, exist_ok=True)
    h6 = run_h6(args, root)
    formal = run_formal(args, root)
    write_json(root / "campaign_summary.json", {
        "status": "pass",
        "h6_summary": str(root / "h6" / "summary.json"),
        "formal_summary": str(root / "formal" / "summary.json"),
        "minimum_h6_evaluator_speedup": min(
            row["speedup"]["evaluator"] for row in h6["rows"]
        ),
        "minimum_h6_end_to_end_speedup": min(
            row["speedup"]["end_to_end"] for row in h6["rows"]
        ),
        "formal_cases": len(formal["rows"]),
    })
    print(json.dumps({
        "status": "pass",
        "h6_cases": len(h6["rows"]),
        "formal_cases": len(formal["rows"]),
        "output_root": str(root),
    }, sort_keys=True))


if __name__ == "__main__":
    main()
