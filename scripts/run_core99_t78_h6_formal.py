#!/usr/bin/env python3
"""
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: resumable T78 Waffle H6 and 20-run paper campaign
Paper DOI: 10.1016/j.apenergy.2020.114896
Public source, missing information, conflicts, declared completion, semantic
IDs, HPC design and claim boundary:
hpc/core99_cpp/include/core99/wu_t78.hpp
Formal protocol: 10 strict-control and 10 economic-compensation repeats, each
with 80 turbines, population 100 and 200 traditional-PSO updates.
HPC protocol: H6 compares one and all Waffle workers on the complete strict
case. Formal repeats each use all Waffle cores and run sequentially, avoiding
nested oversubscription while maximizing single-optimization performance.
Controlling contract: shared/contracts/core99_t78_wu_2020.json
Claim boundary: academic flexible reconstruction, not author numerical replay.
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import statistics
import subprocess
import time
from typing import Any


METHOD_ID = "t78_traditional_pso_declared_v1"
PROBLEM_ID = "t78_fino3_noise_layout_two_case_declared_v1"
PROTOCOL_ID = "t78_native_2x10_repeat_declared_v1"
CASES = (
    ("strict_noise_control", 10),
    ("economic_compensation", 10),
)


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        while block := stream.read(1024 * 1024):
            digest.update(block)
    return digest.hexdigest()


def write_json(path: Path, payload: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(
        json.dumps(payload, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    os.replace(temporary, path)


def execute(
    binary: Path,
    output: Path,
    case: str,
    workers: int,
    seed: int,
    source_commit: str,
) -> dict[str, Any]:
    if output.exists():
        payload = json.loads(output.read_text(encoding="utf-8"))
        if (
            payload.get("source_commit") == source_commit
            and payload.get("requested_workers") == workers
        ):
            return payload
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = output.with_suffix(".binary.tmp")
    started = time.monotonic()
    completed = subprocess.run(
        [
            str(binary), "--mode", "optimize", "--case", case,
            "--workers", str(workers), "--seed", str(seed),
            "--output", str(temporary),
        ],
        text=True,
        capture_output=True,
        timeout=2 * 60 * 60,
    )
    if completed.returncode != 0:
        raise RuntimeError(completed.stderr or completed.stdout)
    payload = json.loads(temporary.read_text(encoding="utf-8"))
    temporary.unlink()
    payload.update({
        "paper_case_role": case,
        "source_commit": source_commit,
        "runner_wall_seconds": time.monotonic() - started,
    })
    write_json(output, payload)
    return payload


def validate(payload: dict[str, Any], case: str, workers: int) -> None:
    require(payload.get("case_id") == f"t78_{case}", f"{case}: ID")
    require(payload.get("method_semantic_id") == METHOD_ID, f"{case}: method")
    require(payload.get("problem_semantic_id") == PROBLEM_ID, f"{case}: problem")
    require(payload.get("protocol_semantic_id") == PROTOCOL_ID, f"{case}: protocol")
    require(payload.get("dimensions") == 160, f"{case}: dimensions")
    require(payload.get("population_size") == 100, f"{case}: population")
    require(payload.get("generations") == 200, f"{case}: iterations")
    require(payload.get("physical_fes") == 20100, f"{case}: FES")
    require(payload.get("requested_workers") == workers, f"{case}: requested")
    require(payload.get("observed_workers") == workers, f"{case}: participation")
    evaluation = payload.get("best_evaluation", {})
    require(evaluation.get("annual_energy_gwh", 0.0) > 0.0, f"{case}: energy")
    require(evaluation.get("minimum_spacing_m", 0.0) >= 713.2 - 1.0e-3,
            f"{case}: spacing")
    require(evaluation.get("spacing_violation_m", 1.0) <= 1.0e-3,
            f"{case}: spacing violation")
    if case == "strict_noise_control":
        require(evaluation.get("maximum_l10_dba", 100.0) <= 45.0 + 1.0e-6,
                "strict: noise feasibility")
    else:
        require(evaluation.get("maximum_l10_dba", 100.0) <= 50.0 + 1.0e-6,
                "economic: limited-noise feasibility")


def run_h6(arguments: argparse.Namespace, root: Path) -> dict[str, Any]:
    rows = {
        workers: execute(
            arguments.binary,
            root / "h6" / f"workers-{workers:02d}.json",
            "strict_noise_control",
            workers,
            arguments.seed_base - 1,
            arguments.source_commit,
        )
        for workers in (1, arguments.total_workers)
    }
    for workers, payload in rows.items():
        validate(payload, "strict_noise_control", workers)
    serial, parallel = rows[1], rows[arguments.total_workers]
    for key in ("physical_fes", "scientific_hash", "best_decision", "best_evaluation"):
        require(serial[key] == parallel[key], f"T78 H6 differs for {key}")
    speedup = {
        stage: serial[f"{stage}_seconds"] / parallel[f"{stage}_seconds"]
        for stage in ("evaluator", "algorithm", "end_to_end")
    }
    require(speedup["evaluator"] > 1.0 and speedup["end_to_end"] > 1.0,
            f"T78 full case did not accelerate: {speedup}")
    result = {
        "status": "pass",
        "case": "strict_noise_control",
        "physical_layout_noise_evaluations": 20100,
        "serial": serial,
        "parallel": parallel,
        "speedup": speedup,
        "claim_boundary": (
            "same pure-C++ source, full 80-turbine strict problem, PSO seed, "
            "physical FES, decision, evaluation and hash; one versus all "
            "Waffle workers"
        ),
    }
    write_json(root / "h6" / "summary.json", result)
    return result


def summarize(rows: list[dict[str, Any]]) -> dict[str, Any]:
    energy = [row["best_evaluation"]["annual_energy_gwh"] for row in rows]
    noise = [row["best_evaluation"]["maximum_l10_dba"] for row in rows]
    times = [row["end_to_end_seconds"] for row in rows]
    result = {
        "repeat_count": len(rows),
        "total_physical_fes": sum(row["physical_fes"] for row in rows),
        "best_energy_gwh": max(energy),
        "mean_energy_gwh": statistics.fmean(energy),
        "maximum_noise_dba_across_best_layouts": max(noise),
        "median_end_to_end_seconds": statistics.median(times),
        "scientific_hashes": [row["scientific_hash"] for row in rows],
    }
    if len(rows) > 1:
        result["standard_deviation_energy_gwh"] = statistics.stdev(energy)
    return result


def run_formal(arguments: argparse.Namespace, root: Path) -> dict[str, Any]:
    jobs: list[tuple[str, int, int]] = []
    offset = 0
    for case, repeats in CASES:
        for repeat in range(repeats):
            jobs.append((case, repeat, arguments.seed_base + offset + repeat))
        offset += repeats
    if arguments.formal_max_runs > 0:
        jobs = jobs[:arguments.formal_max_runs]
    payloads: list[dict[str, Any]] = []
    for case, repeat, seed in jobs:
        payload = execute(
            arguments.binary,
            root / "formal" / case / f"repeat-{repeat:02d}.json",
            case,
            arguments.total_workers,
            seed,
            arguments.source_commit,
        )
        validate(payload, case, arguments.total_workers)
        payloads.append(payload)
    grouped = {
        case: [row for row in payloads if row["paper_case_role"] == case]
        for case, _ in CASES
    }
    result = {
        "status": "pass" if len(jobs) == 20 else "development_smoke_pass",
        "required_target_runs": 20,
        "completed_target_runs": len(payloads),
        "workers_per_run": arguments.total_workers,
        "binary_sha256": sha256(arguments.binary),
        "source_commit": arguments.source_commit,
        "roles": {case: summarize(rows) for case, rows in grouped.items() if rows},
        "claim_boundary": (
            "paper-native ten strict and ten economic repeats using the "
            "admitted academic flexible reconstruction"
        ),
    }
    write_json(root / "formal" / "summary.json", result)
    return result


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--output-root", type=Path, required=True)
    parser.add_argument("--source-commit", required=True)
    parser.add_argument("--total-workers", type=int, default=20)
    parser.add_argument("--seed-base", type=int, default=2026087800)
    parser.add_argument("--formal-max-runs", type=int, default=0)
    arguments = parser.parse_args()
    arguments.binary = arguments.binary.resolve()
    root = arguments.output_root.resolve()
    require(arguments.binary.is_file(), "T78 binary absent")
    require(arguments.total_workers >= 2, "T78 worker allocation too small")
    h6 = run_h6(arguments, root)
    formal = run_formal(arguments, root)
    write_json(root / "campaign_summary.json", {
        "status": formal["status"],
        "h6_speedup": h6["speedup"],
        "formal": formal,
    })
    print(json.dumps({
        "status": formal["status"],
        "h6_end_to_end_speedup": h6["speedup"]["end_to_end"],
        "completed_target_runs": formal["completed_target_runs"],
        "output_root": str(root),
    }, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
