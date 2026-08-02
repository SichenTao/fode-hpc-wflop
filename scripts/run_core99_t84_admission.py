#!/usr/bin/env python3
"""
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: resumable T84 Waffle H6 and 3,200-run paper campaign
Paper DOI: 10.1002/we.2692
Public source: thomas2021-wec 8ff27d66079591f25619a plus pinned PlantEnergy
and Jensen3D model oracles. Missing information, conflicts, reconstruction
resolutions, semantic IDs, HPC design and claim boundary:
hpc/core99_cpp/include/core99/thomas_t84.hpp
Formal protocol: execute the 16 final roles represented by Tables 5-9 for
all 200 common author starts. The paper's exploratory WEC-A/WEC-H tuning
curves and prior-literature comparisons are diagnostics, not target roles.
HPC protocol: H6 separately compares one versus all Waffle cores for the
complete SLSQP+WEC and ALPSO+WEC workflows. Formal production launches one
single-worker process for each independent start and schedules 20 processes
concurrently, which occupies all cores without nested oversubscription.
Production backend: pure C++ CPU-HPC; Python only orchestrates immutable
executables and validates machine-readable receipts.
Controlling contract: shared/contracts/core99_t84_thomas_2022.json
Claim boundary: source-backed flexible academic reproduction; not author
SNOPT, Tapenade, pyOptSparse, PlantEnergy environment or random-state replay.
Last evidence-audit date: 2026-08-01
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


PROBLEM_ID = "t84_wec_four_case_author_data_v1"
PROTOCOL_ID = "t84_table5_9_16roles_200starts_v1"


def make_roles() -> tuple[dict[str, Any], ...]:
    roles: list[dict[str, Any]] = []
    for case_id in range(1, 5):
        roles.extend([
            {
                "id": f"case{case_id}_bpa_slsqp_control",
                "case": case_id, "wake": "bastankhah",
                "optimizer": "slsqp", "wec": False,
                "method": "t84_slsqp_control_v1",
            },
            {
                "id": f"case{case_id}_bpa_slsqp_wec",
                "case": case_id, "wake": "bastankhah",
                "optimizer": "slsqp", "wec": True,
                "method": "t84_slsqp_wec_v1",
            },
            {
                "id": f"case{case_id}_bpa_alpso_control",
                "case": case_id, "wake": "bastankhah",
                "optimizer": "alpso", "wec": False,
                "method": "t84_alpso_control_v1",
            },
        ])
    roles.append({
        "id": "case2_bpa_alpso_wec",
        "case": 2, "wake": "bastankhah", "optimizer": "alpso",
        "wec": True, "method": "t84_alpso_wec_v1",
    })
    roles.extend([
        {
            "id": "case2_jensen_slsqp_control",
            "case": 2, "wake": "jensen", "optimizer": "slsqp",
            "wec": False, "method": "t84_slsqp_control_v1",
        },
        {
            "id": "case2_jensen_slsqp_wec",
            "case": 2, "wake": "jensen", "optimizer": "slsqp",
            "wec": True, "method": "t84_slsqp_wec_v1",
        },
        {
            "id": "case2_jensen_alpso_control",
            "case": 2, "wake": "jensen", "optimizer": "alpso",
            "wec": False, "method": "t84_alpso_control_v1",
        },
    ])
    if len(roles) != 16:
        raise RuntimeError("T84 final role construction must yield 16 roles")
    return tuple(roles)


ROLES = make_roles()


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
    arguments: argparse.Namespace,
    output: Path,
    role: dict[str, Any],
    start_index: int,
    workers: int,
    seed: int,
) -> dict[str, Any]:
    binary_hash = sha256(arguments.binary)
    data_hash = sha256(arguments.data)
    if output.exists():
        payload = json.loads(output.read_text(encoding="utf-8"))
        if (
            payload.get("source_commit") == arguments.source_commit
            and payload.get("binary_sha256") == binary_hash
            and payload.get("data_sha256") == data_hash
            and payload.get("requested_workers") == workers
            and payload.get("start_index") == start_index
        ):
            return payload
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = output.with_suffix(".binary.tmp")
    command = [
        str(arguments.binary),
        "--mode", "optimize",
        "--data", str(arguments.data),
        "--case", str(role["case"]),
        "--start-index", str(start_index),
        "--wake", role["wake"],
        "--optimizer", role["optimizer"],
        "--wec" if role["wec"] else "--no-wec",
        "--workers", str(workers),
        "--seed", str(seed),
        "--maxeval-per-stage", str(arguments.maximum_slsqp_evaluations_per_stage),
        "--output", str(temporary),
    ]
    started = time.monotonic()
    completed = subprocess.run(
        command, text=True, capture_output=True, timeout=30 * 60
    )
    if completed.returncode != 0:
        raise RuntimeError(completed.stderr or completed.stdout)
    payload = json.loads(temporary.read_text(encoding="utf-8"))
    temporary.unlink()
    payload.update({
        "paper_role": role["id"],
        "protocol_semantic_id": PROTOCOL_ID,
        "source_commit": arguments.source_commit,
        "binary_sha256": binary_hash,
        "data_sha256": data_hash,
        "runner_wall_seconds": time.monotonic() - started,
    })
    write_json(output, payload)
    return payload


def validate(
    payload: dict[str, Any],
    role: dict[str, Any],
    start_index: int,
    workers: int,
) -> None:
    label = f"{role['id']}/start-{start_index}"
    require(payload.get("problem_semantic_id") == PROBLEM_ID, f"{label}: problem")
    require(payload.get("protocol_semantic_id") == PROTOCOL_ID, f"{label}: protocol")
    require(payload.get("method_semantic_id") == role["method"], f"{label}: method")
    require(payload.get("case_id") == role["case"], f"{label}: case")
    require(payload.get("wake_model") == role["wake"], f"{label}: wake")
    expected_optimizer = (
        "augmented_lagrangian_pso" if role["optimizer"] == "alpso"
        else "slsqp_open_snopt_replacement"
    )
    require(payload.get("optimizer") == expected_optimizer, f"{label}: optimizer")
    require(payload.get("use_wec") is role["wec"], f"{label}: WEC")
    require(payload.get("start_index") == start_index, f"{label}: start")
    require(payload.get("requested_workers") == workers, f"{label}: workers")
    require(payload.get("observed_workers") == workers, f"{label}: participation")
    require(payload.get("scientific_hash"), f"{label}: hash")
    final = payload.get("final_assessment", {})
    require(final.get("aep_gwh", 0.0) > 0.0, f"{label}: AEP")
    require(
        final.get("maximum_constraint_violation_m", 1.0) <= 1.0e-3,
        f"{label}: infeasible",
    )
    expected_stages = 1
    if role["optimizer"] == "slsqp" and role["wake"] == "bastankhah":
        expected_stages = 7 if role["wec"] else 2
    elif role["optimizer"] == "slsqp" and role["wake"] == "jensen":
        expected_stages = 6 if role["wec"] else 1
    elif role["optimizer"] == "alpso" and role["wec"]:
        expected_stages = 7
    require(len(payload.get("stages", [])) == expected_stages, f"{label}: stages")
    if role["optimizer"] == "alpso":
        expected_calls = 26460 if role["wec"] else {
            1: 20130, 2: 21030, 3: 20280, 4: 20430,
        }[role["case"]]
        require(
            payload.get("paper_function_call_budget") == expected_calls,
            f"{label}: paper ALPSO budget",
        )
        require(
            payload.get("executed_function_calls") == expected_calls,
            f"{label}: executed ALPSO calls",
        )


def h6_pair(
    arguments: argparse.Namespace,
    root: Path,
    role: dict[str, Any],
) -> dict[str, Any]:
    start_index = 0
    rows = {
        workers: execute(
            arguments,
            root / "h6" / role["id"] / f"workers-{workers:02d}.json",
            role,
            start_index,
            workers,
            arguments.seed_base - 1,
        )
        for workers in (1, arguments.total_workers)
    }
    for workers, payload in rows.items():
        validate(payload, role, start_index, workers)
    serial = rows[1]
    parallel = rows[arguments.total_workers]
    for key in (
        "executed_function_calls", "paper_function_call_budget",
        "scientific_hash", "final_layout", "initial_assessment",
        "final_assessment", "stages",
    ):
        if key == "stages":
            left = [{k: v for k, v in item.items() if k != "seconds"}
                    for item in serial[key]]
            right = [{k: v for k, v in item.items() if k != "seconds"}
                     for item in parallel[key]]
            require(left == right, f"{role['id']}: H6 differs for stages")
        elif key in ("initial_assessment", "final_assessment"):
            ignored = {"seconds", "requested_workers", "observed_workers"}
            left = {k: v for k, v in serial[key].items() if k not in ignored}
            right = {k: v for k, v in parallel[key].items() if k not in ignored}
            require(left == right, f"{role['id']}: H6 differs for {key}")
        else:
            require(serial[key] == parallel[key], f"{role['id']}: H6 differs for {key}")
    speedup = {
        stage: serial[f"{stage}_seconds"] / parallel[f"{stage}_seconds"]
        for stage in ("evaluator", "end_to_end")
    }
    require(speedup["evaluator"] > 1.0, f"{role['id']}: evaluator not accelerated")
    require(speedup["end_to_end"] > 1.0, f"{role['id']}: workflow not accelerated")
    return {
        "status": "pass",
        "role": role["id"],
        "serial": serial,
        "parallel": parallel,
        "speedup": speedup,
        "claim_boundary": (
            "same pure-C++ source, complete paper workflow, author start, "
            "algorithm seed, physical work, final layout and scientific hash; "
            "one versus every available Waffle CPU core"
        ),
    }


def run_h6(arguments: argparse.Namespace, root: Path) -> dict[str, Any]:
    selected = (
        next(role for role in ROLES if role["id"] == "case4_bpa_slsqp_wec"),
        next(role for role in ROLES if role["id"] == "case2_bpa_alpso_wec"),
    )
    rows = {role["id"]: h6_pair(arguments, root, role) for role in selected}
    result = {
        "status": "pass",
        "worker_comparison": [1, arguments.total_workers],
        "optimizer_families": rows,
    }
    write_json(root / "h6" / "summary.json", result)
    return result


def summarize(rows: list[dict[str, Any]]) -> dict[str, Any]:
    aep = [row["final_assessment"]["aep_gwh"] for row in rows]
    wake_loss = [row["final_assessment"]["wake_loss_percent"] for row in rows]
    improvement = [
        row["final_assessment"]["aep_gwh"]
        - row["initial_assessment"]["aep_gwh"]
        for row in rows
    ]
    return {
        "start_count": len(rows),
        "mean_final_aep_gwh": statistics.fmean(aep),
        "standard_deviation_final_aep_gwh": statistics.stdev(aep) if len(aep) > 1 else 0.0,
        "best_final_aep_gwh": max(aep),
        "mean_wake_loss_percent": statistics.fmean(wake_loss),
        "mean_aep_improvement_gwh": statistics.fmean(improvement),
        "total_complete_layout_calls": sum(row["executed_function_calls"] for row in rows),
        "median_end_to_end_seconds": statistics.median(
            row["end_to_end_seconds"] for row in rows
        ),
        "scientific_hashes": [row["scientific_hash"] for row in rows],
    }


def run_formal(arguments: argparse.Namespace, root: Path) -> dict[str, Any]:
    jobs = [
        (role, start_index, arguments.seed_base + start_index)
        for role in ROLES
        for start_index in range(200)
    ]
    if arguments.formal_max_runs > 0:
        jobs = jobs[:arguments.formal_max_runs]

    def work(job: tuple[dict[str, Any], int, int]) -> dict[str, Any]:
        role, start_index, seed = job
        payload = execute(
            arguments,
            root / "formal" / role["id"] / f"start-{start_index:03d}.json",
            role,
            start_index,
            1,
            seed,
        )
        validate(payload, role, start_index, 1)
        return payload

    rows: list[dict[str, Any]] = []
    with ThreadPoolExecutor(max_workers=arguments.total_workers) as pool:
        futures = {pool.submit(work, job): job for job in jobs}
        for position, future in enumerate(as_completed(futures), 1):
            rows.append(future.result())
            if position % 100 == 0 or position == len(jobs):
                print(f"T84 formal progress {position}/{len(jobs)}", flush=True)
    grouped = {
        role["id"]: [row for row in rows if row["paper_role"] == role["id"]]
        for role in ROLES
    }
    complete = len(rows) == 16 * 200
    result = {
        "status": "pass" if complete else "bounded_pass",
        "complete": complete,
        "paper_final_roles": 16,
        "common_author_starts_per_role": 200,
        "required_target_runs": 3200,
        "completed_target_runs": len(rows),
        "concurrent_processes": arguments.total_workers,
        "workers_per_process": 1,
        "aggregate_reserved_workers": arguments.total_workers,
        "all_final_layouts_feasible": all(
            row["final_assessment"]["maximum_constraint_violation_m"] <= 1.0e-3
            for row in rows
        ),
        "roles": {
            role: summarize(payloads)
            for role, payloads in grouped.items() if payloads
        },
        "binary_sha256": sha256(arguments.binary),
        "data_sha256": sha256(arguments.data),
        "source_commit": arguments.source_commit,
        "claim_boundary": (
            "all 16 final paper roles over all 200 public common starts; "
            "source-backed flexible academic reproduction rather than exact "
            "author optimizer or random-state replay"
        ),
    }
    write_json(root / "formal" / "summary.json", result)
    return result


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", required=True, type=Path)
    parser.add_argument("--data", required=True, type=Path)
    parser.add_argument("--output-root", required=True, type=Path)
    parser.add_argument("--source-commit", required=True)
    parser.add_argument("--total-workers", type=int, default=20)
    parser.add_argument("--seed-base", type=int, default=2026088400)
    parser.add_argument("--maximum-slsqp-evaluations-per-stage", type=int, default=220)
    parser.add_argument("--stage", choices=("all", "h6", "formal"), default="all")
    parser.add_argument("--formal-max-runs", type=int, default=0)
    arguments = parser.parse_args()
    arguments.binary = arguments.binary.resolve()
    arguments.data = arguments.data.resolve()
    require(arguments.binary.is_file(), "T84 binary missing")
    require(arguments.data.is_file(), "T84 data fixture missing")
    require(arguments.total_workers >= 4, "T84 all-core allocation invalid")
    require(
        arguments.maximum_slsqp_evaluations_per_stage >= 1,
        "T84 SLSQP maximum evaluations invalid",
    )
    root = arguments.output_root.resolve()
    root.mkdir(parents=True, exist_ok=True)
    receipt: dict[str, Any] = {
        "schema_version": 1,
        "corpus_id": "T84",
        "problem_semantic_id": PROBLEM_ID,
        "protocol_semantic_id": PROTOCOL_ID,
        "source_commit": arguments.source_commit,
        "binary_sha256": sha256(arguments.binary),
        "data_sha256": sha256(arguments.data),
        "total_workers": arguments.total_workers,
    }
    if arguments.stage in ("all", "h6"):
        receipt["h6"] = run_h6(arguments, root)
    if arguments.stage in ("all", "formal"):
        receipt["formal"] = run_formal(arguments, root)
    write_json(root / "campaign_receipt.json", receipt)
    print(json.dumps({
        "status": "pass",
        "stage": arguments.stage,
        "output_root": str(root),
        "h6": receipt.get("h6", {}).get("status"),
        "formal": receipt.get("formal", {}).get("status"),
    }, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
