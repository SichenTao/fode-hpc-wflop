#!/usr/bin/env python3
"""
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: resumable T68 Waffle H6 and 90-run paper-native campaign
Paper DOI: 10.1109/TSTE.2016.2614266
Public source, missing information, conflicts, deterministic completion,
semantic IDs, HPC design and claim boundary:
hpc/core99_cpp/include/core99/hou_t68.hpp
Formal protocol: 10 direction-only repeats and 20 repeats for each of Scenarios
I--IV, retaining each role's paper population, iteration receipt and the
published 50-unchanged stopping condition.
HPC protocol: H6 compares one and all Waffle workers on the full 4,800-variable
Scenario III. Formal throughput pairs two 10-worker paper runs so all 20
Waffle cores are occupied without nested oversubscription.
Controlling contract: shared/contracts/core99_t68_hou_2017.json
Claim boundary: academic flexible reconstruction, not author code, native
FINO3/cable/control arrays, random stream, or exact numerical replay.
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
"""

from __future__ import annotations

import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed
import hashlib
import json
import os
from pathlib import Path
import statistics
import subprocess
import time
from typing import Any


METHOD_ID = "t68_zhan_apso_offshore_codesign_declared_v1"
PROBLEM_ID = "t68_fino3_layout_dispatch_lpc_5role_declared_v1"
PROTOCOL_ID = "t68_native_10plus4x20_repeat_declared_v1"
CASES = (
    ("direction_only", 1, 15, 100, 10),
    ("scenario_i_spacing", 16, 30, 50, 20),
    ("scenario_ii_spacing_direction", 17, 35, 70, 20),
    ("scenario_iii_pitch", 4800, 100, 120, 20),
    ("scenario_iv_codesign", 4817, 120, 230, 20),
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
    case: tuple[str, int, int, int, int],
    workers: int,
    seed: int,
    source_commit: str,
) -> dict[str, Any]:
    role = case[0]
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
        "--case", role,
        "--workers", str(workers),
        "--unchanged-iterations", "50",
        "--seed", str(seed),
        "--output", str(temporary),
    ]
    started = time.monotonic()
    completed = subprocess.run(
        command,
        text=True,
        capture_output=True,
        timeout=60 * 60,
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


def validate(
    payload: dict[str, Any],
    case: tuple[str, int, int, int, int],
    workers: int,
) -> None:
    role, dimensions, population, iterations, _ = case
    require(payload.get("case_id") == f"t68_{role}", f"{role}: case ID")
    require(payload.get("method_semantic_id") == METHOD_ID, f"{role}: method ID")
    require(payload.get("problem_semantic_id") == PROBLEM_ID, f"{role}: problem ID")
    require(payload.get("protocol_semantic_id") == PROTOCOL_ID, f"{role}: protocol ID")
    require(payload.get("dimensions") == dimensions, f"{role}: dimensions")
    require(payload.get("population_size") == population, f"{role}: population")
    require(1 <= payload.get("generations", 0) <= iterations, f"{role}: generations")
    require(payload.get("requested_workers") == workers, f"{role}: workers")
    require(
        payload.get("observed_workers") == workers,
        f"{role}: all requested workers did not participate",
    )
    require(payload.get("physical_fes", 0) >= population, f"{role}: FES")
    require(payload.get("scientific_hash"), f"{role}: hash")
    evaluation = payload.get("best_evaluation", {})
    require(evaluation.get("feasible") is True, f"{role}: infeasible")
    require(evaluation.get("lpc_dkk_per_mwh", 0.0) > 0.0, f"{role}: LPC")
    require(evaluation.get("net_energy_gwh", 0.0) > 0.0, f"{role}: energy")
    require(
        evaluation.get("minimum_spacing_m", 0.0) >= 504.0 - 1.0e-8,
        f"{role}: spacing",
    )
    require(
        evaluation.get("pitch_penalty_mdkk", 0.0) <= 1.0e-8,
        f"{role}: pitch feasibility",
    )


def run_h6(arguments: argparse.Namespace, root: Path) -> dict[str, Any]:
    case = CASES[3]
    rows = {
        workers: execute(
            arguments.binary,
            root / "h6" / f"workers-{workers:02d}.json",
            case,
            workers,
            arguments.seed_base - 1,
            arguments.source_commit,
        )
        for workers in (1, arguments.total_workers)
    }
    for workers, payload in rows.items():
        validate(payload, case, workers)
    serial, parallel = rows[1], rows[arguments.total_workers]
    require(serial["physical_fes"] == parallel["physical_fes"], "T68 H6 FES")
    require(serial["scientific_hash"] == parallel["scientific_hash"], "T68 H6 hash")
    require(serial["best_decision"] == parallel["best_decision"], "T68 H6 decision")
    require(
        serial["best_evaluation"] == parallel["best_evaluation"],
        "T68 H6 evaluation",
    )
    speedup = {
        stage: serial[f"{stage}_seconds"] / parallel[f"{stage}_seconds"]
        for stage in ("evaluator", "algorithm", "end_to_end")
    }
    require(
        speedup["evaluator"] > 1.0 and speedup["end_to_end"] > 1.0,
        f"T68 full Scenario-III run did not accelerate: {speedup}",
    )
    result = {
        "status": "pass",
        "case": case[0],
        "dimensions": case[1],
        "physical_layout_dispatch_evaluations": serial["physical_fes"],
        "serial": serial,
        "parallel": parallel,
        "speedup": speedup,
        "claim_boundary": (
            "same pure-C++ source, 4800-variable paper problem, APSO seed, "
            "physical FES, best decision, evaluation and scientific hash; "
            "one versus all Waffle workers"
        ),
    }
    write_json(root / "h6" / "summary.json", result)
    return result


def summarize_role(payloads: list[dict[str, Any]]) -> dict[str, Any]:
    lpc = [item["best_evaluation"]["lpc_dkk_per_mwh"] for item in payloads]
    times = [item["end_to_end_seconds"] for item in payloads]
    result: dict[str, Any] = {
        "repeat_count": len(payloads),
        "total_physical_fes": sum(item["physical_fes"] for item in payloads),
        "best_lpc_dkk_per_mwh": min(lpc),
        "mean_lpc_dkk_per_mwh": statistics.fmean(lpc),
        "median_lpc_dkk_per_mwh": statistics.median(lpc),
        "median_end_to_end_seconds": statistics.median(times),
        "scientific_hashes": [item["scientific_hash"] for item in payloads],
    }
    if len(payloads) > 1:
        result["standard_deviation_lpc_dkk_per_mwh"] = statistics.stdev(lpc)
    return result


def run_formal(arguments: argparse.Namespace, root: Path) -> dict[str, Any]:
    require(arguments.total_workers % 2 == 0, "T68 formal needs even workers")
    workers_per_run = arguments.total_workers // 2
    jobs: list[tuple[tuple[str, int, int, int, int], int, int]] = []
    seed_offset = 0
    for case in CASES:
        for repeat in range(case[4]):
            jobs.append((case, repeat, arguments.seed_base + seed_offset + repeat))
        seed_offset += case[4]
    if arguments.formal_max_runs > 0:
        jobs = jobs[: arguments.formal_max_runs]

    def work(
        job: tuple[tuple[str, int, int, int, int], int, int]
    ) -> dict[str, Any]:
        case, repeat, seed = job
        payload = execute(
            arguments.binary,
            root / "formal" / case[0] / f"repeat-{repeat:02d}.json",
            case,
            workers_per_run,
            seed,
            arguments.source_commit,
        )
        validate(payload, case, workers_per_run)
        return payload

    payloads: list[dict[str, Any]] = []
    with ThreadPoolExecutor(max_workers=2) as pool:
        futures = [pool.submit(work, job) for job in jobs]
        for future in as_completed(futures):
            payloads.append(future.result())
    payloads.sort(key=lambda item: (item["paper_case_role"], item["seed"]))
    grouped = {
        case[0]: [p for p in payloads if p["paper_case_role"] == case[0]]
        for case in CASES
    }
    result = {
        "status": "pass" if len(jobs) == 90 else "development_smoke_pass",
        "required_target_runs": 90,
        "completed_target_runs": len(payloads),
        "formal_workers_per_run": workers_per_run,
        "concurrent_runs": 2,
        "binary_sha256": sha256(arguments.binary),
        "source_commit": arguments.source_commit,
        "roles": {
            role: summarize_role(rows)
            for role, rows in grouped.items() if rows
        },
        "claim_boundary": (
            "paper-native 10 direction and 20 Scenario-I--IV repeats using "
            "the admitted academic flexible reconstruction"
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
    parser.add_argument("--seed-base", type=int, default=2026086800)
    parser.add_argument("--formal-max-runs", type=int, default=0)
    arguments = parser.parse_args()
    arguments.binary = arguments.binary.resolve()
    root = arguments.output_root.resolve()
    require(arguments.binary.is_file(), "T68 binary absent")
    require(arguments.total_workers >= 2, "T68 worker allocation too small")
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
