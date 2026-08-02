#!/usr/bin/env python3
"""
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: resumable L0499 Waffle H6 and 126-case, 20-repeat
formal campaign runner
Paper/DOI: Wen, Song and Wang; 10.1016/j.enconman.2022.115347
Source/reconstruction/claim:
hpc/core99_cpp/include/core99/wen_l0499.hpp
Resource rule: H6 compares one and all twenty workers inside one
optimization. The formal campaign uses up to twenty concurrent one-worker
case/repeat tasks because measured aggregate throughput is the governing
resource criterion; there is no nested oversubscription.
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
"""
from __future__ import annotations

import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed
from datetime import datetime, timezone
import json
import math
from pathlib import Path
import statistics
import subprocess
import time


def case_ids() -> tuple[str, ...]:
    result = [f"l0499_case_a_{objective}" for objective in ("to", "so", "ro")]
    for station in range(1, 42):
        for objective in ("to", "so", "ro"):
            result.append(
                f"l0499_case_b_station_{station:02d}_{objective}"
            )
    return tuple(result)


ALL_CASES = case_ids()
H6_CASE = "l0499_case_b_station_01_so"


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


def run_one(
    *,
    binary: str,
    data: str,
    output: Path,
    source_commit: str,
    case_id: str,
    repeat: int,
    seed: int,
    workers: int,
    max_physical_fes: int,
) -> dict:
    output.parent.mkdir(parents=True, exist_ok=True)
    if output.exists():
        payload = json.loads(output.read_text(encoding="utf-8"))
        if (
            payload.get("source_commit") == source_commit
            and payload.get("case_id") == case_id
            and payload.get("seed") == seed
            and payload.get("requested_workers") == workers
            and payload.get("physical_fes") == max_physical_fes
        ):
            return payload
    started = time.monotonic()
    completed = subprocess.run(
        [
            binary,
            "--mode", "optimize",
            "--case", case_id,
            "--data", data,
            "--workers", str(workers),
            "--max-physical-fes", str(max_physical_fes),
            "--seed", str(seed),
            "--output", str(output),
        ],
        text=True,
        capture_output=True,
    )
    if completed.returncode != 0:
        raise RuntimeError(
            f"{case_id} repeat {repeat} failed: "
            f"{completed.stderr or completed.stdout}"
        )
    payload = json.loads(output.read_text(encoding="utf-8"))
    payload["source_commit"] = source_commit
    payload["formal_repeat"] = repeat
    payload["runner_wall_seconds"] = time.monotonic() - started
    output.write_text(
        json.dumps(payload, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    return payload


def h6_probe(args: argparse.Namespace, root: Path) -> dict:
    h6_root = root / "h6"
    results: dict[int, dict] = {}
    for workers in (1, args.total_workers):
        output = h6_root / f"workers-{workers:02d}.json"
        results[workers] = run_one(
            binary=args.binary,
            data=args.data,
            output=output,
            source_commit=args.source_commit,
            case_id=H6_CASE,
            repeat=0,
            seed=args.seed_base - 1,
            workers=workers,
            max_physical_fes=args.max_physical_fes,
        )
    serial = results[1]
    parallel = results[args.total_workers]
    require(
        serial["scientific_hash"] == parallel["scientific_hash"],
        "L0499 H6 one/all-core scientific hash mismatch",
    )
    require(
        parallel["observed_workers"] == args.total_workers,
        "L0499 H6 did not activate every Waffle worker",
    )
    require(
        parallel["evaluator_seconds"] < serial["evaluator_seconds"],
        "L0499 all-core evaluator did not accelerate",
    )
    require(
        parallel["end_to_end_seconds"] < serial["end_to_end_seconds"],
        "L0499 all-core optimization did not accelerate",
    )
    serial_total = (
        serial["precomputation_seconds"] + serial["end_to_end_seconds"]
    )
    parallel_total = (
        parallel["precomputation_seconds"] + parallel["end_to_end_seconds"]
    )
    receipt = {
        "status": "pass",
        "case_id": H6_CASE,
        "physical_fes": args.max_physical_fes,
        "serial_workers": 1,
        "parallel_workers": args.total_workers,
        "scientific_hash": serial["scientific_hash"],
        "precomputation_speedup":
            serial["precomputation_seconds"]
            / parallel["precomputation_seconds"],
        "evaluator_speedup":
            serial["evaluator_seconds"] / parallel["evaluator_seconds"],
        "optimization_speedup":
            serial["end_to_end_seconds"]
            / parallel["end_to_end_seconds"],
        "cli_total_speedup": serial_total / parallel_total,
        "serial": serial,
        "parallel": parallel,
        "claim_boundary":
            "same pure-C++ source, case, seed, physical budget and "
            "scientific trajectory; one versus all twenty Waffle workers",
    }
    h6_root.mkdir(parents=True, exist_ok=True)
    (h6_root / "summary.json").write_text(
        json.dumps(receipt, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    return receipt


def validate_run(payload: dict, case_id: str, budget: int) -> list[str]:
    failures: list[str] = []
    if payload.get("case_id") != case_id:
        failures.append(f"{case_id}: case ID mismatch")
    if payload.get("method_semantic_id") != (
        "l0499_fixed_count_binary_ga_completed_v1"
    ):
        failures.append(f"{case_id}: method semantic ID mismatch")
    expected_problem = (
        "l0499_case_a_dm_cvar_grid_v1"
        if case_id.startswith("l0499_case_a_")
        else "l0499_case_b_ndawn41_proxy_dm_cvar_grid_v1"
    )
    if payload.get("problem_semantic_id") != expected_problem:
        failures.append(f"{case_id}: problem semantic ID mismatch")
    if payload.get("physical_fes") != budget:
        failures.append(f"{case_id}: physical FES mismatch")
    if payload.get("requested_workers") != 1:
        failures.append(f"{case_id}: formal task is not one-worker")
    best = payload.get("best_evaluation", {})
    initial = payload.get("initial_best", {})
    if not best.get("feasible", False):
        failures.append(f"{case_id}: infeasible final layout")
    if not math.isfinite(best.get("objective", math.nan)):
        failures.append(f"{case_id}: non-finite objective")
    if (
        best.get("objective", -math.inf) + 1.0e-9
        < initial.get("objective", math.inf)
    ):
        failures.append(f"{case_id}: final objective regressed")
    if not payload.get("scientific_hash"):
        failures.append(f"{case_id}: scientific hash absent")
    return failures


def summarize_case(payloads: list[dict]) -> dict:
    evaluations = [item["best_evaluation"] for item in payloads]
    objectives = [item["objective"] for item in evaluations]
    walls = [item["runner_wall_seconds"] for item in payloads]
    return {
        "repeat_count": len(payloads),
        "mean_objective": statistics.fmean(objectives),
        "standard_deviation_objective": statistics.stdev(objectives),
        "minimum_objective": min(objectives),
        "median_objective": statistics.median(objectives),
        "maximum_objective": max(objectives),
        "mean_expected_aep_mwh": statistics.fmean(
            item["expected_aep_mwh"] for item in evaluations
        ),
        "mean_aep_standard_deviation_mwh": statistics.fmean(
            item["aep_standard_deviation_mwh"] for item in evaluations
        ),
        "mean_cvar_mwh": statistics.fmean(
            item["cvar_mwh"] for item in evaluations
        ),
        "mean_minimum_sector_power_kw": statistics.fmean(
            item["minimum_sector_power_kw"] for item in evaluations
        ),
        "median_runner_wall_seconds": statistics.median(walls),
        "scientific_hashes": [
            item["scientific_hash"] for item in payloads
        ],
    }


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", required=True)
    parser.add_argument("--data", required=True)
    parser.add_argument("--output-root", required=True)
    parser.add_argument("--source-commit", required=True)
    parser.add_argument("--total-workers", type=int, default=20)
    parser.add_argument("--max-physical-fes", type=int, default=20032)
    parser.add_argument("--repeat-count", type=int, default=20)
    parser.add_argument("--seed-base", type=int, default=2026049900)
    args = parser.parse_args()
    require(args.total_workers == 20, "Waffle must use all twenty cores")
    require(
        args.max_physical_fes == 20032,
        "declared completed GA requires 20032 physical evaluations",
    )
    require(
        args.repeat_count == 20,
        "target paper requires twenty independent runs",
    )
    require(len(ALL_CASES) == 126, "paper case registry drift")

    root = Path(args.output_root)
    root.mkdir(parents=True, exist_ok=True)
    campaign_started = time.monotonic()
    h6 = h6_probe(args, root)
    tasks = [
        (case_index, case_id, repeat)
        for case_index, case_id in enumerate(ALL_CASES)
        for repeat in range(1, args.repeat_count + 1)
    ]
    results: dict[tuple[str, int], dict] = {}
    with ThreadPoolExecutor(max_workers=args.total_workers) as pool:
        futures = {}
        for case_index, case_id, repeat in tasks:
            seed = args.seed_base + case_index * 100 + repeat
            output = root / "formal" / case_id / f"repeat-{repeat:02d}.json"
            future = pool.submit(
                run_one,
                binary=args.binary,
                data=args.data,
                output=output,
                source_commit=args.source_commit,
                case_id=case_id,
                repeat=repeat,
                seed=seed,
                workers=1,
                max_physical_fes=args.max_physical_fes,
            )
            futures[future] = (case_id, repeat)
        completed_count = 0
        for future in as_completed(futures):
            case_id, repeat = futures[future]
            results[(case_id, repeat)] = future.result()
            completed_count += 1
            if completed_count % 100 == 0 or completed_count == len(tasks):
                print(
                    f"L0499 completed {completed_count}/{len(tasks)}",
                    flush=True,
                )

    failures: list[str] = []
    for case_id in ALL_CASES:
        for repeat in range(1, args.repeat_count + 1):
            failures.extend(
                validate_run(
                    results[(case_id, repeat)],
                    case_id,
                    args.max_physical_fes,
                )
            )
    case_summaries = {
        case_id: summarize_case([
            results[(case_id, repeat)]
            for repeat in range(1, args.repeat_count + 1)
        ])
        for case_id in ALL_CASES
    }
    summary = {
        "campaign":
            "L0499 Waffle H6 plus 126-case 20-repeat paper campaign",
        "generated_at_utc": datetime.now(timezone.utc).isoformat(),
        "source_commit": args.source_commit,
        "problem_record_count": 42,
        "objective_variant_count": 3,
        "case_count": len(ALL_CASES),
        "repeat_count_per_case": args.repeat_count,
        "formal_run_count": len(tasks),
        "physical_fes_per_run": args.max_physical_fes,
        "total_physical_fes": len(tasks) * args.max_physical_fes,
        "resource_mapping":
            "H6 uses one twenty-worker persistent population team; formal "
            "campaign uses twenty concurrent one-worker case/seed tasks; "
            "maximum aggregate workers is twenty",
        "maximum_aggregate_cpu_workers": args.total_workers,
        "h6": h6,
        "case_summaries": case_summaries,
        "campaign_wall_seconds": time.monotonic() - campaign_started,
        "status": "pass" if not failures else "fail",
        "failures": failures,
        "claim_boundary":
            "formal results for the declared source-backed reconstruction; "
            "not author NDAWN arrays, source, exact GA or numerical replay",
    }
    (root / "summary.json").write_text(
        json.dumps(summary, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    if failures:
        raise SystemExit("; ".join(failures))
    print(json.dumps(summary, sort_keys=True))


if __name__ == "__main__":
    main()
