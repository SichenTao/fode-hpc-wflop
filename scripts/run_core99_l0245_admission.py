#!/usr/bin/env python3
"""
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: resumable L0245 Waffle H6 and paper-native campaign.
Paper/DOI, public assets, missing data, conflicts, reconstruction decisions,
semantic IDs, HPC design and claim boundary:
hpc/core99_cpp/include/core99/padron_l0245.hpp.
Controlling contract: shared/contracts/core99_l0245_padron_2019.json.
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


METHOD = "l0245_pcr_cv_gradient_slsqp_declared_v1"
PROBLEM = "l0245_amalia60_two_uncertainty_floris_declared_v1"
PROTOCOL = "l0245_four_layout_convergence_three_start_10set_v1"
LAYOUTS = ["grid", "amalia", "optimized", "random"]
STARTS = ["amalia", "grid", "random"]
METHODS = ["pcr_coarse", "pcr_fine", "rectangle_coarse", "rectangle_fine"]
EXPECTED_COUNTS = {
    "pcr_coarse": 231,
    "pcr_fine": 630,
    "rectangle_coarse": 225,
    "rectangle_fine": 625,
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
        json.dumps(payload, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    os.replace(temporary, path)


def execute(
    args: argparse.Namespace,
    output: Path,
    command_arguments: list[str],
    identity: dict[str, Any],
    timeout: int = 3 * 60 * 60,
) -> dict[str, Any]:
    binary_hash = sha256(args.binary)
    data_hash = sha256(args.data)
    if output.exists():
        payload = json.loads(output.read_text(encoding="utf-8"))
        if (payload.get("source_commit") == args.source_commit
                and payload.get("binary_sha256") == binary_hash
                and payload.get("data_sha256") == data_hash
                and all(payload.get(key) == value
                        for key, value in identity.items())):
            return payload
    completed = subprocess.run(
        [str(args.binary), *command_arguments],
        check=True, text=True, capture_output=True, timeout=timeout,
    )
    payload = json.loads(completed.stdout)
    payload.update(identity)
    payload["source_commit"] = args.source_commit
    payload["binary_sha256"] = binary_hash
    payload["data_sha256"] = data_hash
    write_json(output, payload)
    return payload


def profile_science(payload: dict[str, Any]) -> dict[str, Any]:
    ignored = {
        "requested_workers", "observed_workers", "seconds",
        "campaign_repeat", "campaign_workers", "source_commit",
        "binary_sha256", "data_sha256",
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
                    "--action", "profile", "--data", str(args.data),
                    "--layout", "amalia", "--method", "pcr_fine",
                    "--seed", "2019024501", "--workers", str(workers),
                    "--repeats", "2",
                ],
                identity,
            )
            rows[workers].append(payload)
    reference = profile_science(rows[1][0])
    for workers, repeats in rows.items():
        for payload in repeats:
            require(profile_science(payload) == reference,
                    f"L0245 H6 science differs at {workers} workers")
            require(payload["requested_workers"] == workers,
                    f"L0245 H6 worker request differs at {workers}")
            require(payload["observed_workers"] >= min(2, workers),
                    f"L0245 H6 participation differs at {workers}")
            require(payload["physical_wake_simulations"] == 1260,
                    "L0245 H6 physical state count differs")
    medians = {
        workers: statistics.median(row["seconds"] for row in repeats)
        for workers, repeats in rows.items()
    }
    result = {
        "status": "pass",
        "workload": (
            "Amalia 60-turbine PC-R-fine, 630 states, 120-coordinate "
            "fixed-width AD gradient, two repeated evaluations"
        ),
        "worker_comparison": [1, args.total_workers],
        "repeats_per_worker": 3,
        "median_seconds": medians,
        "all_core_speedup": medians[1] / medians[args.total_workers],
        "runs": rows,
    }
    require(result["all_core_speedup"] > 1.0,
            "L0245 physical gradient kernel did not scale")
    write_json(root / "h6" / "summary.json", result)
    return result


def validate_evaluation(
    payload: dict[str, Any], method: str, workers: int
) -> None:
    require(payload["method_semantic_id"] == METHOD, "L0245 method")
    require(payload["problem_semantic_id"] == PROBLEM, "L0245 problem")
    evaluation = payload["evaluation"]
    require(evaluation["physical_wake_simulations"] == EXPECTED_COUNTS[method],
            "L0245 method state count")
    require(evaluation["requested_workers"] == workers,
            "L0245 worker request")
    require(evaluation["observed_workers"] > 1,
            "L0245 all-core evaluation participation")
    require(math_is_finite_positive(evaluation["aep_gwh"]),
            "L0245 invalid AEP")


def math_is_finite_positive(value: float) -> bool:
    return value == value and value not in (float("inf"), float("-inf")) \
        and value > 0.0


def validate_optimization(
    payload: dict[str, Any], method: str, start: str, workers: int
) -> None:
    require(payload["method_semantic_id"] == METHOD, "L0245 method")
    require(payload["problem_semantic_id"] == PROBLEM, "L0245 problem")
    require(payload["protocol_semantic_id"] == PROTOCOL, "L0245 protocol")
    require(payload["method"] == method, "L0245 optimization method")
    require(payload["starting_layout"] == start, "L0245 optimization start")
    require(payload["requested_workers"] == workers
            and payload["observed_workers"] > 1,
            "L0245 optimization all-core participation")
    require(payload["objective_calls"] > 0 and payload["gradient_calls"] > 0,
            "L0245 optimizer did not evaluate gradients")
    require(payload["final_evaluation"]["feasible"],
            "L0245 final layout is infeasible")
    if payload["initial_evaluation"]["feasible"]:
        require(payload["final_evaluation"]["aep_gwh"] + 1e-9
                >= payload["initial_evaluation"]["aep_gwh"],
                "L0245 optimization reduced its feasible starting objective")
    require(payload["reference_seed"] == 2019024599,
            "L0245 common Monte-Carlo reference seed")
    require(payload["reference_evaluation"]["physical_wake_simulations"]
            == 200000,
            "L0245 optimized-layout Monte-Carlo count")
    require(math_is_finite_positive(
        payload["reference_evaluation"]["aep_gwh"]
    ), "L0245 invalid optimized-layout reference AEP")
    require(len(payload["final_layout_m"]) == 60,
            "L0245 final layout cardinality")


def run_formal(args: argparse.Namespace, root: Path) -> dict[str, Any]:
    references = {}
    for layout in LAYOUTS:
        identity = {
            "campaign_role": "fixed_layout_reference",
            "campaign_layout": layout,
            "campaign_workers": args.total_workers,
        }
        payload = execute(
            args,
            root / "formal" / "references" / f"{layout}.json",
            [
                "--action", "evaluate", "--data", str(args.data),
                "--layout", layout, "--method", "monte_carlo_reference",
                "--seed", "2019024599", "--workers",
                str(args.total_workers),
            ],
            identity,
        )
        evaluation = payload["evaluation"]
        require(evaluation["physical_wake_simulations"] == 200000,
                "L0245 reference state count")
        require(evaluation["observed_workers"] > 1,
                "L0245 reference all-core participation")
        references[layout] = evaluation["aep_gwh"]

    fixed_rows = []
    for layout in LAYOUTS:
        for method in METHODS:
            for sample_set in range(1, 11):
                seed = 2019024500 + sample_set
                identity = {
                    "campaign_role": "fixed_layout_method_set",
                    "campaign_layout": layout,
                    "campaign_method": method,
                    "campaign_sample_set": sample_set,
                    "campaign_seed": seed,
                    "campaign_workers": args.total_workers,
                }
                payload = execute(
                    args,
                    root / "formal" / "fixed" / layout / method
                    / f"set-{sample_set:02d}.json",
                    [
                        "--action", "evaluate", "--data", str(args.data),
                        "--layout", layout, "--method", method,
                        "--seed", str(seed), "--workers",
                        str(args.total_workers),
                    ],
                    identity,
                )
                validate_evaluation(payload, method, args.total_workers)
                fixed_rows.append(payload)

    optimization_rows = []
    for start in STARTS:
        for method in METHODS:
            for sample_set in range(1, 11):
                seed = 2019024500 + sample_set
                identity = {
                    "campaign_role": "optimization",
                    "campaign_start": start,
                    "campaign_method": method,
                    "campaign_sample_set": sample_set,
                    "campaign_seed": seed,
                    "campaign_workers": args.total_workers,
                }
                payload = execute(
                    args,
                    root / "formal" / "optimization" / start / method
                    / f"set-{sample_set:02d}.json",
                    [
                        "--action", "optimize", "--data", str(args.data),
                        "--layout", start, "--method", method,
                        "--seed", str(seed), "--workers",
                        str(args.total_workers), "--evaluations",
                        str(args.maximum_evaluations), "--reference",
                    ],
                    identity,
                )
                validate_optimization(
                    payload, method, start, args.total_workers
                )
                optimization_rows.append(payload)

    require(len(fixed_rows) == 160, "L0245 fixed campaign incomplete")
    require(len(optimization_rows) == 120,
            "L0245 optimization campaign incomplete")
    optimization_summary = {}
    for start in STARTS:
        optimization_summary[start] = {}
        for method in METHODS:
            rows = [
                row for row in optimization_rows
                if row["campaign_start"] == start
                and row["campaign_method"] == method
            ]
            references_aep = [
                row["reference_evaluation"]["aep_gwh"] for row in rows
            ]
            optimization_summary[start][method] = {
                "completed_runs": len(rows),
                "mean_reference_aep_gwh": statistics.mean(references_aep),
                "sd_reference_aep_gwh": statistics.stdev(references_aep),
                "maximum_reference_aep_gwh": max(references_aep),
                "median_end_to_end_seconds": statistics.median(
                    row["end_to_end_seconds"] for row in rows
                ),
                "scientific_hashes": [row["scientific_hash"] for row in rows],
            }
    result = {
        "status": "pass",
        "complete": True,
        "required_target_runs": 284,
        "completed_target_runs": 284,
        "fixed_layout_references": 4,
        "fixed_layout_method_set_roles": 160,
        "optimization_roles": 120,
        "workers_per_run": args.total_workers,
        "maximum_optimizer_evaluations": args.maximum_evaluations,
        "reference_aep_gwh": references,
        "optimization": optimization_summary,
        "source_commit": args.source_commit,
        "binary_sha256": sha256(args.binary),
        "data_sha256": sha256(args.data),
        "claim_boundary": (
            "source-backed flexible method/problem/protocol reproduction; "
            "not author DAKOTA, SNOPT, target FLORISSE, numeric or timing replay"
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
    parser.add_argument("--maximum-evaluations", type=int, default=1000)
    parser.add_argument("--stage", choices=("all", "h6", "formal"),
                        default="all")
    args = parser.parse_args()
    args.binary = args.binary.resolve()
    args.data = args.data.resolve()
    require(args.binary.is_file(), "L0245 binary missing")
    require(args.data.is_file(), "L0245 public data missing")
    require(args.total_workers >= 4, "L0245 all-core allocation invalid")
    require(args.maximum_evaluations >= 12,
            "L0245 optimizer budget is not an academic run")
    root = args.output_root.resolve()
    root.mkdir(parents=True, exist_ok=True)
    receipt: dict[str, Any] = {
        "schema_version": 1,
        "corpus_id": "L0245",
        "method_semantic_id": METHOD,
        "problem_semantic_id": PROBLEM,
        "protocol_semantic_id": PROTOCOL,
        "source_commit": args.source_commit,
        "binary_sha256": sha256(args.binary),
        "data_sha256": sha256(args.data),
        "total_workers": args.total_workers,
        "maximum_evaluations": args.maximum_evaluations,
    }
    if args.stage in ("all", "h6"):
        receipt["h6"] = run_h6(args, root)
    if args.stage in ("all", "formal"):
        receipt["formal"] = run_formal(args, root)
    write_json(root / "campaign_receipt.json", receipt)
    print(json.dumps({
        "status": "pass",
        "corpus_id": "L0245",
        "stage": args.stage,
        "output_root": str(root),
    }, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
