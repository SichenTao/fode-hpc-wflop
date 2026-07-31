#!/usr/bin/env python3
"""
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: resumable T69 Waffle H5/H6 and paper-native campaign
Paper/DOI: Feng and Shen; 10.1016/j.enconman.2017.06.005
Formal work: five alpha, five beta, five Table-3-compatible gamma cases and
five equation-declared gamma conflict cases; 10000 complete feasible layout
evaluations and one fixed 1000-distribution bank per case; one run because the
paper reports no independent repeat count.
HPC work: one/all-worker fixed-work H6 for short, long and overall objectives.
Facts and claim boundary: hpc/core99_cpp/include/core99/feng_t69.hpp
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


WEIGHTS = (0.0, 0.05, 0.5, 0.95, 1.0)
METHOD = "t69_random_position_rs_v1"
PROBLEM = "t69_horns_changing_wind_robustness_declared_v1"
PROTOCOL = "t69_native_15case_10000fes_v1"


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
    *, binary: Path, output: Path, study: str, profile: str, weight: float,
    workers: int, seed: int, scenario_seed: int, scenarios: int, fes: int,
    source_commit: str,
) -> dict[str, Any]:
    if output.exists():
        previous = json.loads(output.read_text(encoding="utf-8"))
        if all((
            previous.get("source_commit") == source_commit,
            previous.get("requested_workers") == workers,
            previous.get("frozen_scenario_seed") == scenario_seed,
            previous.get("frozen_scenarios") == scenarios,
            previous.get("physical_fes") == fes,
        )):
            return previous
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = output.with_suffix(".binary.tmp")
    command = [
        str(binary), "--mode", "optimize", "--study", study,
        "--profile", profile, "--weight", str(weight), "--workers", str(workers),
        "--seed", str(seed), "--scenario-seed", str(scenario_seed),
        "--scenarios", str(scenarios), "--fes", str(fes),
        "--output", str(temporary),
    ]
    started = time.monotonic()
    completed = subprocess.run(
        command, text=True, capture_output=True, timeout=3600.0
    )
    if completed.returncode:
        raise RuntimeError(completed.stderr or completed.stdout)
    payload = json.loads(temporary.read_text(encoding="utf-8"))
    temporary.unlink()
    payload.update({
        "source_commit": source_commit,
        "binary_sha256": sha256(binary),
        "frozen_scenario_seed": scenario_seed,
        "frozen_scenarios": scenarios,
        "runner_wall_seconds": time.monotonic() - started,
    })
    write_json(output, payload)
    return payload


def validate(payload: dict[str, Any], study: str, profile: str,
             weight: float, workers: int, fes: int) -> None:
    require(payload["method_semantic_id"] == METHOD, "method identity")
    require(payload["problem_semantic_id"] == PROBLEM, "problem identity")
    require(payload["protocol_semantic_id"] == PROTOCOL, "protocol identity")
    require(payload["study"] == study, "study identity")
    require(payload["conflict_profile"] == profile, "profile identity")
    require(abs(payload["weight"] - weight) <= 1e-15, "weight identity")
    require(payload["requested_workers"] == workers, "worker request")
    require(payload["physical_fes"] == fes, "physical FES")
    require(len(payload["final_layout"]) == 80, "80-turbine layout")
    require(payload["reference"]["feasible"] is True, "reference feasible")
    require(payload["final_evaluation"]["feasible"] is True, "final feasible")
    require(payload["parallel_regions"] > 0, "parallel regions")
    if workers > 1:
        require(payload["observed_workers"] >= 2, "worker participation")


def h6(arguments: argparse.Namespace, root: Path) -> dict[str, Any]:
    report: dict[str, Any] = {"status": "pass", "studies": {}}
    for index, study in enumerate(("short", "long", "overall")):
        profile = "table3_compatible" if study == "overall" else "equation_declared"
        rows = {}
        for workers in (1, arguments.total_workers):
            payload = execute(
                binary=arguments.binary,
                output=root / "h6" / study / f"workers-{workers:02d}.json",
                study=study, profile=profile, weight=0.95,
                workers=workers, seed=arguments.seed_base - 10 + index,
                scenario_seed=arguments.scenario_seed,
                scenarios=arguments.scenarios, fes=arguments.h6_fes,
                source_commit=arguments.source_commit,
            )
            validate(payload, study, profile, 0.95, workers, arguments.h6_fes)
            rows[workers] = payload
        serial, parallel = rows[1], rows[arguments.total_workers]
        for key in (
            "physical_fes", "accepted_moves", "infeasible_proposals", "reference",
            "final_evaluation", "final_layout", "scientific_hash",
        ):
            require(serial[key] == parallel[key], f"{study} differs: {key}")
        speedup = {
            "wake_update": serial["wake_update_seconds"]
                / parallel["wake_update_seconds"],
            "robustness_metric": serial["robustness_metric_seconds"]
                / parallel["robustness_metric_seconds"],
            "end_to_end": serial["end_to_end_seconds"]
                / parallel["end_to_end_seconds"],
        }
        for stage, value in speedup.items():
            require(value > 1.0, f"{study} {stage} did not accelerate")
        report["studies"][study] = {
            "fixed_physical_fes": arguments.h6_fes,
            "one_worker": serial,
            "all_worker": parallel,
            "speedup": speedup,
            "same_layout_metrics_fes_and_hash": True,
        }
    write_json(root / "h6" / "summary.json", report)
    return report


def formal_cases() -> list[tuple[str, str, float]]:
    result = []
    result.extend(("short", "equation_declared", value) for value in WEIGHTS)
    result.extend(("long", "equation_declared", value) for value in WEIGHTS)
    result.extend(("overall", "table3_compatible", value) for value in WEIGHTS)
    result.extend(("overall", "equation_declared", value) for value in WEIGHTS)
    return result


def formal(arguments: argparse.Namespace, root: Path) -> dict[str, Any]:
    rows = []
    for index, (study, profile, weight) in enumerate(formal_cases()):
        slug = f"{index:02d}-{study}-{profile}-w{weight:g}".replace(".", "p")
        payload = execute(
            binary=arguments.binary,
            output=root / "formal" / f"{slug}.json",
            study=study, profile=profile, weight=weight,
            workers=arguments.total_workers, seed=arguments.seed_base + index,
            scenario_seed=arguments.scenario_seed,
            scenarios=arguments.scenarios, fes=arguments.formal_fes,
            source_commit=arguments.source_commit,
        )
        validate(payload, study, profile, weight,
                 arguments.total_workers, arguments.formal_fes)
        rows.append({
            "study": study,
            "profile": profile,
            "weight": weight,
            "seed": payload["seed"],
            "physical_fes": payload["physical_fes"],
            "accepted_moves": payload["accepted_moves"],
            "mean_power_mw": payload["final_evaluation"]["mean_power_mw"],
            "variability_of_power":
                payload["final_evaluation"]["variability_of_power"],
            "long_term_mean_mw":
                payload["final_evaluation"]["long_term_mean_mw"],
            "long_term_std_mw":
                payload["final_evaluation"]["long_term_std_mw"],
            "objective": (
                payload["final_evaluation"]["short_robustness"]
                if study == "short" else
                payload["final_evaluation"]["long_robustness"]
                if study == "long" else
                payload["final_evaluation"]["overall_robustness"]
            ),
            "end_to_end_seconds": payload["end_to_end_seconds"],
            "scientific_hash": payload["scientific_hash"],
        })
    native = [row for row in rows if not (
        row["study"] == "overall" and row["profile"] == "equation_declared"
    )]
    require(len(native) == 15, "15 native result rows")
    require(len(rows) == 20, "five supplementary conflict rows")
    summary = {
        "status": "pass",
        "source_commit": arguments.source_commit,
        "paper_native_case_count": 15,
        "supplementary_conflict_case_count": 5,
        "formal_physical_fes_per_case": arguments.formal_fes,
        "long_term_scenarios": arguments.scenarios,
        "workers_per_case": arguments.total_workers,
        "rows": rows,
        "claim_boundary": (
            "flexible academic reproduction; one run per paper weight because "
            "no independent repeat count is reported; not exact numeric replay"
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
    parser.add_argument("--seed-base", type=int, default=690100)
    parser.add_argument("--scenario-seed", type=int, default=69000)
    parser.add_argument("--scenarios", type=int, default=1000)
    parser.add_argument("--h6-fes", type=int, default=1000)
    parser.add_argument("--formal-fes", type=int, default=10000)
    args = parser.parse_args()
    args.binary = args.binary.resolve()
    root = args.output_root.resolve()
    root.mkdir(parents=True, exist_ok=True)
    h6_report = h6(args, root)
    formal_report = formal(args, root)
    write_json(root / "campaign_summary.json", {
        "status": "pass",
        "h6_summary": str(root / "h6" / "summary.json"),
        "formal_summary": str(root / "formal" / "summary.json"),
        "minimum_h6_end_to_end_speedup": min(
            row["speedup"]["end_to_end"]
            for row in h6_report["studies"].values()
        ),
        "formal_cases": len(formal_report["rows"]),
    })
    print(json.dumps({
        "status": "pass",
        "h6_studies": 3,
        "formal_cases": 20,
        "output_root": str(root),
    }, sort_keys=True))


if __name__ == "__main__":
    main()
