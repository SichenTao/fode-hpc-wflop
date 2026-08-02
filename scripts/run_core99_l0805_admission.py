#!/usr/bin/env python3
"""
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: resumable L0805 Waffle H6 and paper-native campaign
Paper/DOI: Shao et al.; 10.1016/J.ENERGY.2025.138820.
Protocol: one/all-core H6 on the same Case-III batch, then 30 independent
all-core runs for Cases I-III and the paper's single Case-IV run.
Public source, missing assets, conflicts, reconstruction, HPC analysis,
semantic IDs and claim boundary:
hpc/core99_cpp/include/core99/shao_l0805.hpp.
Controlling contract: shared/contracts/core99_l0805_pce_kriging_2025.json.
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
from typing import Any


METHOD = "l0805_pce_additive_quadratic_kriging_msp_ei_ga_v1"
PROTOCOL = "l0805_native_30x3_plus_single_iv_v1"
CASES = {
    "l0805_case_i": (80, 343, 50, 30),
    "l0805_case_ii": (160, 567, 50, 30),
    "l0805_case_iii": (320, 839, 50, 30),
    "l0805_case_iv": (160, 272, 8, 1),
}


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
    command_arguments: list[str],
    identity: dict[str, Any],
) -> dict[str, Any]:
    binary_hash = sha256(args.binary)
    if output.exists():
        payload = json.loads(output.read_text(encoding="utf-8"))
        if (payload.get("source_commit") == args.source_commit
                and payload.get("binary_sha256") == binary_hash
                and all(payload.get(key) == value
                        for key, value in identity.items())):
            return payload
    completed = subprocess.run(
        [str(args.binary), *command_arguments], check=True, text=True,
        capture_output=True, timeout=2 * 60 * 60,
    )
    payload = json.loads(completed.stdout)
    payload.update(identity)
    payload["source_commit"] = args.source_commit
    payload["binary_sha256"] = binary_hash
    write_json(output, payload)
    return payload


def batch_science(payload: dict[str, Any]) -> dict[str, Any]:
    return {
        key: value for key, value in payload.items()
        if key not in {
            "requested_workers", "observed_workers", "seconds",
            "source_commit", "binary_sha256", "campaign_action",
            "campaign_repeat",
        }
    }


def run_h6(args: argparse.Namespace, root: Path) -> dict[str, Any]:
    rows: dict[int, list[dict[str, Any]]] = {}
    for workers in (1, args.total_workers):
        rows[workers] = []
        for repeat in range(1, 4):
            identity = {
                "campaign_action": "profile-batch",
                "campaign_repeat": repeat,
                "requested_workers": workers,
            }
            rows[workers].append(execute(
                args,
                root / "h6" / f"workers-{workers:02d}-repeat-{repeat:02d}.json",
                [
                    "--action", "profile-batch", "--case", "l0805_case_iii",
                    "--layouts", "320", "--seed", "2026080501",
                    "--workers", str(workers),
                ],
                identity,
            ))
    reference = batch_science(rows[1][0])
    for workers, repeats in rows.items():
        for payload in repeats:
            require(batch_science(payload) == reference,
                    f"L0805 H6 science differs at {workers} workers")
            require(payload["observed_workers"] >= min(2, workers),
                    f"L0805 H6 participation differs at {workers} workers")
    medians = {
        workers: statistics.median(row["seconds"] for row in repeats)
        for workers, repeats in rows.items()
    }
    speedup = medians[1] / medians[args.total_workers]
    require(speedup > 1.0, "L0805 Case-III batch not accelerated")
    result = {
        "status": "pass",
        "case": "Case III, 320 layouts, 50 PCE wake states each",
        "worker_comparison": [1, args.total_workers],
        "repeats_per_worker": 3,
        "median_seconds": medians,
        "speedup": speedup,
        "serial": rows[1],
        "parallel": rows[args.total_workers],
    }
    write_json(root / "h6" / "summary.json", result)
    return result


def validate_formal(
    payload: dict[str, Any], case_id: str, workers: int,
) -> None:
    initial, target, states, _ = CASES[case_id]
    require(payload["case_id"] == case_id, f"L0805 case {case_id}")
    require(payload["method_semantic_id"] == METHOD, "L0805 method")
    require(payload["protocol_semantic_id"] == PROTOCOL, "L0805 protocol")
    require(payload["requested_workers"] == workers, "L0805 workers")
    require(payload["observed_workers"] > 1, "L0805 all-core participation")
    require(payload["initial_samples"] == initial, "L0805 initial samples")
    require(payload["truth_calls"] == target, "L0805 truth calls")
    require(payload["physical_wake_simulations"] == target * states,
            "L0805 physical wake work")
    require(len(payload["best_history_gwh"]) == 1 + target - initial,
            "L0805 convergence-history length")
    require(all(right >= left for left, right in zip(
        payload["best_history_gwh"], payload["best_history_gwh"][1:])),
        "L0805 non-monotone best history")
    require(payload["best_evaluation"]["aep_gwh"]
            >= payload["initial_best"]["aep_gwh"],
            "L0805 formal optimization reduced AEP")
    expected_degree = 0 if case_id.endswith("iv") else range(1, 9)
    if isinstance(expected_degree, int):
        require(payload["selected_pce_degree"] == expected_degree,
                "L0805 Case-IV PCE degree")
    else:
        require(payload["selected_pce_degree"] in expected_degree,
                "L0805 identifiable PCE degree")


def run_formal(args: argparse.Namespace, root: Path) -> dict[str, Any]:
    case_summaries: dict[str, Any] = {}
    completed = 0
    for case_index, (case_id, contract) in enumerate(CASES.items(), start=1):
        _, _, _, repeats = contract
        payloads = []
        for repeat in range(1, repeats + 1):
            seed = 2026080500 + case_index * 1000 + repeat
            identity = {
                "campaign_action": "optimize",
                "campaign_case": case_id,
                "campaign_repeat": repeat,
                "campaign_seed": seed,
                "requested_workers": args.total_workers,
            }
            payload = execute(
                args,
                root / "formal" / case_id / f"seed-{repeat:02d}.json",
                [
                    "--action", "optimize", "--case", case_id,
                    "--seed", str(seed), "--workers", str(args.total_workers),
                ],
                identity,
            )
            validate_formal(payload, case_id, args.total_workers)
            payloads.append(payload)
            completed += 1
        objectives = [row["best_evaluation"]["aep_gwh"] for row in payloads]
        case_summaries[case_id] = {
            "required_runs": repeats,
            "completed_runs": len(payloads),
            "median_best_aep_gwh": statistics.median(objectives),
            "minimum_best_aep_gwh": min(objectives),
            "maximum_best_aep_gwh": max(objectives),
            "median_end_to_end_seconds": statistics.median(
                row["end_to_end_seconds"] for row in payloads
            ),
            "scientific_hashes": [row["scientific_hash"] for row in payloads],
        }
    require(completed == 91, "L0805 formal campaign incomplete")
    result = {
        "status": "pass",
        "complete": True,
        "required_target_runs": 91,
        "completed_target_runs": completed,
        "workers_per_run": args.total_workers,
        "cases": case_summaries,
        "source_commit": args.source_commit,
        "binary_sha256": sha256(args.binary),
        "claim_boundary": (
            "paper-native case sizes/repeats and reconstructed lifecycle on "
            "declared proxies; not author numerical or CFD replay"
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
    parser.add_argument("--stage", choices=("all", "h6", "formal"),
                        default="all")
    args = parser.parse_args()
    args.binary = args.binary.resolve()
    require(args.binary.is_file(), "L0805 binary missing")
    require(args.total_workers >= 4, "L0805 all-core allocation invalid")
    root = args.output_root.resolve()
    root.mkdir(parents=True, exist_ok=True)
    receipt: dict[str, Any] = {
        "schema_version": 1,
        "corpus_id": "L0805",
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
