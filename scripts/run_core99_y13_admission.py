#!/usr/bin/env python3
"""
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: resumable Y13 Waffle H6 and four-case native campaign
Paper/DOI: Du et al.; 10.1109/TSTE.2025.3609006.
Protocol: compare one versus all available Waffle CPU workers on the complete
20x20, 200-turbine, 36-scenario, ten-ADMM-iteration workflow; then execute one
deterministic native run for each of the four paper grid cases.
Facts, corrections and claim boundary: hpc/core99_cpp/include/core99/du_y13.hpp.
Controlling contract: shared/contracts/core99_y13_du_grid_admm_2026.json.
Last evidence-audit date: 2026-08-01
END WFLOP IMPLEMENTATION FACT DECLARATION
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
from typing import Any


PROBLEM = "y13_four_grid_fg36_declared_v1"
METHOD = "y13_l2box_consensus_admm_highs_declared_v1"
PROTOCOL = "y13_native_four_case_single_run_v1"
CASES = (("6x6", 36, 18), ("10x10", 100, 50),
         ("16x16", 256, 128), ("20x20", 400, 200))


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
    args: argparse.Namespace, output: Path, case: str, workers: int,
) -> dict[str, Any]:
    binary_hash = sha256(args.binary)
    if output.exists():
        payload = json.loads(output.read_text(encoding="utf-8"))
        if (payload.get("source_commit") == args.source_commit
                and payload.get("binary_sha256") == binary_hash
                and payload.get("case_id") == case
                and payload.get("requested_workers") == workers):
            return payload
    temporary = output.with_suffix(".binary.tmp")
    output.parent.mkdir(parents=True, exist_ok=True)
    completed = subprocess.run([
        str(args.binary), "--case", case, "--workers", str(workers),
        "--iterations", "10", "--output", str(temporary),
    ], text=True, capture_output=True, timeout=60 * 60)
    if completed.returncode != 0:
        raise RuntimeError(completed.stderr or completed.stdout)
    payload = json.loads(temporary.read_text(encoding="utf-8"))
    temporary.unlink()
    payload.update({
        "source_commit": args.source_commit,
        "binary_sha256": binary_hash,
        "paper_role": f"{case}_admm",
    })
    write_json(output, payload)
    return payload


def validate(payload: dict[str, Any], case: str, cells: int,
             turbines: int, workers: int) -> None:
    label = f"{case}/{workers}-workers"
    require(payload.get("problem_semantic_id") == PROBLEM, f"{label}: problem")
    require(payload.get("method_semantic_id") == METHOD, f"{label}: method")
    require(payload.get("protocol_semantic_id") == PROTOCOL, f"{label}: protocol")
    require(payload.get("cells") == cells, f"{label}: cells")
    require(payload.get("turbines") == turbines, f"{label}: turbines")
    require(payload.get("wind_scenarios") == 36, f"{label}: scenarios")
    require(payload.get("requested_workers") == workers, f"{label}: workers")
    require(payload.get("observed_workers", 0) >= min(2, workers),
            f"{label}: participation")
    require(payload.get("admm_iterations") == 10, f"{label}: iterations")
    require(payload.get("scenario_subproblem_solves") == 360,
            f"{label}: scenario subproblems")
    require(payload.get("complete_layout_evaluations") == 2,
            f"{label}: layout evaluations")
    require(len(payload.get("selected_cells", [])) == turbines,
            f"{label}: cardinality")
    require(payload.get("final_rounding_deviation", 1.0) <= 0.05,
            f"{label}: rounding deviation")
    final = payload.get("final_evaluation", {})
    require(0.0 < final.get("net_aep_gwh", 0.0)
            <= final.get("gross_aep_gwh", 0.0), f"{label}: AEP")


def normalized(payload: dict[str, Any]) -> dict[str, Any]:
    ignored = {
        "requested_workers", "observed_workers", "matrix_seconds",
        "subproblem_seconds", "evaluator_seconds", "algorithm_seconds",
        "end_to_end_seconds", "source_commit", "binary_sha256", "paper_role",
    }
    result = {key: value for key, value in payload.items() if key not in ignored}
    for field in ("initial_evaluation", "final_evaluation"):
        result[field] = {
            key: value for key, value in result[field].items()
            if key not in {"requested_workers", "observed_workers", "seconds"}
        }
    result["iterations"] = [
        {key: value for key, value in row.items()
         if key != "subproblem_seconds"}
        for row in result["iterations"]
    ]
    return result


def run_h6(args: argparse.Namespace, root: Path) -> dict[str, Any]:
    pair = {
        workers: execute(args, root / "h6" / f"workers-{workers:02d}.json",
                         "20x20", workers)
        for workers in (1, args.total_workers)
    }
    for workers, payload in pair.items():
        validate(payload, "20x20", 400, 200, workers)
    require(normalized(pair[1]) == normalized(pair[args.total_workers]),
            "Y13 one/all-core H6 science differs")
    speedup = {
        field: pair[1][f"{field}_seconds"]
               / pair[args.total_workers][f"{field}_seconds"]
        for field in ("matrix", "subproblem", "algorithm", "end_to_end")
    }
    require(speedup["subproblem"] > 1.0,
            "Y13 scenario subproblems not accelerated")
    require(speedup["end_to_end"] > 1.0, "Y13 workflow not accelerated")
    result = {
        "status": "pass",
        "worker_comparison": [1, args.total_workers],
        "case": "20x20",
        "serial": pair[1],
        "parallel": pair[args.total_workers],
        "speedup": speedup,
    }
    write_json(root / "h6" / "summary.json", result)
    return result


def run_formal(args: argparse.Namespace, root: Path) -> dict[str, Any]:
    rows = []
    for case, cells, turbines in CASES:
        payload = execute(
            args, root / "formal" / f"{case}_admm.json",
            case, args.total_workers,
        )
        validate(payload, case, cells, turbines, args.total_workers)
        rows.append(payload)
        print(f"Y13 formal progress {len(rows)}/{len(CASES)}", flush=True)
    result = {
        "status": "pass",
        "complete": True,
        "required_target_runs": 4,
        "completed_target_runs": len(rows),
        "paper_reported_native_repeats": 1,
        "workers_per_run": args.total_workers,
        "roles": {
            row["paper_role"]: {
                "case_id": row["case_id"],
                "cells": row["cells"],
                "turbines": row["turbines"],
                "net_aep_gwh": row["final_evaluation"]["net_aep_gwh"],
                "rounding_deviation": row["final_rounding_deviation"],
                "end_to_end_seconds": row["end_to_end_seconds"],
                "scientific_hash": row["scientific_hash"],
            }
            for row in rows
        },
        "source_commit": args.source_commit,
        "binary_sha256": sha256(args.binary),
        "claim_boundary": (
            "four deterministic paper-native roles under a declared "
            "equation-level reconstruction; not author numerical replay"
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
    parser.add_argument("--stage", choices=("all", "h6", "formal"), default="all")
    args = parser.parse_args()
    args.binary = args.binary.resolve()
    require(args.binary.is_file(), "Y13 binary missing")
    require(args.total_workers >= 4, "Y13 all-core allocation invalid")
    root = args.output_root.resolve()
    root.mkdir(parents=True, exist_ok=True)
    receipt: dict[str, Any] = {
        "schema_version": 1,
        "corpus_id": "Y13",
        "problem_semantic_id": PROBLEM,
        "method_semantic_id": METHOD,
        "protocol_semantic_id": PROTOCOL,
        "source_commit": args.source_commit,
        "binary_sha256": sha256(args.binary),
        "total_workers": args.total_workers,
    }
    if args.stage in ("all", "h6"):
        receipt["h6"] = run_h6(args, root)
    if args.stage in ("all", "formal"):
        receipt["formal"] = run_formal(args, root)
    write_json(root / "campaign_receipt.json", receipt)
    print(json.dumps({
        "status": "pass", "stage": args.stage,
        "output_root": str(root),
        "h6": receipt.get("h6", {}).get("status"),
        "formal": receipt.get("formal", {}).get("status"),
    }, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
