#!/usr/bin/env python3
"""
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: resumable L0623 Waffle H6 and three-case 25-repeat
formal campaign runner
Paper/DOI: Wang et al.; 10.1016/j.oceaneng.2023.116644
Source/reconstruction/claim:
hpc/core99_cpp/include/core99/wang_l0623.hpp
Resource rule: H6 compares one and all twenty workers inside complete Case I.
Formal execution uses twenty concurrent one-worker case/repeat tasks and no
nested oversubscription.
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


CASES = (
    "l0623_case1_flat_single",
    "l0623_case2_flat_windrose",
    "l0623_case3_hill_windrose",
)
PROBLEM_IDS = {
    CASES[0]: "l0623_case1_flat_single_proxy_v1",
    CASES[1]: "l0623_case2_flat_windrose_proxy_v1",
    CASES[2]: "l0623_case3_gaussian_hill_windrose_proxy_v1",
}
TRUTH_CALLS = {CASES[0]: 437, CASES[1]: 400, CASES[2]: 399}
H6_CASE = CASES[0]


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


def run_one(
    *,
    binary: str,
    output: Path,
    source_commit: str,
    case_id: str,
    repeat: int,
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
            and payload.get("truth_calls") == TRUTH_CALLS[case_id]
        ):
            return payload
    started = time.monotonic()
    completed = subprocess.run(
        [
            binary,
            "--mode", "optimize",
            "--case", case_id,
            "--workers", str(workers),
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


def validate_run(payload: dict, case_id: str, workers: int) -> list[str]:
    failures: list[str] = []
    if payload.get("case_id") != case_id:
        failures.append(f"{case_id}: case ID mismatch")
    if payload.get("problem_semantic_id") != PROBLEM_IDS[case_id]:
        failures.append(f"{case_id}: problem semantic ID mismatch")
    if (
        payload.get("method_semantic_id")
        != "l0623_adaptive_kriging_ga_completed_v1"
    ):
        failures.append(f"{case_id}: method semantic ID mismatch")
    if payload.get("initial_samples") != 360:
        failures.append(f"{case_id}: LHS sample count mismatch")
    if payload.get("truth_calls") != TRUTH_CALLS[case_id]:
        failures.append(f"{case_id}: truth-call count mismatch")
    if payload.get("requested_workers") != workers:
        failures.append(f"{case_id}: requested-worker mismatch")
    if payload.get("surrogate_fes", 0) <= 0:
        failures.append(f"{case_id}: no surrogate FEs")
    best = payload.get("best_evaluation", {})
    initial = payload.get("initial_best", {})
    if not best.get("feasible", False):
        failures.append(f"{case_id}: final layout infeasible")
    if not math.isfinite(best.get("aep_gwh", math.nan)):
        failures.append(f"{case_id}: non-finite final AEP")
    if best.get("aep_gwh", -math.inf) + 1.0e-9 < initial.get(
        "aep_gwh", math.inf
    ):
        failures.append(f"{case_id}: final AEP regressed")
    if not payload.get("scientific_hash"):
        failures.append(f"{case_id}: scientific hash absent")
    return failures


def h6_probe(args: argparse.Namespace, root: Path) -> dict:
    h6_root = root / "h6"
    results = {}
    for workers in (1, args.total_workers):
        results[workers] = run_one(
            binary=args.binary,
            output=h6_root / f"workers-{workers:02d}.json",
            source_commit=args.source_commit,
            case_id=H6_CASE,
            repeat=0,
            seed=args.seed_base - 1,
            workers=workers,
        )
    serial = results[1]
    parallel = results[args.total_workers]
    require(
        not validate_run(serial, H6_CASE, 1)
        and not validate_run(parallel, H6_CASE, args.total_workers),
        "L0623 H6 semantic or numerical validation failed",
    )
    require(
        serial["scientific_hash"] == parallel["scientific_hash"]
        and serial["surrogate_fes"] == parallel["surrogate_fes"],
        "L0623 H6 one/all-core trajectory mismatch",
    )
    require(
        parallel["observed_workers"] == args.total_workers,
        "L0623 H6 did not activate all Waffle workers",
    )
    require(
        parallel["surrogate_inference_seconds"]
        < serial["surrogate_inference_seconds"]
        and parallel["end_to_end_seconds"] < serial["end_to_end_seconds"],
        "L0623 all-core inference/end-to-end did not accelerate",
    )
    receipt = {
        "status": "pass",
        "case_id": H6_CASE,
        "truth_calls": TRUTH_CALLS[H6_CASE],
        "surrogate_fes": serial["surrogate_fes"],
        "scientific_hash": serial["scientific_hash"],
        "serial": serial,
        "parallel": parallel,
        "truth_proxy_speedup":
            serial["truth_evaluator_seconds"]
            / parallel["truth_evaluator_seconds"],
        "kriging_training_speedup":
            serial["surrogate_training_seconds"]
            / parallel["surrogate_training_seconds"],
        "kriging_inference_speedup":
            serial["surrogate_inference_seconds"]
            / parallel["surrogate_inference_seconds"],
        "end_to_end_speedup":
            serial["end_to_end_seconds"] / parallel["end_to_end_seconds"],
        "claim_boundary":
            "same pure-C++ source, proxy problem, seed, truth/surrogate "
            "budgets and scientific trajectory; one versus all twenty "
            "Waffle workers; no OpenFOAM CFD performance claim",
    }
    h6_root.mkdir(parents=True, exist_ok=True)
    (h6_root / "summary.json").write_text(
        json.dumps(receipt, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    return receipt


def summarize_case(payloads: list[dict]) -> dict:
    values = [item["best_evaluation"]["aep_gwh"] for item in payloads]
    walls = [item["runner_wall_seconds"] for item in payloads]
    return {
        "repeat_count": len(payloads),
        "truth_calls_per_run": payloads[0]["truth_calls"],
        "mean_surrogate_fes": statistics.fmean(
            item["surrogate_fes"] for item in payloads
        ),
        "mean_aep_gwh": statistics.fmean(values),
        "standard_deviation_aep_gwh":
            statistics.stdev(values) if len(values) > 1 else 0.0,
        "minimum_aep_gwh": min(values),
        "median_aep_gwh": statistics.median(values),
        "maximum_aep_gwh": max(values),
        "median_runner_wall_seconds": statistics.median(walls),
        "scientific_hashes": [
            item["scientific_hash"] for item in payloads
        ],
    }


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", required=True)
    parser.add_argument("--output-root", required=True)
    parser.add_argument("--source-commit", required=True)
    parser.add_argument("--total-workers", type=int, default=20)
    parser.add_argument("--repeat-count", type=int, default=25)
    parser.add_argument("--seed-base", type=int, default=2026062300)
    args = parser.parse_args()
    require(args.total_workers == 20, "Waffle must use all twenty cores")
    require(args.repeat_count == 25, "formal protocol requires 25 seeds")

    root = Path(args.output_root)
    root.mkdir(parents=True, exist_ok=True)
    campaign_started = time.monotonic()
    h6 = h6_probe(args, root)
    tasks = [
        (case_index, case_id, repeat)
        for case_index, case_id in enumerate(CASES)
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
                output=output,
                source_commit=args.source_commit,
                case_id=case_id,
                repeat=repeat,
                seed=seed,
                workers=1,
            )
            futures[future] = (case_id, repeat)
        completed_count = 0
        for future in as_completed(futures):
            case_id, repeat = futures[future]
            results[(case_id, repeat)] = future.result()
            completed_count += 1
            if completed_count % 15 == 0 or completed_count == len(tasks):
                print(
                    f"L0623 completed {completed_count}/{len(tasks)}",
                    flush=True,
                )

    failures: list[str] = []
    for case_id in CASES:
        for repeat in range(1, args.repeat_count + 1):
            failures.extend(
                validate_run(results[(case_id, repeat)], case_id, 1)
            )
    summary = {
        "campaign":
            "L0623 Waffle H6 and three-case 25-seed truth-budget campaign",
        "generated_at_utc": datetime.now(timezone.utc).isoformat(),
        "source_commit": args.source_commit,
        "case_count": len(CASES),
        "repeat_count_per_case": args.repeat_count,
        "formal_run_count": len(tasks),
        "total_truth_calls": sum(
            results[(case_id, repeat)]["truth_calls"]
            for case_id in CASES
            for repeat in range(1, args.repeat_count + 1)
        ),
        "total_surrogate_fes": sum(
            results[(case_id, repeat)]["surrogate_fes"]
            for case_id in CASES
            for repeat in range(1, args.repeat_count + 1)
        ),
        "maximum_aggregate_cpu_workers": args.total_workers,
        "resource_mapping":
            "H6 uses one/all-twenty persistent teams; formal uses twenty "
            "concurrent one-worker case/seed processes without nested "
            "oversubscription",
        "h6": h6,
        "case_summaries": {
            case_id: summarize_case([
                results[(case_id, repeat)]
                for repeat in range(1, args.repeat_count + 1)
            ])
            for case_id in CASES
        },
        "campaign_wall_seconds": time.monotonic() - campaign_started,
        "status": "pass" if not failures else "fail",
        "failures": failures,
        "claim_boundary":
            "formal results for the declared ADM/Gaussian proxy; not author "
            "OpenFOAM CFD, source, meshes, responses or numerical replay",
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
