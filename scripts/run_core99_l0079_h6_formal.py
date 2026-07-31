#!/usr/bin/env python3
"""
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: resumable L0079 Waffle H5/H6 and paper-native campaign
Paper/DOI: Pillai et al.; 10.1016/j.oceaneng.2017.04.049
Formal work: adaptive GA and gBest PSO, each with array, journal-628 binary,
and continuous constraints; population/swarm 100; maximum 1000 generations;
paper stopping criteria; one run per role as explicitly stated in the thesis.
HPC work: one/all-worker fixed-work H6 for all six roles.
Facts and claim boundary: hpc/core99_cpp/include/core99/pillai_l0079.hpp
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


ROLES = (
    ("ga", "array"),
    ("ga", "binary"),
    ("ga", "continuous"),
    ("pso", "array"),
    ("pso", "binary"),
    ("pso", "continuous"),
)
PROBLEM = "l0079_middelgrunden_lcoe_three_constraint_declared_v1"
PROTOCOL = "l0079_native_six_role_single_run_v1"


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
    *, binary: Path, output: Path, optimizer: str, mode: str,
    workers: int, seed: int, population: int, maximum_generations: int,
    convergence: bool, source_commit: str, timeout_seconds: float,
) -> dict[str, Any]:
    expected = {
        "source_commit": source_commit,
        "optimizer": "adaptive_ga" if optimizer == "ga" else "gbest_pso",
        "constraint_mode": mode,
        "candidate_profile": "journal_628",
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
        str(binary), "--action", "optimize", "--optimizer", optimizer,
        "--constraint-mode", mode, "--candidate-profile", "journal_628",
        "--workers", str(workers), "--seed", str(seed),
        "--population", str(population),
        "--maximum-generations", str(maximum_generations),
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


def validate(
    payload: dict[str, Any], optimizer: str, mode: str,
    workers: int, population: int,
) -> None:
    expected_method = (
        "l0079_adaptive_ga_three_encoding_declared_v1"
        if optimizer == "ga"
        else "l0079_gbest_pso_three_encoding_declared_v1"
    )
    require(payload["method_semantic_id"] == expected_method, "method identity")
    require(payload["problem_semantic_id"] == PROBLEM, "problem identity")
    require(payload["protocol_semantic_id"] == PROTOCOL, "protocol identity")
    require(payload["optimizer"] == (
        "adaptive_ga" if optimizer == "ga" else "gbest_pso"
    ), "optimizer identity")
    require(payload["constraint_mode"] == mode, "mode identity")
    require(payload["candidate_profile"] == "journal_628", "target profile")
    require(payload["requested_workers"] == workers, "workers")
    require(payload["population"] == population, "population")
    require(payload["physical_fes"] >= population, "physical FES")
    require(len(payload["best_layout"]) == 20, "twenty turbines")
    require(payload["reference_evaluation"]["feasible"] is True, "reference")
    require(payload["best_evaluation"]["feasible"] is True, "best feasible")
    require(payload["best_evaluation"]["minimum_spacing_m"] >= 175.0 - 1e-8,
            "minimum spacing")
    require(payload["parallel_regions"] > 0, "parallel work")
    if workers > 1:
        require(payload["observed_workers"] >= 2, "worker participation")


def run_h6(arguments: argparse.Namespace, root: Path) -> dict[str, Any]:
    rows: list[dict[str, Any]] = []
    for role_index, (optimizer, mode) in enumerate(ROLES):
        pair = {}
        for workers in (1, arguments.total_workers):
            payload = execute(
                binary=arguments.binary,
                output=root / "h6" / f"{optimizer}-{mode}-w{workers:02d}.json",
                optimizer=optimizer,
                mode=mode,
                workers=workers,
                seed=arguments.seed_base - 100 + role_index,
                population=arguments.population,
                maximum_generations=arguments.h6_generations,
                convergence=False,
                source_commit=arguments.source_commit,
                timeout_seconds=1800.0,
            )
            validate(payload, optimizer, mode, workers, arguments.population)
            pair[workers] = payload
        serial = pair[1]
        parallel = pair[arguments.total_workers]
        for key in (
            "generations", "physical_fes", "convergence_reason",
            "reference_evaluation", "best_evaluation", "best_layout",
            "scientific_hash",
        ):
            require(serial[key] == parallel[key],
                    f"{optimizer}/{mode} differs: {key}")
        speedup = {
            "evaluator": serial["evaluator_seconds"]
                / parallel["evaluator_seconds"],
            "end_to_end": serial["end_to_end_seconds"]
                / parallel["end_to_end_seconds"],
        }
        require(speedup["evaluator"] > 1.0, f"{optimizer}/{mode} evaluator")
        require(speedup["end_to_end"] > 1.0, f"{optimizer}/{mode} end-to-end")
        rows.append({
            "optimizer": optimizer,
            "constraint_mode": mode,
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
        "role_count": 6,
        "rows": rows,
    }
    write_json(root / "h6" / "summary.json", summary)
    return summary


def run_formal(arguments: argparse.Namespace, root: Path) -> dict[str, Any]:
    rows = []
    for role_index, (optimizer, mode) in enumerate(ROLES):
        payload = execute(
            binary=arguments.binary,
            output=root / "formal" / f"{optimizer}-{mode}.json",
            optimizer=optimizer,
            mode=mode,
            workers=arguments.total_workers,
            seed=arguments.seed_base + role_index,
            population=arguments.population,
            maximum_generations=arguments.formal_maximum_generations,
            convergence=True,
            source_commit=arguments.source_commit,
            timeout_seconds=14400.0,
        )
        validate(payload, optimizer, mode,
                 arguments.total_workers, arguments.population)
        rows.append({
            "optimizer": optimizer,
            "constraint_mode": mode,
            "seed": payload["seed"],
            "population": payload["population"],
            "generations": payload["generations"],
            "physical_fes": payload["physical_fes"],
            "convergence_reason": payload["convergence_reason"],
            "best_lcoe_gbp_per_mwh":
                payload["best_evaluation"]["lcoe_gbp_per_mwh"],
            "best_aep_mwh_8766":
                payload["best_evaluation"]["net_aep_mwh_8766"],
            "best_lifetime_cost_gbp":
                payload["best_evaluation"]["lifetime_cost_gbp"],
            "end_to_end_seconds": payload["end_to_end_seconds"],
            "scientific_hash": payload["scientific_hash"],
        })
    require(len(rows) == 6, "six formal native roles")
    summary = {
        "status": "pass",
        "source_commit": arguments.source_commit,
        "paper_native_case_count": 6,
        "native_repeats": 1,
        "population_or_swarm": arguments.population,
        "maximum_generations": arguments.formal_maximum_generations,
        "workers_per_case": arguments.total_workers,
        "rows": rows,
        "claim_boundary": (
            "source-backed flexible academic reproduction; single run per "
            "paper-native optimizer/mode role; not author numeric replay"
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
    parser.add_argument("--seed-base", type=int, default=790100)
    parser.add_argument("--population", type=int, default=100)
    parser.add_argument("--h6-generations", type=int, default=20)
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
        "h6_roles": len(h6["rows"]),
        "formal_cases": len(formal["rows"]),
        "output_root": str(root),
    }, sort_keys=True))


if __name__ == "__main__":
    main()
