#!/usr/bin/env python3
"""
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: resumable L0341 Waffle H6 and ten-case 25-repeat formal
campaign runner
Paper/DOI: Tao et al.; 10.1016/j.renene.2020.06.003
Source/reconstruction/claim:
hpc/core99_cpp/include/core99/tao_l0341.hpp
Resource rule: H6 compares one and all twenty workers inside one complete
paper-budget optimization. Formal execution uses up to twenty concurrent
one-worker case/repeat tasks and no nested oversubscription.
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
    "l0341_uniform_wfa_a",
    "l0341_uniform_wfa_b",
    "l0341_uniform_wfa_c",
    "l0341_uniform_wfb_c",
    "l0341_uniform_wfc_c",
    "l0341_nonuniform_wfa_a",
    "l0341_nonuniform_wfa_b",
    "l0341_nonuniform_wfa_c",
    "l0341_nonuniform_wfb_c",
    "l0341_nonuniform_wfc_c",
)
FES = {
    "l0341_uniform_wfa_a": 15_301_020,
    "l0341_uniform_wfa_b": 15_301_020,
    "l0341_uniform_wfa_c": 15_301_020,
    "l0341_uniform_wfb_c": 27_301_820,
    "l0341_uniform_wfc_c": 36_302_420,
    "l0341_nonuniform_wfa_a": 30_001_500,
    "l0341_nonuniform_wfa_b": 30_001_500,
    "l0341_nonuniform_wfa_c": 30_001_500,
    "l0341_nonuniform_wfb_c": 54_002_700,
    "l0341_nonuniform_wfc_c": 72_003_600,
}
H6_CASE = "l0341_uniform_wfa_c"


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
    expected_fes = FES[case_id]
    if output.exists():
        payload = json.loads(output.read_text(encoding="utf-8"))
        if (
            payload.get("source_commit") == source_commit
            and payload.get("case_id") == case_id
            and payload.get("seed") == seed
            and payload.get("requested_workers") == workers
            and payload.get("physical_fes") == expected_fes
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
    if (
        payload.get("problem_semantic_id")
        != "l0341_three_farm_3d_gaussian_v1"
    ):
        failures.append(f"{case_id}: problem semantic ID mismatch")
    if (
        payload.get("method_semantic_id")
        != "l0341_mdpso_predecessor_completed_v1"
    ):
        failures.append(f"{case_id}: method semantic ID mismatch")
    if payload.get("physical_fes") != FES[case_id]:
        failures.append(f"{case_id}: physical-FES mismatch")
    if payload.get("requested_workers") != workers:
        failures.append(f"{case_id}: requested-worker mismatch")
    best = payload.get("best_evaluation", {})
    initial = payload.get("initial_best", {})
    if not best.get("feasible", False):
        failures.append(f"{case_id}: final layout infeasible")
    if not math.isfinite(best.get("expected_power_mw", math.nan)):
        failures.append(f"{case_id}: non-finite final power")
    if (
        best.get("expected_power_mw", -math.inf) + 1.0e-9
        < initial.get("expected_power_mw", math.inf)
    ):
        failures.append(f"{case_id}: final power regressed")
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
        "L0341 H6 semantic or numerical validation failed",
    )
    require(
        serial["scientific_hash"] == parallel["scientific_hash"],
        "L0341 H6 one/all-core scientific hash mismatch",
    )
    require(
        parallel["observed_workers"] == args.total_workers,
        "L0341 H6 did not activate all Waffle workers",
    )
    require(
        parallel["evaluator_seconds"] < serial["evaluator_seconds"]
        and parallel["algorithm_seconds"] < serial["algorithm_seconds"]
        and parallel["end_to_end_seconds"] < serial["end_to_end_seconds"],
        "L0341 all-core backend did not accelerate every declared component",
    )
    receipt = {
        "status": "pass",
        "case_id": H6_CASE,
        "physical_fes": FES[H6_CASE],
        "scientific_hash": serial["scientific_hash"],
        "serial": serial,
        "parallel": parallel,
        "evaluator_speedup":
            serial["evaluator_seconds"] / parallel["evaluator_seconds"],
        "algorithm_speedup":
            serial["algorithm_seconds"] / parallel["algorithm_seconds"],
        "end_to_end_speedup":
            serial["end_to_end_seconds"] / parallel["end_to_end_seconds"],
        "claim_boundary":
            "same pure-C++ source, paper case, seed, physical budget and "
            "scientific trajectory; one versus all twenty Waffle workers",
    }
    h6_root.mkdir(parents=True, exist_ok=True)
    (h6_root / "summary.json").write_text(
        json.dumps(receipt, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    return receipt


def summarize_case(payloads: list[dict]) -> dict:
    powers = [
        item["best_evaluation"]["expected_power_mw"] for item in payloads
    ]
    walls = [item["runner_wall_seconds"] for item in payloads]
    return {
        "repeat_count": len(payloads),
        "physical_fes_per_run": payloads[0]["physical_fes"],
        "mean_expected_power_mw": statistics.fmean(powers),
        "standard_deviation_expected_power_mw":
            statistics.stdev(powers) if len(powers) > 1 else 0.0,
        "minimum_expected_power_mw": min(powers),
        "median_expected_power_mw": statistics.median(powers),
        "maximum_expected_power_mw": max(powers),
        "mean_capacity_factor_percent": statistics.fmean(
            item["best_evaluation"]["capacity_factor_percent"]
            for item in payloads
        ),
        "mean_efficiency_percent": statistics.fmean(
            item["best_evaluation"]["efficiency_percent"]
            for item in payloads
        ),
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
    parser.add_argument("--seed-base", type=int, default=2026034100)
    args = parser.parse_args()
    require(args.total_workers == 20, "Waffle must use all twenty cores")
    require(args.repeat_count == 25, "formal protocol requires 25 seeds")
    require(len(CASES) == 10, "L0341 case registry drift")

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
            if completed_count % 25 == 0 or completed_count == len(tasks):
                print(
                    f"L0341 completed {completed_count}/{len(tasks)}",
                    flush=True,
                )

    failures: list[str] = []
    for case_id in CASES:
        for repeat in range(1, args.repeat_count + 1):
            failures.extend(
                validate_run(results[(case_id, repeat)], case_id, 1)
            )
    total_fes = sum(
        results[(case_id, repeat)]["physical_fes"]
        for case_id in CASES
        for repeat in range(1, args.repeat_count + 1)
    )
    summary = {
        "campaign":
            "L0341 Waffle H6 and ten-case 25-seed paper-budget campaign",
        "generated_at_utc": datetime.now(timezone.utc).isoformat(),
        "source_commit": args.source_commit,
        "case_count": len(CASES),
        "repeat_count_per_case": args.repeat_count,
        "formal_run_count": len(tasks),
        "total_physical_fes": total_fes,
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
            "formal results of the declared equation-backed reconstruction; "
            "not author source, private figures, exact curves or replay",
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
