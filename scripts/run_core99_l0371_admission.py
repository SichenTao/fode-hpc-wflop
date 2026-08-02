#!/usr/bin/env python3
"""
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: resumable L0371 Waffle H6 and 29-case, 30-repeat formal
campaign runner
Paper/DOI: Guo et al.; 10.1016/j.jweia.2021.104548
Source/reconstruction/claim:
hpc/core99_cpp/include/core99/guo_l0371.hpp
Resource rule: at most twenty aggregate Waffle CPU workers; the actual
504-state case uses one 20-worker inner team, while light cases use one worker
per run and up to twenty independent runs concurrently.
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


STABILITIES = ("vu", "u", "nu", "n", "ns", "s", "vs")
IDEAL_CASES = tuple(
    f"l0371_ideal_{wind}_{stability}"
    for wind in ("a", "b", "c")
    for stability in STABILITIES
)
HORNS_SINGLE_CASES = tuple(
    f"l0371_horns_{stability}" for stability in STABILITIES
)
ACTUAL_CASE = "l0371_horns_actual"
ALL_CASES = (*IDEAL_CASES, *HORNS_SINGLE_CASES, ACTUAL_CASE)


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
            case_id=ACTUAL_CASE,
            repeat=0,
            seed=args.seed_base - 1,
            workers=workers,
            max_physical_fes=args.h6_physical_fes,
        )
    serial = results[1]
    parallel = results[args.total_workers]
    require(
        serial["scientific_hash"] == parallel["scientific_hash"],
        "L0371 H6 one/all-core scientific hash mismatch",
    )
    require(
        parallel["observed_workers"] == args.total_workers,
        "L0371 H6 did not activate every Waffle worker",
    )
    def ratio(field: str) -> float:
        return serial[field] / parallel[field]

    serial_total = (
        serial["precomputation_seconds"] + serial["end_to_end_seconds"]
    )
    parallel_total = (
        parallel["precomputation_seconds"] + parallel["end_to_end_seconds"]
    )
    receipt = {
        "status": "pass",
        "case_id": ACTUAL_CASE,
        "physical_fes": args.h6_physical_fes,
        "serial_workers": 1,
        "parallel_workers": args.total_workers,
        "scientific_hash": serial["scientific_hash"],
        "precomputation_speedup": ratio("precomputation_seconds"),
        "evaluator_speedup": ratio("evaluator_seconds"),
        "optimization_speedup": ratio("end_to_end_seconds"),
        "cli_total_speedup": serial_total / parallel_total,
        "serial": serial,
        "parallel": parallel,
        "claim_boundary":
            "same pure-C++ source and scientific trajectory; one versus "
            "all twenty Waffle workers on the heavy actual-stability case",
    }
    (h6_root / "summary.json").write_text(
        json.dumps(receipt, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    return receipt


def validate_run(
    payload: dict,
    case_id: str,
    max_physical_fes: int,
    workers: int,
) -> list[str]:
    failures: list[str] = []
    if payload.get("case_id") != case_id:
        failures.append(f"{case_id}: case ID mismatch")
    if payload.get("method_semantic_id") != (
        "l0371_deem_predecessor_completed_v1"
    ):
        failures.append(f"{case_id}: method semantic ID mismatch")
    expected_problem = (
        "l0371_horns_ietgwm_grid_proxy_v1"
        if case_id.startswith("l0371_horns_")
        else "l0371_ideal_ietgwm_grid_v1"
    )
    if payload.get("problem_semantic_id") != expected_problem:
        failures.append(f"{case_id}: problem semantic ID mismatch")
    if payload.get("physical_fes") != max_physical_fes:
        failures.append(f"{case_id}: physical FES mismatch")
    if payload.get("requested_workers") != workers:
        failures.append(f"{case_id}: worker request mismatch")
    best = payload.get("best_evaluation", {})
    initial = payload.get("initial_evaluation", {})
    if not best.get("feasible", False):
        failures.append(f"{case_id}: infeasible final layout")
    if not math.isfinite(best.get("average_power_kw", math.nan)):
        failures.append(f"{case_id}: non-finite final power")
    if (
        best.get("average_power_kw", -math.inf) + 1.0e-9
        < initial.get("average_power_kw", math.inf)
    ):
        failures.append(f"{case_id}: final power regressed")
    if not payload.get("scientific_hash"):
        failures.append(f"{case_id}: scientific hash absent")
    return failures


def summarize_case(payloads: list[dict]) -> dict:
    powers = [item["best_evaluation"]["average_power_kw"] for item in payloads]
    efficiencies = [item["best_evaluation"]["efficiency"] for item in payloads]
    coe = [item["best_evaluation"]["coe"] for item in payloads]
    walls = [item["runner_wall_seconds"] for item in payloads]
    trials = [item["proposed_trials"] for item in payloads]
    return {
        "repeat_count": len(payloads),
        "mean_power_kw": statistics.fmean(powers),
        "standard_deviation_power_kw": statistics.stdev(powers),
        "minimum_power_kw": min(powers),
        "median_power_kw": statistics.median(powers),
        "maximum_power_kw": max(powers),
        "mean_efficiency": statistics.fmean(efficiencies),
        "mean_coe": statistics.fmean(coe),
        "median_runner_wall_seconds": statistics.median(walls),
        "median_proposed_trials": statistics.median(trials),
        "scientific_hashes": [item["scientific_hash"] for item in payloads],
    }


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", required=True)
    parser.add_argument("--data", required=True)
    parser.add_argument("--output-root", required=True)
    parser.add_argument("--source-commit", required=True)
    parser.add_argument("--total-workers", type=int, default=20)
    parser.add_argument("--max-physical-fes", type=int, default=150000)
    parser.add_argument("--repeat-count", type=int, default=30)
    parser.add_argument("--seed-base", type=int, default=2026371000)
    parser.add_argument("--h6-physical-fes", type=int, default=5000)
    args = parser.parse_args()
    require(args.total_workers == 20, "Waffle must use all twenty cores")
    require(
        args.max_physical_fes == 150000,
        "direct DEEM predecessor requires 150000 physical evaluations",
    )
    require(
        args.repeat_count == 30,
        "direct DEEM predecessor requires 30 independent runs",
    )
    require(len(ALL_CASES) == 29, "paper case registry drift")

    root = Path(args.output_root)
    root.mkdir(parents=True, exist_ok=True)
    campaign_started = time.monotonic()
    h6 = h6_probe(args, root)
    results: dict[tuple[str, int], dict] = {}
    light_tasks = [
        (case_index, case_id, repeat)
        for case_index, case_id in enumerate(ALL_CASES[:-1])
        for repeat in range(1, args.repeat_count + 1)
    ]
    completed_count = 0
    total_count = len(ALL_CASES) * args.repeat_count
    with ThreadPoolExecutor(max_workers=args.total_workers) as pool:
        futures = {}
        for case_index, case_id, repeat in light_tasks:
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
        for future in as_completed(futures):
            case_id, repeat = futures[future]
            results[(case_id, repeat)] = future.result()
            completed_count += 1
            if completed_count % 20 == 0 or completed_count == len(light_tasks):
                print(
                    f"L0371 completed {completed_count}/{total_count}",
                    flush=True,
                )

    actual_index = len(ALL_CASES) - 1
    for repeat in range(1, args.repeat_count + 1):
        seed = args.seed_base + actual_index * 100 + repeat
        output = (
            root / "formal" / ACTUAL_CASE / f"repeat-{repeat:02d}.json"
        )
        results[(ACTUAL_CASE, repeat)] = run_one(
            binary=args.binary,
            data=args.data,
            output=output,
            source_commit=args.source_commit,
            case_id=ACTUAL_CASE,
            repeat=repeat,
            seed=seed,
            workers=args.total_workers,
            max_physical_fes=args.max_physical_fes,
        )
        completed_count += 1
        print(
            f"L0371 completed {completed_count}/{total_count}: "
            f"actual repeat {repeat}",
            flush=True,
        )

    failures: list[str] = []
    for case_id in ALL_CASES:
        workers = args.total_workers if case_id == ACTUAL_CASE else 1
        for repeat in range(1, args.repeat_count + 1):
            failures.extend(
                validate_run(
                    results[(case_id, repeat)],
                    case_id,
                    args.max_physical_fes,
                    workers,
                )
            )
    case_summaries = {
        case_id: summarize_case(
            [
                results[(case_id, repeat)]
                for repeat in range(1, args.repeat_count + 1)
            ]
        )
        for case_id in ALL_CASES
    }
    summary = {
        "campaign":
            "L0371 Waffle H6 plus 29-case 30-repeat paper-scale campaign",
        "generated_at_utc": datetime.now(timezone.utc).isoformat(),
        "source_commit": args.source_commit,
        "case_count": len(ALL_CASES),
        "repeat_count_per_case": args.repeat_count,
        "formal_run_count": total_count,
        "physical_fes_per_run": args.max_physical_fes,
        "total_physical_fes":
            total_count * args.max_physical_fes,
        "resource_mapping":
            "28 light cases use one worker per run and at most twenty "
            "concurrent runs; the 504-state actual case uses one twenty-"
            "worker persistent team; no nested oversubscription",
        "maximum_aggregate_cpu_workers": args.total_workers,
        "h6": h6,
        "case_summaries": case_summaries,
        "campaign_wall_seconds": time.monotonic() - campaign_started,
        "status": "pass" if not failures else "fail",
        "failures": failures,
        "claim_boundary":
            "formal results for the declared source-backed reproduction, "
            "not author arrays, exact grid, source, seeds or numerical replay",
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
