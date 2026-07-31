#!/usr/bin/env python3
"""
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: resumable T33 Waffle H6 and twenty-case formal campaign
Paper/DOI: Combined Layout and Cable Optimization of Offshore Wind Farms;
10.1016/j.ejor.2023.04.046
Official data DOI: 10.11583/DTU.13134731
Cited cable predecessor DOI: 10.1002/net.22100
Public source, missing assets, paper/data conflicts, declared completions,
semantic IDs, production backend, and claim boundary:
hpc/core99_cpp/include/core99/cazzaro_t33.hpp
Resource rule: H6 compares the identical pure-C++ complete low-density
trajectory with one and all twenty workers and independent cold matrix
caches. Formal runs use one all-core process, execute all twenty paper cases
by 25 independent seeds sequentially, and reuse immutable per-site wake
matrices without sharing algorithm state between seeds.
Controlling contract: shared/contracts/core99_t33_cazzaro_combined_2023.json
Claim boundary: declared academic reconstruction, not author numerical replay
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
"""

from __future__ import annotations

import argparse
import json
import math
import os
from pathlib import Path
import statistics
import subprocess
import time


SITES = tuple(chr(ord("a") + index) for index in range(10))
CASES = tuple(
    f"{site}_{density}"
    for site in SITES
    for density in ("low", "high")
)
METHOD_ID = "t33_combined_layout_cable_vns_declared_v1"
PROBLEM_ID = "t33_official_synthetic10_low_high_joint_npv_v1"
PROTOCOL_ID = "t33_fixed_860_2064_cycles_25seed_v1"
LOW_CYCLES = 860
HIGH_CYCLES = 2064


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


def cycles_for(case_name: str) -> int:
    return HIGH_CYCLES if case_name.endswith("_high") else LOW_CYCLES


def execute(
    binary: str,
    data_root: str,
    output: Path,
    matrix_cache: Path,
    case_name: str,
    workers: int,
    seed: int,
    source_commit: str,
) -> dict:
    output.parent.mkdir(parents=True, exist_ok=True)
    matrix_cache.parent.mkdir(parents=True, exist_ok=True)
    if output.exists():
        payload = json.loads(output.read_text(encoding="utf-8"))
        if payload.get("source_commit") == source_commit:
            return payload

    temporary = output.with_suffix(".tmp")
    started = time.monotonic()
    completed = subprocess.run(
        [
            binary,
            "--mode", "optimize",
            "--data-root", data_root,
            "--case", case_name,
            "--workers", str(workers),
            "--cycles", str(cycles_for(case_name)),
            "--seed", str(seed),
            "--matrix-cache", str(matrix_cache),
            "--output", str(temporary),
        ],
        text=True,
        capture_output=True,
        timeout=6 * 60 * 60,
    )
    if completed.returncode != 0:
        raise RuntimeError(completed.stderr or completed.stdout)
    payload = json.loads(temporary.read_text(encoding="utf-8"))
    payload["source_commit"] = source_commit
    payload["runner_wall_seconds"] = time.monotonic() - started
    temporary.write_text(
        json.dumps(payload, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    os.replace(temporary, output)
    return payload


def validate(payload: dict, case_name: str, workers: int) -> None:
    require(
        payload.get("case_id") == f"t33_official_{case_name}",
        f"{case_name}: case ID mismatch",
    )
    require(
        payload.get("problem_semantic_id") == PROBLEM_ID,
        f"{case_name}: problem semantic ID mismatch",
    )
    require(
        payload.get("method_semantic_id") == METHOD_ID,
        f"{case_name}: method semantic ID mismatch",
    )
    require(
        payload.get("protocol_semantic_id") == PROTOCOL_ID,
        f"{case_name}: protocol semantic ID mismatch",
    )
    require(
        payload.get("completed_vns_cycles") == cycles_for(case_name),
        f"{case_name}: complete VNS-cycle count mismatch",
    )
    require(
        payload.get("requested_workers") == workers
        and payload.get("observed_workers") == workers,
        f"{case_name}: worker activation mismatch",
    )
    require(
        payload.get("layout_candidate_evaluations", 0) > 0
        and payload.get("cable_route_evaluations", 0) > 0,
        f"{case_name}: missing physical algorithm work",
    )
    initial = payload.get("initial", {})
    best = payload.get("best", {})
    for label, evaluation in (("initial", initial), ("best", best)):
        require(evaluation.get("feasible"), f"{case_name}: {label} infeasible")
        require(
            evaluation.get("spacing_violation_m") == 0.0
            and evaluation.get("cable_crossings") == 0,
            f"{case_name}: {label} constraint violation",
        )
        for field in (
            "aep_mwh",
            "lifetime_revenue_eur",
            "foundation_cost_eur",
            "cable_cost_eur",
            "npv_eur",
        ):
            require(
                math.isfinite(evaluation[field]),
                f"{case_name}: nonfinite {label} {field}",
            )
    require(
        best["npv_eur"] + 1.0e-6 >= initial["npv_eur"],
        f"{case_name}: incumbent NPV regressed",
    )
    require(payload.get("scientific_hash"), f"{case_name}: missing hash")


def run_h6(args: argparse.Namespace, root: Path) -> dict:
    rows = {}
    for workers in (1, args.total_workers):
        rows[workers] = execute(
            args.binary,
            args.data_root,
            root / "h6" / f"workers-{workers:02d}.json",
            root / "h6" / f"workers-{workers:02d}-cold-a.pair",
            "a_low",
            workers,
            args.seed_base - 1,
            args.source_commit,
        )
        validate(rows[workers], "a_low", workers)
    serial = rows[1]
    parallel = rows[args.total_workers]
    require(
        serial["scientific_hash"] == parallel["scientific_hash"],
        "T33 one/all-core scientific trajectory mismatch",
    )
    require(
        serial["best"] == parallel["best"],
        "T33 one/all-core objective mismatch",
    )
    speedup = {
        "matrix": serial["matrix_seconds"] / parallel["matrix_seconds"],
        "candidate":
            serial["candidate_seconds"] / parallel["candidate_seconds"],
        "cable": serial["cable_seconds"] / parallel["cable_seconds"],
        "optimization":
            serial["optimization_seconds"]
            / parallel["optimization_seconds"],
        "end_to_end":
            serial["end_to_end_seconds"] / parallel["end_to_end_seconds"],
    }
    require(
        speedup["matrix"] > 1.0
        and speedup["candidate"] > 1.0
        and speedup["end_to_end"] > 1.0,
        f"T33 dominant stages did not accelerate: {speedup}",
    )
    result = {
        "status": "pass",
        "case": "a_low",
        "complete_vns_cycles": LOW_CYCLES,
        "serial": serial,
        "parallel": parallel,
        "speedup": speedup,
        "claim_boundary":
            "same pure-C++ source, paper problem, seed, complete VNS work, "
            "cold matrix cache, and scientific trajectory; one versus all "
            "twenty Waffle workers",
    }
    (root / "h6" / "summary.json").write_text(
        json.dumps(result, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    return result


def summarize_case(payloads: list[dict]) -> dict:
    best = [item["best"] for item in payloads]
    return {
        "repeat_count": len(payloads),
        "complete_vns_cycles":
            sum(item["completed_vns_cycles"] for item in payloads),
        "layout_candidate_evaluations":
            sum(item["layout_candidate_evaluations"] for item in payloads),
        "cable_route_evaluations":
            sum(item["cable_route_evaluations"] for item in payloads),
        "mean_best_npv_eur":
            statistics.fmean(item["npv_eur"] for item in best),
        "standard_deviation_best_npv_eur":
            statistics.stdev(item["npv_eur"] for item in best),
        "mean_best_aep_mwh":
            statistics.fmean(item["aep_mwh"] for item in best),
        "standard_deviation_best_aep_mwh":
            statistics.stdev(item["aep_mwh"] for item in best),
        "mean_best_foundation_cost_eur":
            statistics.fmean(item["foundation_cost_eur"] for item in best),
        "mean_best_cable_cost_eur":
            statistics.fmean(item["cable_cost_eur"] for item in best),
        "median_matrix_seconds":
            statistics.median(item["matrix_seconds"] for item in payloads),
        "median_optimization_seconds":
            statistics.median(
                item["optimization_seconds"] for item in payloads
            ),
        "median_end_to_end_seconds":
            statistics.median(
                item["end_to_end_seconds"] for item in payloads
            ),
        "scientific_hashes": [
            item["scientific_hash"] for item in payloads
        ],
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", required=True)
    parser.add_argument("--data-root", required=True)
    parser.add_argument("--output-root", required=True)
    parser.add_argument("--source-commit", required=True)
    parser.add_argument("--total-workers", type=int, default=20)
    parser.add_argument("--repeat-count", type=int, default=25)
    parser.add_argument("--seed-base", type=int, default=2026033000)
    args = parser.parse_args()
    require(args.total_workers == 20, "T33 Waffle requires all 20 cores")
    require(args.repeat_count == 25, "T33 formal requires 25 seeds")

    root = Path(args.output_root)
    root.mkdir(parents=True, exist_ok=True)
    started = time.monotonic()
    h6 = run_h6(args, root)

    case_summaries = {}
    completed_runs = 0
    for case_index, case_name in enumerate(CASES):
        payloads = []
        site = case_name[0]
        matrix_cache = root / "matrix-cache" / f"site-{site}.pair"
        for repeat in range(args.repeat_count):
            payload = execute(
                args.binary,
                args.data_root,
                root / "formal" / case_name / f"repeat-{repeat:02d}.json",
                matrix_cache,
                case_name,
                args.total_workers,
                args.seed_base + 1000 * case_index + repeat,
                args.source_commit,
            )
            validate(payload, case_name, args.total_workers)
            payloads.append(payload)
            completed_runs += 1
            print(
                f"t33_formal_progress completed={completed_runs}/"
                f"{len(CASES) * args.repeat_count} case={case_name} "
                f"repeat={repeat}",
                flush=True,
            )
        case_summaries[case_name] = summarize_case(payloads)

    summary = {
        "schema_version": 1,
        "corpus_id": "T33",
        "status": "pass",
        "source_commit": args.source_commit,
        "selected_workers": args.total_workers,
        "h6": h6,
        "formal_run_count": completed_runs,
        "total_complete_vns_cycles":
            sum(
                summary["complete_vns_cycles"]
                for summary in case_summaries.values()
            ),
        "cases": case_summaries,
        "campaign_wall_seconds": time.monotonic() - started,
        "claim_boundary":
            "formal results for the declared paper/data academic "
            "reconstruction; not author source, unpublished manual "
            "substations, proprietary Gurobi lifecycle, original random "
            "stream, or numerical replay",
    }
    (root / "summary.json").write_text(
        json.dumps(summary, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    print(
        "t33_h6_formal_pass "
        f"runs={completed_runs} "
        f"cycles={summary['total_complete_vns_cycles']}",
        flush=True,
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
