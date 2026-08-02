#!/usr/bin/env python3
"""
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: resumable T58 Waffle H6 and paper-native campaign
Paper/DOI: Rethore et al.; 10.1002/we.1667
Native target roles: 2x3 SLP, 2x3 SGA, 2x3 SGA+SLP, Stags SGA+SLP,
and Middelgrunden SGA+SLP. The paper reports no repeat count. One run per
role is retained as the native protocol; 25 deterministic reconstruction
seeds are additionally executed for every role containing stochastic SGA,
and are explicitly labelled as a robustness extension.
HPC protocol: H6 compares the same complete Stags and Middelgrunden
SGA+SLP workflows with one versus every available Waffle CPU core. Formal
throughput launches independent single-worker seed processes concurrently.
Source facts and claim boundary:
hpc/core99_cpp/include/core99/rethore_t58.hpp.
Controlling contract: shared/contracts/core99_t58_rethore_topfarm_2014.json
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


PROBLEM = "t58_topfarm_three_case_financial_declared_v1"
PROTOCOL = "t58_native_five_role_single_run_v1"
ROLES = (
    {"id": "2x3_slp", "case": "2x3", "method": "slp",
     "semantic": "t58_slp_declared_v1", "seeds": 1, "fes": 1401},
    {"id": "2x3_sga", "case": "2x3", "method": "sga",
     "semantic": "t58_sga_declared_v1", "seeds": 25, "fes": 3021},
    {"id": "2x3_sga_slp", "case": "2x3", "method": "sga-slp",
     "semantic": "t58_sga_slp_multifidelity_declared_v1",
     "seeds": 25, "fes": 4422},
    {"id": "stags_sga_slp", "case": "stags", "method": "sga-slp",
     "semantic": "t58_sga_slp_multifidelity_declared_v1",
     "seeds": 25, "fes": 22182},
    {"id": "middelgrunden_sga_slp", "case": "middelgrunden",
     "method": "sga-slp",
     "semantic": "t58_sga_slp_multifidelity_declared_v1",
     "seeds": 25, "fes": 21702},
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
        json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    os.replace(temporary, path)


def execute(
    arguments: argparse.Namespace,
    output: Path,
    role: dict[str, Any],
    seed_index: int,
    workers: int,
) -> dict[str, Any]:
    binary_hash = sha256(arguments.binary)
    seed = arguments.seed_base + seed_index
    if output.exists():
        payload = json.loads(output.read_text(encoding="utf-8"))
        if (payload.get("source_commit") == arguments.source_commit
                and payload.get("binary_sha256") == binary_hash
                and payload.get("requested_workers") == workers
                and payload.get("seed") == seed):
            return payload
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = output.with_suffix(".binary.tmp")
    command = [
        str(arguments.binary), "--case", role["case"],
        "--method", role["method"], "--workers", str(workers),
        "--seed", str(seed), "--output", str(temporary),
    ]
    started = time.monotonic()
    completed = subprocess.run(
        command, text=True, capture_output=True, timeout=90 * 60
    )
    if completed.returncode != 0:
        raise RuntimeError(completed.stderr or completed.stdout)
    payload = json.loads(temporary.read_text(encoding="utf-8"))
    temporary.unlink()
    payload.update({
        "paper_role": role["id"],
        "seed_index": seed_index,
        "source_commit": arguments.source_commit,
        "binary_sha256": binary_hash,
        "runner_wall_seconds": time.monotonic() - started,
    })
    write_json(output, payload)
    return payload


def validate(payload: dict[str, Any], role: dict[str, Any], workers: int) -> None:
    label = f"{role['id']}/seed-{payload.get('seed_index')}"
    require(payload.get("problem_semantic_id") == PROBLEM, f"{label}: problem")
    require(payload.get("protocol_semantic_id") == PROTOCOL, f"{label}: protocol")
    require(payload.get("method_semantic_id") == role["semantic"], f"{label}: method")
    require(payload.get("requested_workers") == workers, f"{label}: workers")
    require(payload.get("observed_workers", 0) >= min(2, workers),
            f"{label}: participation")
    require(payload.get("physical_fes") == role["fes"], f"{label}: physical FES")
    require(payload.get("scientific_hash"), f"{label}: hash")
    final = payload.get("final_evaluation", {})
    require(final.get("net_aep_mwh_per_year", 0.0) > 0.0, f"{label}: AEP")
    if role["method"] != "sga":
        require(final.get("feasible") is True, f"{label}: feasibility")


def normalized(payload: dict[str, Any]) -> dict[str, Any]:
    ignored_top = {
        "requested_workers", "observed_workers", "evaluator_seconds",
        "algorithm_seconds", "end_to_end_seconds", "runner_wall_seconds",
        "source_commit", "binary_sha256", "paper_role", "seed_index",
    }
    result = {key: value for key, value in payload.items() if key not in ignored_top}
    for field in ("initial_evaluation", "final_evaluation"):
        result[field] = {
            key: value for key, value in result[field].items()
            if key not in {"seconds", "requested_workers", "observed_workers"}
        }
    result["stages"] = [
        {key: value for key, value in stage.items()
         if key not in {"seconds", "evaluator_seconds"}}
        for stage in result["stages"]
    ]
    return result


def h6(arguments: argparse.Namespace, root: Path) -> dict[str, Any]:
    selected = [role for role in ROLES if role["id"] in {
        "stags_sga_slp", "middelgrunden_sga_slp"}]
    rows: dict[str, Any] = {}
    for role in selected:
        pair = {
            workers: execute(
                arguments,
                root / "h6" / role["id"] / f"workers-{workers:02d}.json",
                role, 0, workers,
            )
            for workers in (1, arguments.total_workers)
        }
        for workers, payload in pair.items():
            validate(payload, role, workers)
        require(normalized(pair[1]) == normalized(pair[arguments.total_workers]),
                f"{role['id']}: H6 science differs")
        speedup = {
            key: pair[1][f"{key}_seconds"]
                 / pair[arguments.total_workers][f"{key}_seconds"]
            for key in ("evaluator", "end_to_end")
        }
        require(speedup["evaluator"] > 1.0,
                f"{role['id']}: evaluator not accelerated")
        require(speedup["end_to_end"] > 1.0,
                f"{role['id']}: workflow not accelerated")
        rows[role["id"]] = {
            "status": "pass", "serial": pair[1],
            "parallel": pair[arguments.total_workers], "speedup": speedup,
        }
    result = {"status": "pass", "worker_comparison":
              [1, arguments.total_workers], "roles": rows}
    write_json(root / "h6" / "summary.json", result)
    return result


def summarize(rows: list[dict[str, Any]]) -> dict[str, Any]:
    balances = [row["final_evaluation"]["financial_balance_meur"] for row in rows]
    improvements = [
        row["final_evaluation"]["financial_balance_meur"]
        - row["initial_evaluation"]["financial_balance_meur"] for row in rows
    ]
    return {
        "run_count": len(rows),
        "mean_final_balance_meur": statistics.fmean(balances),
        "standard_deviation_final_balance_meur":
            statistics.stdev(balances) if len(balances) > 1 else 0.0,
        "best_final_balance_meur": max(balances),
        "mean_balance_improvement_meur": statistics.fmean(improvements),
        "median_end_to_end_seconds": statistics.median(
            row["end_to_end_seconds"] for row in rows),
        "total_physical_fes": sum(row["physical_fes"] for row in rows),
        "scientific_hashes": [row["scientific_hash"] for row in rows],
    }


def formal(arguments: argparse.Namespace, root: Path) -> dict[str, Any]:
    jobs = [(role, seed_index) for role in ROLES
            for seed_index in range(role["seeds"])]
    if arguments.formal_max_runs > 0:
        jobs = jobs[:arguments.formal_max_runs]

    def work(job: tuple[dict[str, Any], int]) -> dict[str, Any]:
        role, seed_index = job
        payload = execute(
            arguments,
            root / "formal" / role["id"] / f"seed-{seed_index:02d}.json",
            role, seed_index, 1,
        )
        validate(payload, role, 1)
        return payload

    rows: list[dict[str, Any]] = []
    with ThreadPoolExecutor(max_workers=arguments.total_workers) as pool:
        futures = {pool.submit(work, job): job for job in jobs}
        for position, future in enumerate(as_completed(futures), 1):
            rows.append(future.result())
            if position % 10 == 0 or position == len(jobs):
                print(f"T58 formal progress {position}/{len(jobs)}", flush=True)
    required = sum(role["seeds"] for role in ROLES)
    grouped = {
        role["id"]: [row for row in rows if row["paper_role"] == role["id"]]
        for role in ROLES
    }
    result = {
        "status": "pass" if len(rows) == required else "bounded_pass",
        "complete": len(rows) == required,
        "paper_native_roles": 5,
        "paper_reported_native_repeats": 1,
        "declared_stochastic_robustness_seeds": 25,
        "required_target_runs": required,
        "completed_target_runs": len(rows),
        "concurrent_processes": arguments.total_workers,
        "workers_per_process": 1,
        "roles": {role: summarize(values) for role, values in grouped.items()
                  if values},
        "binary_sha256": sha256(arguments.binary),
        "source_commit": arguments.source_commit,
        "claim_boundary": (
            "five paper-native roles plus explicitly labelled 25-seed "
            "reconstruction robustness; not author random-state replay"
        ),
    }
    write_json(root / "formal" / "summary.json", result)
    return result


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", required=True, type=Path)
    parser.add_argument("--output-root", required=True, type=Path)
    parser.add_argument("--source-commit", required=True)
    parser.add_argument("--total-workers", type=int, default=20)
    parser.add_argument("--seed-base", type=int, default=2026085800)
    parser.add_argument("--stage", choices=("all", "h6", "formal"), default="all")
    parser.add_argument("--formal-max-runs", type=int, default=0)
    arguments = parser.parse_args()
    arguments.binary = arguments.binary.resolve()
    require(arguments.binary.is_file(), "T58 binary missing")
    require(arguments.total_workers >= 4, "T58 all-core allocation invalid")
    root = arguments.output_root.resolve()
    root.mkdir(parents=True, exist_ok=True)
    receipt: dict[str, Any] = {
        "schema_version": 1, "corpus_id": "T58",
        "problem_semantic_id": PROBLEM, "protocol_semantic_id": PROTOCOL,
        "source_commit": arguments.source_commit,
        "binary_sha256": sha256(arguments.binary),
        "total_workers": arguments.total_workers,
    }
    if arguments.stage in ("all", "h6"):
        receipt["h6"] = h6(arguments, root)
    if arguments.stage in ("all", "formal"):
        receipt["formal"] = formal(arguments, root)
    write_json(root / "campaign_receipt.json", receipt)
    print(json.dumps({
        "status": "pass", "stage": arguments.stage,
        "output_root": str(root),
        "h6": receipt.get("h6", {}).get("status"),
        "formal": receipt.get("formal", {}).get("status"),
    }, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
