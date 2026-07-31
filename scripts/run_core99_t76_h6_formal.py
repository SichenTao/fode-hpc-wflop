#!/usr/bin/env python3
"""
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: resumable T76 Waffle all-core H6 and paper-case campaign
Paper DOI: 10.1016/j.energy.2018.11.073
Public source: no target source, manufacturer arrays, Sha Chau hourly array,
or native random states were located.
Missing information and declared completion:
hpc/core99_cpp/include/core99/sun_t76.hpp
Formal protocol: the two aligned paper cases are evaluated once; the four
MPGA paper cases each use 25 platform-standard independent seeds. Each MPGA
run uses 10 demes, 20 chromosomes per deme, the paper's 500 unchanged-
generation stopping rule, and a declared 5000-generation fail-safe.
HPC protocol: H6 compares the same complete 950200 physical-layout trajectory
with one and all Waffle cores. Formal runs use one all-core process at a time.
Controlling contract: shared/contracts/core99_t76_sun_2019.json
Claim boundary: academic flexible declared reconstruction, not author source,
native wind/manufacturer arrays, random streams, or numerical replay.
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import statistics
import subprocess
import time
from typing import Any


METHOD_ID = "t76_mpga_directional_heterogeneous_declared_v1"
PROBLEM_ID = "t76_fourcase_directional_multitype_declared_v1"
PROTOCOL_ID = "t76_fourcase_25seed_500stall_v1"
FIXED_CASES = (
    ("t76_case1_omnidirectional_aligned", "case1_omnidirectional_aligned"),
    ("t76_case1_directional_aligned", "case1_directional_aligned"),
)
OPTIMIZED_CASES = (
    ("t76_case2_omnidirectional_mpga", "case2_omnidirectional_mpga"),
    ("t76_case2_directional_mpga", "case2_directional_mpga"),
    ("t76_case3_directional_multitype_mpga", "case3_directional_multitype_mpga"),
    ("t76_case4_sha_chau_multitype_mpga", "case4_sha_chau_multitype_mpga"),
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


def execute_reference(
    binary: Path,
    output: Path,
    case: tuple[str, str],
    source_commit: str,
) -> dict[str, Any]:
    role, argument = case
    if output.exists():
        payload = json.loads(output.read_text(encoding="utf-8"))
        if payload.get("source_commit") == source_commit:
            return payload
    completed = subprocess.run(
        [str(binary), "--mode", "evaluate", "--case", argument],
        text=True,
        capture_output=True,
        check=True,
    )
    payload = json.loads(completed.stdout)
    require(payload.get("case_id") == role, f"{role}: fixed case mismatch")
    require(payload["evaluation"]["feasible"] is True, f"{role}: infeasible")
    payload["source_commit"] = source_commit
    write_json(output, payload)
    return payload


def execute_optimize(
    binary: Path,
    output: Path,
    case: tuple[str, str],
    workers: int,
    seed: int,
    source_commit: str,
    fixed_complete_work: bool,
) -> dict[str, Any]:
    role, argument = case
    if output.exists():
        payload = json.loads(output.read_text(encoding="utf-8"))
        if payload.get("source_commit") == source_commit:
            return payload
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = output.with_suffix(".binary.tmp")
    command = [
        str(binary),
        "--mode", "optimize",
        "--case", argument,
        "--workers", str(workers),
        "--demes", "10",
        "--individuals", "20",
        "--unchanged-generations", "6000" if fixed_complete_work else "500",
        "--max-generations", "5000",
        "--migration-period", "20",
        "--seed", str(seed),
        "--output", str(temporary),
    ]
    started = time.monotonic()
    completed = subprocess.run(
        command,
        text=True,
        capture_output=True,
        timeout=4 * 60 * 60,
    )
    if completed.returncode != 0:
        raise RuntimeError(completed.stderr or completed.stdout)
    payload = json.loads(temporary.read_text(encoding="utf-8"))
    temporary.unlink()
    payload.update({
        "paper_case_role": role,
        "source_commit": source_commit,
        "fixed_complete_work": fixed_complete_work,
        "runner_wall_seconds": time.monotonic() - started,
    })
    write_json(output, payload)
    return payload


def validate_optimize(
    payload: dict[str, Any],
    role: str,
    workers: int,
    fixed_complete_work: bool,
) -> None:
    require(payload.get("case_id") == role, f"{role}: case mismatch")
    require(payload.get("method_semantic_id") == METHOD_ID, f"{role}: method ID")
    require(payload.get("problem_semantic_id") == PROBLEM_ID, f"{role}: problem ID")
    require(payload.get("protocol_semantic_id") == PROTOCOL_ID, f"{role}: protocol ID")
    require(
        payload.get("requested_workers") == workers
        and payload.get("observed_workers") == workers,
        f"{role}: worker activation mismatch",
    )
    require(payload.get("demes") == 10, f"{role}: deme count")
    require(payload.get("individuals_per_deme") == 20, f"{role}: deme size")
    generations = payload.get("generations", 0)
    require(0 < generations <= 5000, f"{role}: generation limit")
    require(payload.get("physical_fes") == 200 + generations * 190, f"{role}: FES")
    if fixed_complete_work:
        require(generations == 5000, f"{role}: complete generation work")
        require(payload.get("physical_fes") == 950200, f"{role}: complete FES")
    evaluation = payload.get("best_evaluation", {})
    require(evaluation.get("feasible") is True, f"{role}: infeasible result")
    for field in (
        "expected_power_mw",
        "theoretical_no_wake_power_mw",
        "utilization_rate",
        "minimum_turbine_power_kw",
        "maximum_turbine_power_kw",
    ):
        require(math.isfinite(evaluation[field]), f"{role}: nonfinite {field}")
    require(payload.get("scientific_hash"), f"{role}: missing hash")


def run_h6(arguments: argparse.Namespace, root: Path) -> dict[str, Any]:
    case = OPTIMIZED_CASES[1]
    rows = {}
    for workers in (1, arguments.total_workers):
        rows[workers] = execute_optimize(
            arguments.binary,
            root / "h6" / f"workers-{workers:02d}.json",
            case,
            workers,
            arguments.seed_base - 1,
            arguments.source_commit,
            True,
        )
        validate_optimize(rows[workers], case[0], workers, True)
    serial = rows[1]
    parallel = rows[arguments.total_workers]
    require(
        serial["scientific_hash"] == parallel["scientific_hash"]
        and serial["best_evaluation"] == parallel["best_evaluation"]
        and serial["best_layout"] == parallel["best_layout"],
        "T76 one/all-core scientific trajectory mismatch",
    )
    speedup = {
        stage: serial[f"{stage}_seconds"] / parallel[f"{stage}_seconds"]
        for stage in ("evaluator", "algorithm", "end_to_end")
    }
    require(
        speedup["evaluator"] > 1.0 and speedup["end_to_end"] > 1.0,
        f"T76 dominant stages did not accelerate: {speedup}",
    )
    result = {
        "status": "pass",
        "case": case[0],
        "physical_layout_evaluations": 950200,
        "serial": serial,
        "parallel": parallel,
        "speedup": speedup,
        "claim_boundary": (
            "same pure-C++ source, paper problem, seed, complete physical "
            "work, and scientific trajectory; one versus all Waffle cores"
        ),
    }
    write_json(root / "h6" / "summary.json", result)
    return result


def run_formal(arguments: argparse.Namespace, root: Path) -> dict[str, Any]:
    references = [
        execute_reference(
            arguments.binary,
            root / "formal" / role / "reference.json",
            case,
            arguments.source_commit,
        )
        for role, argument in FIXED_CASES
        for case in ((role, argument),)
    ]
    jobs = [
        (case, seed_index, arguments.seed_base + seed_index)
        for case in OPTIMIZED_CASES
        for seed_index in range(25)
    ]
    if arguments.formal_max_runs > 0:
        jobs = jobs[:arguments.formal_max_runs]
    rows: list[dict[str, Any]] = []
    for position, (case, seed_index, seed) in enumerate(jobs, 1):
        payload = execute_optimize(
            arguments.binary,
            root / "formal" / case[0] / f"seed-{seed_index:02d}.json",
            case,
            arguments.total_workers,
            seed,
            arguments.source_commit,
            False,
        )
        validate_optimize(payload, case[0], arguments.total_workers, False)
        rows.append(payload)
        if position % 25 == 0 or position == len(jobs):
            print(f"T76 formal progress {position}/{len(jobs)}", flush=True)
    complete = len(rows) == 4 * 25
    summary = {
        "status": "pass" if complete else "bounded_pass",
        "fixed_reference_cases": len(references),
        "optimized_paper_case_roles": 4,
        "seeds_per_optimized_case": 25,
        "required_target_runs": 100,
        "completed_target_runs": len(rows),
        "complete": complete,
        "all_all_core": all(
            row["requested_workers"] == arguments.total_workers
            and row["observed_workers"] == arguments.total_workers
            for row in rows
        ),
        "all_feasible": all(row["best_evaluation"]["feasible"] for row in rows),
        "total_physical_layout_evaluations": sum(row["physical_fes"] for row in rows),
        "median_generations": statistics.median(row["generations"] for row in rows),
        "median_end_to_end_seconds": statistics.median(row["end_to_end_seconds"] for row in rows),
        "binary_sha256": sha256(arguments.binary),
        "source_commit": arguments.source_commit,
    }
    write_json(root / "formal" / "summary.json", summary)
    return summary


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", required=True, type=Path)
    parser.add_argument("--output-root", required=True, type=Path)
    parser.add_argument("--source-commit", required=True)
    parser.add_argument("--total-workers", type=int, default=20)
    parser.add_argument("--seed-base", type=int, default=2026077600)
    parser.add_argument("--h6-only", action="store_true")
    parser.add_argument("--skip-h6", action="store_true")
    parser.add_argument("--formal-max-runs", type=int, default=0)
    arguments = parser.parse_args()
    arguments.binary = arguments.binary.resolve()
    require(arguments.binary.is_file(), "T76 binary missing")
    require(arguments.total_workers >= 2, "T76 all-core workers invalid")
    root = arguments.output_root.resolve()
    root.mkdir(parents=True, exist_ok=True)

    receipt: dict[str, Any] = {
        "schema_version": 1,
        "corpus_id": "T76",
        "source_commit": arguments.source_commit,
        "binary": str(arguments.binary),
        "binary_sha256": sha256(arguments.binary),
        "total_workers": arguments.total_workers,
    }
    if not arguments.skip_h6:
        receipt["h6"] = run_h6(arguments, root)
    if not arguments.h6_only:
        receipt["formal"] = run_formal(arguments, root)
    write_json(root / "campaign_receipt.json", receipt)
    print(json.dumps(receipt, indent=2, sort_keys=True))


if __name__ == "__main__":
    main()
