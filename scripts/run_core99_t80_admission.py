#!/usr/bin/env python3
"""
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: resumable T80 Waffle H6 and 13-case 10-repeat campaign
Paper/source/missing/reconstruction:
hpc/core99_cpp/include/core99/bai_t80.hpp.
Resource rule: each optimization owns one all-twenty-core persistent team;
paper cases/repeats execute sequentially without nested oversubscription.
Claim boundary: academic reconstruction; NJ is a figure-derived proxy.
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
"""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import json
import math
from pathlib import Path
import statistics
import subprocess
import time


CASES = tuple(
    [
        f"t80_case1_s{scenario}_{size}"
        for scenario in range(1, 5)
        for size in ("small", "medium", "large")
    ]
    + ["t80_case2_new_jersey"]
)
H6_CASE = "t80_case1_s1_medium"


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


def run_one(
    *,
    binary: str,
    output: Path,
    source_commit: str,
    case_id: str,
    seed: int,
    workers: int,
) -> dict:
    output.parent.mkdir(parents=True, exist_ok=True)
    if output.exists():
        payload = json.loads(output.read_text(encoding="utf-8"))
        if (
            payload.get("source_commit") == source_commit
            and payload.get("case_id") == case_id
            and payload.get("seed") == seed
            and payload.get("requested_workers") == workers
        ):
            return payload
    temporary = output.with_suffix(".tmp")
    started = time.monotonic()
    completed = subprocess.run(
        [
            binary,
            "--mode", "optimize",
            "--case", case_id,
            "--workers", str(workers),
            "--seed", str(seed),
            "--output", str(temporary),
        ],
        text=True,
        capture_output=True,
        timeout=6 * 60 * 60,
    )
    if completed.returncode != 0:
        raise RuntimeError(completed.stderr or completed.stdout)
    payload = json.loads(temporary.read_text(encoding="utf-8"))["runs"][0]
    payload["source_commit"] = source_commit
    payload["runner_wall_seconds"] = time.monotonic() - started
    output.write_text(
        json.dumps(payload, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    temporary.unlink()
    return payload


def validate_run(payload: dict, case_id: str, workers: int) -> list[str]:
    failures: list[str] = []
    if payload.get("case_id") != case_id:
        failures.append(f"{case_id}: case ID mismatch")
    if (
        payload.get("method_semantic_id")
        != "t80_aga_spmcts_declared_completion_v1"
    ):
        failures.append(f"{case_id}: method identity mismatch")
    if (
        payload.get("population") != 100
        or payload.get("generations") != 200
        or payload.get("mcts_simulations") != 200
    ):
        failures.append(f"{case_id}: paper/completion budget mismatch")
    if payload.get("requested_workers") != workers:
        failures.append(f"{case_id}: worker request mismatch")
    if workers == 20 and payload.get("observed_workers") != 20:
        failures.append(f"{case_id}: all-core team not observed")
    if payload.get("physical_fes", 0) < 20000:
        failures.append(f"{case_id}: incomplete physical FES")
    best = payload.get("best_evaluation", {})
    initial = payload.get("initial_best", {})
    if not best.get("feasible", False):
        failures.append(f"{case_id}: final layout infeasible")
    if not math.isfinite(
        best.get("conversion_efficiency_percent", math.nan)
    ):
        failures.append(f"{case_id}: non-finite final efficiency")
    if best.get("conversion_efficiency_percent", -math.inf) < initial.get(
        "conversion_efficiency_percent", math.inf
    ) - 1.0e-12:
        failures.append(f"{case_id}: retained best regressed")
    if len(payload.get("best_efficiency_history_percent", [])) != 200:
        failures.append(f"{case_id}: convergence history incomplete")
    if not payload.get("scientific_hash"):
        failures.append(f"{case_id}: scientific hash absent")
    return failures


def h6_probe(args: argparse.Namespace, root: Path) -> dict:
    rows = {}
    for workers in (1, args.total_workers):
        rows[workers] = run_one(
            binary=args.binary,
            output=root / "h6" / f"workers-{workers:02d}.json",
            source_commit=args.source_commit,
            case_id=H6_CASE,
            seed=args.seed_base - 1,
            workers=workers,
        )
    serial = rows[1]
    parallel = rows[args.total_workers]
    require(
        not validate_run(serial, H6_CASE, 1)
        and not validate_run(parallel, H6_CASE, args.total_workers),
        "T80 H6 semantic validation failed",
    )
    require(
        serial["scientific_hash"] == parallel["scientific_hash"]
        and serial["physical_fes"] == parallel["physical_fes"],
        "T80 H6 one/all-core trajectory or FES mismatch",
    )
    require(
        parallel["mcts_relocation_seconds"]
            < serial["mcts_relocation_seconds"]
        and parallel["end_to_end_seconds"] < serial["end_to_end_seconds"],
        "T80 all-core MCTS/end-to-end did not accelerate",
    )
    receipt = {
        "status": "pass",
        "case_id": H6_CASE,
        "physical_fes": serial["physical_fes"],
        "scientific_hash": serial["scientific_hash"],
        "serial": serial,
        "parallel": parallel,
        "speedup": {
            "population_evaluation":
                serial["population_evaluation_seconds"]
                / parallel["population_evaluation_seconds"],
            "mcts_relocation":
                serial["mcts_relocation_seconds"]
                / parallel["mcts_relocation_seconds"],
            "genetic_operator":
                serial["genetic_operator_seconds"]
                / parallel["genetic_operator_seconds"],
            "end_to_end":
                serial["end_to_end_seconds"]
                / parallel["end_to_end_seconds"],
        },
        "claim_boundary":
            "same pure-C++ source, paper problem, seed, adaptive trajectory "
            "and physical FES; one versus all twenty Waffle workers",
    }
    (root / "h6").mkdir(parents=True, exist_ok=True)
    (root / "h6" / "summary.json").write_text(
        json.dumps(receipt, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    return receipt


def summarize_case(payloads: list[dict]) -> dict:
    efficiencies = [
        item["best_evaluation"]["conversion_efficiency_percent"]
        for item in payloads
    ]
    return {
        "repeat_count": len(payloads),
        "total_physical_fes": sum(item["physical_fes"] for item in payloads),
        "mean_efficiency_percent": statistics.fmean(efficiencies),
        "standard_deviation_efficiency_percent":
            statistics.stdev(efficiencies),
        "minimum_efficiency_percent": min(efficiencies),
        "median_efficiency_percent": statistics.median(efficiencies),
        "maximum_efficiency_percent": max(efficiencies),
        "median_runner_wall_seconds":
            statistics.median(item["runner_wall_seconds"] for item in payloads),
        "scientific_hashes": [item["scientific_hash"] for item in payloads],
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", required=True)
    parser.add_argument("--output-root", required=True)
    parser.add_argument("--source-commit", required=True)
    parser.add_argument("--total-workers", type=int, default=20)
    parser.add_argument("--repeat-count", type=int, default=10)
    parser.add_argument("--seed-base", type=int, default=2026080000)
    args = parser.parse_args()
    require(args.total_workers == 20, "T80 Waffle requires all 20 cores")
    require(args.repeat_count == 10, "T80 paper protocol requires 10 repeats")

    root = Path(args.output_root)
    root.mkdir(parents=True, exist_ok=True)
    started = time.monotonic()
    h6 = h6_probe(args, root)
    results: dict[str, list[dict]] = {}
    failures: list[str] = []
    completed = 0
    for case_index, case_id in enumerate(CASES):
        case_results = []
        for repeat in range(1, args.repeat_count + 1):
            payload = run_one(
                binary=args.binary,
                output=(
                    root / "formal" / case_id / f"repeat-{repeat:02d}.json"
                ),
                source_commit=args.source_commit,
                case_id=case_id,
                seed=args.seed_base + case_index * 100 + repeat,
                workers=args.total_workers,
            )
            failures.extend(validate_run(payload, case_id, 20))
            case_results.append(payload)
            completed += 1
            print(
                f"T80 completed {completed}/{len(CASES)*args.repeat_count}",
                flush=True,
            )
        results[case_id] = case_results
    summary = {
        "campaign": "T80 Waffle H6 and 13-case 10-repeat paper campaign",
        "generated_at_utc": datetime.now(timezone.utc).isoformat(),
        "source_commit": args.source_commit,
        "case_count": len(CASES),
        "repeat_count_per_case": args.repeat_count,
        "formal_run_count": len(CASES) * args.repeat_count,
        "total_physical_fes": sum(
            item["physical_fes"]
            for case_results in results.values()
            for item in case_results
        ),
        "maximum_aggregate_cpu_workers": args.total_workers,
        "resource_mapping":
            "one paper run at a time with one persistent all-twenty-core "
            "team; independent MCTS trees parallelized across individuals",
        "h6": h6,
        "case_summaries": {
            case_id: summarize_case(results[case_id]) for case_id in CASES
        },
        "campaign_wall_seconds": time.monotonic() - started,
        "status": "pass" if not failures else "fail",
        "failures": failures,
        "claim_boundary":
            "academic paper/predecessor reconstruction; New Jersey is a "
            "figure-derived proxy",
    }
    (root / "summary.json").write_text(
        json.dumps(summary, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    if failures:
        raise RuntimeError("; ".join(failures))
    print(json.dumps(summary, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
