#!/usr/bin/env python3
"""
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: resumable T81 Waffle H6 and 50-run paper-case campaign
Paper DOI: 10.1016/j.apenergy.2021.117947
Public source: FLORIS v2.4 is a cited dependency; no target source or native
NCEP, bathymetry, mesh, MIKE21/SWAN, or wave-load arrays were located.
Missing information and declared completion:
hpc/core99_cpp/include/core99/ti_t81.hpp
Formal protocol: each of the two paper problem roles uses 25 platform-standard
independent seeds. Every run retains 15 SLSQP starts, the two-stage method,
four published alpha0 values, and the declared solver limits.
HPC protocol: H6 compares one versus 15 independent-start workers. Formal
orchestration runs two 10-worker paper cases concurrently to occupy all 20
Waffle cores without changing the paper's 15 starts per case.
Controlling contract: shared/contracts/core99_t81_ti_2022.json
Claim boundary: academic flexible declared reconstruction, not author source,
native environmental arrays, SciPy trajectory, or numerical optimum replay.
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
"""

from __future__ import annotations

import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed
import hashlib
import json
import math
import os
from pathlib import Path
import statistics
import subprocess
import time
from typing import Any


METHOD_ID = "t81_multistart_slsqp_wave_aep_declared_v1"
PROBLEM_ID = "t81_twofarm_inhomogeneous_wave_declared_v1"
PROTOCOL_ID = "t81_twofarm_4alpha_25seed_15start_v1"
CASES = (
    ("t81_case1_mild_slope", "case1_mild_slope"),
    ("t81_case2_complex_terrain", "case2_complex_terrain"),
)
ALPHAS = (0.99, 0.98, 0.95, 0.0)


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
    case: tuple[str, str],
    workers: int,
    seed: int,
    source_commit: str,
    maximum_evaluations_per_start: int,
) -> dict[str, Any]:
    role, argument = case
    if output.exists():
        payload = json.loads(output.read_text(encoding="utf-8"))
        if (
            payload.get("source_commit") == source_commit
            and payload.get("requested_workers") == workers
        ):
            return payload
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = output.with_suffix(".binary.tmp")
    command = [
        str(binary),
        "--mode", "optimize",
        "--case", argument,
        "--workers", str(workers),
        "--multistarts", "15",
        "--maxeval-per-start", str(maximum_evaluations_per_start),
        "--xtol-rel", "1e-7",
        "--seed", str(seed),
        "--output", str(temporary),
    ]
    started = time.monotonic()
    completed = subprocess.run(
        command,
        text=True,
        capture_output=True,
        timeout=30 * 60,
    )
    if completed.returncode != 0:
        raise RuntimeError(completed.stderr or completed.stdout)
    payload = json.loads(temporary.read_text(encoding="utf-8"))
    temporary.unlink()
    payload.update({
        "paper_case_role": role,
        "source_commit": source_commit,
        "runner_wall_seconds": time.monotonic() - started,
    })
    write_json(output, payload)
    return payload


def validate(payload: dict[str, Any], role: str, workers: int) -> None:
    require(payload.get("case_id") == role, f"{role}: case mismatch")
    require(payload.get("method_semantic_id") == METHOD_ID, f"{role}: method ID")
    require(payload.get("problem_semantic_id") == PROBLEM_ID, f"{role}: problem ID")
    require(payload.get("protocol_semantic_id") == PROTOCOL_ID, f"{role}: protocol ID")
    require(payload.get("requested_workers") == workers, f"{role}: requested workers")
    require(
        payload.get("observed_workers") == min(workers, 15),
        f"{role}: independent-start worker activation",
    )
    require(payload.get("physical_fes", 0) > 0, f"{role}: physical FES")
    require(payload.get("scientific_hash"), f"{role}: scientific hash")
    stages = payload.get("stages", [])
    require(len(stages) == 5, f"{role}: two-stage cardinality")
    require(stages[0]["stage_id"] == "t81_stage1_maximum_aep", f"{role}: stage1")
    require(stages[0]["alpha1"] == 1.0, f"{role}: baseline alpha1")
    prior_beta = math.inf
    for stage, alpha in zip(stages[1:], ALPHAS, strict=True):
        require(abs(stage["alpha0"] - alpha) <= 1.0e-12, f"{role}: alpha0")
        require(stage["alpha1"] + 1.0e-6 >= alpha, f"{role}: AEP constraint")
        require(stage["beta"] <= prior_beta + 1.0e-9, f"{role}: beta monotonicity")
        prior_beta = stage["beta"]
    for stage in stages:
        evaluation = stage["best_evaluation"]
        require(evaluation["feasible"] is True, f"{role}: infeasible")
        require(evaluation["spacing_violation_m"] <= 1.0e-3, f"{role}: spacing")
        require(evaluation["boundary_violation_m"] <= 1.0e-3, f"{role}: boundary")
        require(stage["successful_starts"] >= 1, f"{role}: no successful start")
        require(stage["multistarts"] == 15, f"{role}: multistarts")


def run_h6(arguments: argparse.Namespace, root: Path) -> dict[str, Any]:
    case = CASES[0]
    all_start_workers = min(15, arguments.total_workers)
    rows = {
        workers: execute(
            arguments.binary,
            root / "h6" / f"workers-{workers:02d}.json",
            case,
            workers,
            arguments.seed_base - 1,
            arguments.source_commit,
            arguments.maximum_evaluations_per_start,
        )
        for workers in (1, all_start_workers)
    }
    for workers, payload in rows.items():
        validate(payload, case[0], workers)
    serial, parallel = rows[1], rows[all_start_workers]
    require(serial["physical_fes"] == parallel["physical_fes"], "T81 H6 FES mismatch")
    require(serial["scientific_hash"] == parallel["scientific_hash"], "T81 H6 hash mismatch")
    for serial_stage, parallel_stage in zip(serial["stages"], parallel["stages"], strict=True):
        require(serial_stage["best_layout"] == parallel_stage["best_layout"], "T81 H6 layout mismatch")
        require(serial_stage["best_evaluation"] == parallel_stage["best_evaluation"], "T81 H6 evaluation mismatch")
    speedup = {
        stage: serial[f"{stage}_seconds"] / parallel[f"{stage}_seconds"]
        for stage in ("evaluator", "end_to_end")
    }
    require(
        speedup["evaluator"] > 1.0 and speedup["end_to_end"] > 1.0,
        f"T81 independent starts did not accelerate: {speedup}",
    )
    result = {
        "status": "pass",
        "case": case[0],
        "physical_layout_evaluations": serial["physical_fes"],
        "serial": serial,
        "parallel": parallel,
        "speedup": speedup,
        "parallel_start_lanes": all_start_workers,
        "claim_boundary": (
            "same pure-C++ source, paper problem, seed, 15 SLSQP starts, "
            "physical FES, and scientific hash; one versus all available "
            "independent-start lanes"
        ),
    }
    write_json(root / "h6" / "summary.json", result)
    return result


def run_formal(arguments: argparse.Namespace, root: Path) -> dict[str, Any]:
    require(arguments.total_workers % 2 == 0, "T81 formal requires even core count")
    workers_per_run = arguments.total_workers // 2
    require(workers_per_run >= 2, "T81 formal per-run workers invalid")
    jobs = [
        (case, seed_index, arguments.seed_base + seed_index)
        for seed_index in range(25)
        for case in CASES
    ]
    if arguments.formal_max_runs > 0:
        jobs = jobs[:arguments.formal_max_runs]

    def work(job: tuple[tuple[str, str], int, int]) -> dict[str, Any]:
        case, seed_index, seed = job
        payload = execute(
            arguments.binary,
            root / "formal" / case[0] / f"seed-{seed_index:02d}.json",
            case,
            workers_per_run,
            seed,
            arguments.source_commit,
            arguments.maximum_evaluations_per_start,
        )
        validate(payload, case[0], workers_per_run)
        return payload

    rows: list[dict[str, Any]] = []
    with ThreadPoolExecutor(max_workers=2) as pool:
        futures = {pool.submit(work, job): job for job in jobs}
        for position, future in enumerate(as_completed(futures), 1):
            rows.append(future.result())
            if position % 10 == 0 or position == len(jobs):
                print(f"T81 formal progress {position}/{len(jobs)}", flush=True)
    complete = len(rows) == 2 * 25
    summary = {
        "status": "pass" if complete else "bounded_pass",
        "paper_case_roles": 2,
        "seeds_per_role": 25,
        "required_target_runs": 50,
        "completed_target_runs": len(rows),
        "complete": complete,
        "concurrent_processes": 2,
        "workers_per_process": workers_per_run,
        "aggregate_reserved_workers": 2 * workers_per_run,
        "all_parallel_start_teams_active": all(
            row["observed_workers"] == min(workers_per_run, 15)
            for row in rows
        ),
        "all_stages_feasible": all(
            stage["best_evaluation"]["feasible"]
            for row in rows
            for stage in row["stages"]
        ),
        "total_physical_layout_evaluations": sum(row["physical_fes"] for row in rows),
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
    parser.add_argument("--seed-base", type=int, default=2026078100)
    parser.add_argument("--maximum-evaluations-per-start", type=int, default=300)
    parser.add_argument("--h6-only", action="store_true")
    parser.add_argument("--skip-h6", action="store_true")
    parser.add_argument("--formal-max-runs", type=int, default=0)
    arguments = parser.parse_args()
    arguments.binary = arguments.binary.resolve()
    require(arguments.binary.is_file(), "T81 binary missing")
    require(arguments.total_workers >= 4, "T81 all-core allocation invalid")
    require(
        arguments.maximum_evaluations_per_start >= 1,
        "T81 maximum evaluations invalid",
    )
    root = arguments.output_root.resolve()
    root.mkdir(parents=True, exist_ok=True)

    receipt: dict[str, Any] = {
        "schema_version": 1,
        "corpus_id": "T81",
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
