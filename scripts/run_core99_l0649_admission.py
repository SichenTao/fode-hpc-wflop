#!/usr/bin/env python3
"""
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: resumable L0649 Waffle H6 and paper-native campaign
Paper/DOI: LoCascio et al.; 10.1002/WE.2954.
Protocol: compare one versus every available Waffle CPU worker on the same
N=500, M=10 FLOWERS AEP-plus-gradient evaluation, then execute the paper's
single WR7 nine-turbine FLOWERS optimization with the all-core backend.
Public source, missing assets, reconstruction, semantic IDs and claim boundary:
hpc/core99_cpp/include/core99/locascio_l0649.hpp.
Controlling contract: shared/contracts/core99_l0649_flowers_aep_2024.json.
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


PROBLEM = "l0649_wr7_nine_turbine_14d_square_v1"
METHOD = "l0649_flowers_aep_analytic_gradient_projected_lbfgs_v1"
PROTOCOL = "l0649_native_single_optimization_plus_n500_h6_v1"


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
    args: argparse.Namespace,
    output: Path,
    action: str,
    workers: int,
) -> dict[str, Any]:
    binary_hash = sha256(args.binary)
    if output.exists():
        payload = json.loads(output.read_text(encoding="utf-8"))
        if (payload.get("source_commit") == args.source_commit
                and payload.get("binary_sha256") == binary_hash
                and payload.get("requested_workers") == workers
                and payload.get("campaign_action") == action):
            return payload
    command = [str(args.binary), "--action", action, "--workers", str(workers)]
    if action == "evaluate-scale":
        command += ["--turbines", "500"]
    completed = subprocess.run(
        command, check=True, text=True, capture_output=True, timeout=60 * 60,
    )
    payload = json.loads(completed.stdout)
    payload.update({
        "source_commit": args.source_commit,
        "binary_sha256": binary_hash,
        "campaign_action": action,
        "requested_workers": workers,
    })
    write_json(output, payload)
    return payload


def scale_science(payload: dict[str, Any]) -> dict[str, Any]:
    evaluation = dict(payload["evaluation"])
    for key in ("requested_workers", "observed_workers", "seconds"):
        evaluation.pop(key, None)
    return evaluation


def run_h6(args: argparse.Namespace, root: Path) -> dict[str, Any]:
    rows: dict[int, list[dict[str, Any]]] = {}
    for workers in (1, args.total_workers):
        rows[workers] = [
            execute(
                args,
                root / "h6" / f"workers-{workers:02d}-repeat-{repeat:02d}.json",
                "evaluate-scale",
                workers,
            )
            for repeat in range(1, 4)
        ]
    reference = scale_science(rows[1][0])
    for workers, repeats in rows.items():
        for payload in repeats:
            require(scale_science(payload) == reference,
                    f"L0649 N500 science differs at {workers} workers")
            require(payload["evaluation"]["observed_workers"] >= min(2, workers),
                    f"L0649 participation differs at {workers} workers")
    medians = {
        workers: sorted(row["evaluation"]["seconds"] for row in repeats)[1]
        for workers, repeats in rows.items()
    }
    speedup = medians[1] / medians[args.total_workers]
    require(speedup > 1.0, "L0649 N500 evaluation not accelerated")
    result = {
        "status": "pass",
        "case": "WR7 N=500 M=10 AEP plus analytical gradient",
        "worker_comparison": [1, args.total_workers],
        "repeats_per_worker": 3,
        "median_seconds": medians,
        "speedup": speedup,
        "serial": rows[1],
        "parallel": rows[args.total_workers],
    }
    write_json(root / "h6" / "summary.json", result)
    return result


def run_formal(args: argparse.Namespace, root: Path) -> dict[str, Any]:
    payload = execute(
        args, root / "formal" / "wr7_nine_turbine_flowers_opt.json",
        "optimize", args.total_workers,
    )
    require(payload["problem_semantic_id"] == PROBLEM, "L0649 formal problem")
    require(payload["method_semantic_id"] == METHOD, "L0649 formal method")
    require(payload["protocol_semantic_id"] == PROTOCOL, "L0649 formal protocol")
    require(13.7 <= payload["objective_gain_percent"] <= 14.0,
            "L0649 formal gain outside paper neighborhood")
    require(payload["history"][-1]["projected_gradient_inf"] <= 1.0e-3,
            "L0649 formal optimality tolerance")
    result = {
        "status": "pass",
        "complete": True,
        "required_target_runs": 1,
        "completed_target_runs": 1,
        "paper_reported_native_repeats": 1,
        "workers_per_run": args.total_workers,
        "role": {
            "name": payload["paper_role"],
            "initial_aep_wh": payload["initial_evaluation"]["aep_wh"],
            "final_aep_wh": payload["final_evaluation"]["aep_wh"],
            "objective_gain_percent": payload["objective_gain_percent"],
            "iterations": payload["iterations"],
            "end_to_end_seconds": payload["end_to_end_seconds"],
            "scientific_hash": payload["scientific_hash"],
        },
        "source_commit": args.source_commit,
        "binary_sha256": sha256(args.binary),
        "claim_boundary": (
            "single paper-native target role with declared open-solver "
            "replacement; not author SNOPT or numerical timing replay"
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
    require(args.binary.is_file(), "L0649 binary missing")
    require(args.total_workers >= 4, "L0649 all-core allocation invalid")
    root = args.output_root.resolve()
    root.mkdir(parents=True, exist_ok=True)
    receipt: dict[str, Any] = {
        "schema_version": 1,
        "corpus_id": "L0649",
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
        "status": "pass",
        "stage": args.stage,
        "output_root": str(root),
        "h6": receipt.get("h6", {}).get("status"),
        "formal": receipt.get("formal", {}).get("status"),
    }, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
