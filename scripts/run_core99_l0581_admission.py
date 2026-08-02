#!/usr/bin/env python3
"""
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: resumable L0581 Waffle H6 and paper-native campaign.
Paper/DOI, public source, missing assets, conflicts, reconstruction,
semantic IDs, HPC design and claim boundary:
hpc/core99_cpp/include/core99/varela_l0581.hpp.
Controlling contract: shared/contracts/core99_l0581_sparse_gradient_2023.json.
Last evidence-audit date: 2026-08-01
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


METHOD = "l0581_adaptive_sparse_colored_forward_ad_v1"
PROTOCOL = "l0581_accuracy_8_sizes_plus_10_paired_starts_v1"
SIZES = [38, 63, 95, 133, 177, 228, 285, 349]
THRESHOLDS = ["1e-8", "1e-10", "1e-12", "1e-14", "1e-16"]


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
        [str(args.binary), *command_arguments],
        check=True, text=True, capture_output=True, timeout=2 * 60 * 60,
    )
    payload = json.loads(completed.stdout)
    payload.update(identity)
    payload["source_commit"] = args.source_commit
    payload["binary_sha256"] = binary_hash
    write_json(output, payload)
    return payload


def accuracy_science(payload: dict[str, Any]) -> dict[str, Any]:
    ignored = {
        "dense_seconds", "sparse_seconds", "requested_workers",
        "observed_workers",
        "campaign_repeat", "campaign_workers", "source_commit",
        "binary_sha256",
    }
    return {key: value for key, value in payload.items() if key not in ignored}


def run_h6(args: argparse.Namespace, root: Path) -> dict[str, Any]:
    rows: dict[int, list[dict[str, Any]]] = {}
    for workers in (1, args.total_workers):
        rows[workers] = []
        for repeat in range(1, 4):
            identity = {
                "campaign_repeat": repeat,
                "campaign_workers": workers,
            }
            payload = execute(
                args,
                root / "h6" / f"workers-{workers:02d}-repeat-{repeat:02d}.json",
                [
                    "--action", "accuracy", "--turbines", "349",
                    "--threshold", "1e-8", "--workers", str(workers),
                ],
                identity,
            )
            rows[workers].append(payload)
    reference = accuracy_science(rows[1][0])
    for workers, repeats in rows.items():
        for payload in repeats:
            require(accuracy_science(payload) == reference,
                    f"L0581 H6 science differs at {workers} workers")
            require(payload["sparse_colors"] < payload["dense_colors"],
                    "L0581 H6 did not compress colors")
            require(payload["maximum_scaled_error"] <= 1e-6,
                    "L0581 H6 sparse gradient inaccurate")
            require(payload["requested_workers"] == workers,
                    f"L0581 H6 worker request differs at {workers}")
            require(payload["observed_workers"] >= min(2, workers),
                    f"L0581 H6 participation differs at {workers}")
    dense_medians = {
        workers: statistics.median(row["dense_seconds"] for row in repeats)
        for workers, repeats in rows.items()
    }
    sparse_medians = {
        workers: statistics.median(row["sparse_seconds"] for row in repeats)
        for workers, repeats in rows.items()
    }
    result = {
        "status": "pass",
        "workload": "349 turbines, one direction, threshold 1e-8",
        "worker_comparison": [1, args.total_workers],
        "repeats_per_worker": 3,
        "dense_median_seconds": dense_medians,
        "sparse_median_seconds": sparse_medians,
        "dense_all_core_speedup": (
            dense_medians[1] / dense_medians[args.total_workers]
        ),
        "sparse_all_core_speedup": (
            sparse_medians[1] / sparse_medians[args.total_workers]
        ),
        "all_core_dense_over_sparse_kernel_speedup": (
            dense_medians[args.total_workers]
            / sparse_medians[args.total_workers]
        ),
        "pattern_discovery_accounting": (
            "dense_seconds is the full-pattern discovery kernel; "
            "sparse_seconds is the reusable compressed kernel"
        ),
        "runs": rows,
    }
    require(result["all_core_dense_over_sparse_kernel_speedup"] > 1.0,
            "L0581 all-core sparse kernel not faster than dense kernel")
    require(result["dense_all_core_speedup"] > 1.0,
            "L0581 dense kernel did not scale across all cores")
    require(result["sparse_all_core_speedup"] > 1.0,
            "L0581 sparse kernel did not scale across all cores")
    write_json(root / "h6" / "summary.json", result)
    return result


def validate_accuracy(payload: dict[str, Any], turbines: int) -> None:
    require(payload["method_semantic_id"] == METHOD, "L0581 method")
    require(payload["turbines"] == turbines, "L0581 accuracy size")
    require(payload["dense_colors"] == 2 * turbines,
            "L0581 dense color contract")
    require(payload["requested_workers"] > 1
            and payload["observed_workers"] > 1,
            "L0581 accuracy all-core participation")
    require(0 < payload["sparse_colors"] < payload["dense_colors"],
            "L0581 sparse color contract")
    require(payload["maximum_scaled_error"] <= 1e-6,
            "L0581 formal sparse gradient accuracy")


def validate_optimization(payload: dict[str, Any], mode: str, workers: int) -> None:
    require(payload["method_semantic_id"] == METHOD, "L0581 method")
    require(payload["protocol_semantic_id"] == PROTOCOL, "L0581 protocol")
    require(payload["mode"] == mode, "L0581 optimizer variant")
    require(payload["requested_workers"] == workers, "L0581 workers")
    require(payload["observed_workers"] > 1, "L0581 worker participation")
    require(payload["final_wake_loss_percent"]
            <= payload["initial_wake_loss_percent"] + 1e-10,
            "L0581 optimization increased wake loss")
    require(len(payload["final_layout_d"]) == 95,
            "L0581 final layout size")
    require(all(right >= left for left, right in zip(
        payload["best_history"], payload["best_history"][1:])),
        "L0581 non-monotone objective history")


def run_formal(args: argparse.Namespace, root: Path) -> dict[str, Any]:
    accuracy_rows = []
    for turbines in SIZES:
        for threshold in THRESHOLDS:
            identity = {
                "campaign_role": "accuracy",
                "campaign_turbines": turbines,
                "campaign_threshold": threshold,
                "campaign_workers": args.total_workers,
            }
            payload = execute(
                args,
                root / "formal" / "accuracy"
                / f"n-{turbines:03d}-threshold-{threshold}.json",
                [
                    "--action", "accuracy", "--turbines", str(turbines),
                    "--threshold", threshold,
                    "--workers", str(args.total_workers),
                ],
                identity,
            )
            validate_accuracy(payload, turbines)
            accuracy_rows.append(payload)

    optimization_rows: dict[str, list[dict[str, Any]]] = {
        "dense": [], "sparse": [],
    }
    for repeat in range(1, 11):
        seed = 2023058100 + repeat
        paired_initial = None
        for mode in ("dense", "sparse"):
            identity = {
                "campaign_role": "optimization",
                "campaign_mode": mode,
                "campaign_repeat": repeat,
                "campaign_seed": seed,
                "campaign_workers": args.total_workers,
            }
            payload = execute(
                args,
                root / "formal" / "optimization" / mode
                / f"seed-{repeat:02d}.json",
                [
                    "--action", "optimize", "--mode", mode,
                    "--seed", str(seed),
                    "--workers", str(args.total_workers),
                    "--iterations", "24",
                ],
                identity,
            )
            validate_optimization(payload, mode, args.total_workers)
            if paired_initial is None:
                paired_initial = payload["initial_wake_loss_percent"]
            require(payload["initial_wake_loss_percent"] == paired_initial,
                    "L0581 common paired start differs")
            optimization_rows[mode].append(payload)

    require(len(accuracy_rows) == 40, "L0581 accuracy campaign incomplete")
    require(sum(map(len, optimization_rows.values())) == 20,
            "L0581 optimization campaign incomplete")
    optimization_summary = {}
    for mode, rows in optimization_rows.items():
        optimization_summary[mode] = {
            "completed_runs": len(rows),
            "median_wake_loss_reduction_points": statistics.median(
                row["wake_loss_reduction_points"] for row in rows
            ),
            "median_end_to_end_seconds": statistics.median(
                row["end_to_end_seconds"] for row in rows
            ),
            "scientific_hashes": [row["scientific_hash"] for row in rows],
        }
    result = {
        "status": "pass",
        "complete": True,
        "required_target_runs": 60,
        "completed_target_runs": 60,
        "accuracy_roles": 40,
        "optimization_roles": 20,
        "workers_per_run": args.total_workers,
        "accuracy": {
            "sizes": SIZES,
            "thresholds": THRESHOLDS,
            "maximum_scaled_error": max(
                row["maximum_scaled_error"] for row in accuracy_rows
            ),
            "minimum_color_fraction": min(
                row["color_fraction"] for row in accuracy_rows
            ),
            "maximum_color_fraction": max(
                row["color_fraction"] for row in accuracy_rows
            ),
        },
        "optimization": optimization_summary,
        "source_commit": args.source_commit,
        "binary_sha256": sha256(args.binary),
        "claim_boundary": (
            "paper-native sizes and paired roles with disclosed completion; "
            "not author target-code, optimizer, numerical or timing replay"
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
    require(args.binary.is_file(), "L0581 binary missing")
    require(args.total_workers >= 4, "L0581 all-core allocation invalid")
    root = args.output_root.resolve()
    root.mkdir(parents=True, exist_ok=True)
    receipt: dict[str, Any] = {
        "schema_version": 1,
        "corpus_id": "L0581",
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
        "corpus_id": "L0581",
        "stage": args.stage,
        "output_root": str(root),
    }, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
