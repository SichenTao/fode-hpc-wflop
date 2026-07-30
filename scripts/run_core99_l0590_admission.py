#!/usr/bin/env python3
"""
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: resumable L0590 from-scratch training, Waffle H6 and
eight-case 25-repeat formal campaign runner
Paper/DOI: Sun and Yang; 10.1016/j.apenergy.2023.121554
Source/reconstruction/claim:
hpc/core99_cpp/include/core99/sun_l0590.hpp
Resource rule: H6 compares one and all twenty workers for both training and
E4 optimization. Formal optimization uses up to twenty concurrent one-worker
case/repeat processes with one frozen from-scratch trained surrogate and no
nested oversubscription.
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
"""
from __future__ import annotations

import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed
from datetime import datetime, timezone
import hashlib
import json
import math
from pathlib import Path
import statistics
import subprocess
import time


CASES = (
    "l0590_e1", "l0590_e2", "l0590_e3", "l0590_e4",
    "l0590_c1", "l0590_c2", "l0590_c3", "l0590_c4",
)
GENERATIONS = {
    "l0590_e1": 0,
    "l0590_e2": 838,
    "l0590_e3": 838,
    "l0590_e4": 838,
    "l0590_c1": 0,
    "l0590_c2": 1017,
    "l0590_c3": 838,
    "l0590_c4": 1017,
}


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


def train(
    *,
    binary: str,
    weights: Path,
    output: Path,
    source_commit: str,
    workers: int,
    seed: int,
    sample_count: int,
    maximum_epochs: int,
) -> dict:
    output.parent.mkdir(parents=True, exist_ok=True)
    if output.exists() and weights.exists():
        payload = json.loads(output.read_text(encoding="utf-8"))
        if (
            payload.get("source_commit") == source_commit
            and payload.get("requested_workers") == workers
            and payload.get("seed") == seed
            and payload.get("train_count", 0)
                + payload.get("validation_count", 0)
                + payload.get("test_count", 0) == sample_count
        ):
            return payload
    completed = subprocess.run(
        [
            binary,
            "--mode", "train",
            "--weights", str(weights),
            "--output", str(output),
            "--workers", str(workers),
            "--seed", str(seed),
            "--sample-count", str(sample_count),
            "--maximum-epochs", str(maximum_epochs),
            "--target-mse", "1e-6",
        ],
        text=True,
        capture_output=True,
    )
    if completed.returncode != 0:
        raise RuntimeError(completed.stderr or completed.stdout)
    payload = json.loads(output.read_text(encoding="utf-8"))
    payload["source_commit"] = source_commit
    payload["weights_sha256"] = hashlib.sha256(
        weights.read_bytes()
    ).hexdigest()
    output.write_text(
        json.dumps(payload, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    return payload


def run_one(
    *,
    binary: str,
    weights: Path,
    output: Path,
    source_commit: str,
    weights_sha256: str,
    case_id: str,
    repeat: int,
    seed: int,
    workers: int,
) -> dict:
    output.parent.mkdir(parents=True, exist_ok=True)
    generations = GENERATIONS[case_id]
    expected_fes = 1 if generations == 0 else 64 * (generations + 1)
    if output.exists():
        payload = json.loads(output.read_text(encoding="utf-8"))
        if (
            payload.get("source_commit") == source_commit
            and payload.get("weights_sha256") == weights_sha256
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
            "--weights", str(weights),
            "--workers", str(workers),
            "--generations", str(generations),
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
    payload["weights_sha256"] = weights_sha256
    payload["formal_repeat"] = repeat
    payload["runner_wall_seconds"] = time.monotonic() - started
    output.write_text(
        json.dumps(payload, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    return payload


def h6_probe(args: argparse.Namespace, root: Path) -> tuple[dict, Path, str]:
    h6_root = root / "h6"
    h6_root.mkdir(parents=True, exist_ok=True)
    training = {}
    weight_paths = {}
    for workers in (1, args.total_workers):
        weights = h6_root / f"weights-workers-{workers:02d}.bin"
        output = h6_root / f"training-workers-{workers:02d}.json"
        training[workers] = train(
            binary=args.binary,
            weights=weights,
            output=output,
            source_commit=args.source_commit,
            workers=workers,
            seed=args.training_seed,
            sample_count=args.sample_count,
            maximum_epochs=args.maximum_epochs,
        )
        weight_paths[workers] = weights
    require(
        training[1]["scientific_hash"]
        == training[args.total_workers]["scientific_hash"],
        "L0590 H6 one/all-core training scientific hash mismatch",
    )
    require(
        training[1]["weights_sha256"]
        == training[args.total_workers]["weights_sha256"],
        "L0590 H6 one/all-core trained weight mismatch",
    )
    require(
        training[args.total_workers]["observed_workers"]
        == args.total_workers,
        "L0590 H6 training did not activate every Waffle worker",
    )
    optimization = {}
    for workers in (1, args.total_workers):
        output = h6_root / f"e4-workers-{workers:02d}.json"
        optimization[workers] = run_one(
            binary=args.binary,
            weights=weight_paths[args.total_workers],
            output=output,
            source_commit=args.source_commit,
            weights_sha256=training[args.total_workers]["weights_sha256"],
            case_id="l0590_e4",
            repeat=0,
            seed=args.seed_base - 1,
            workers=workers,
        )
    serial = optimization[1]
    parallel = optimization[args.total_workers]
    require(
        serial["scientific_hash"] == parallel["scientific_hash"],
        "L0590 H6 one/all-core optimization scientific hash mismatch",
    )
    require(
        parallel["observed_workers"] == args.total_workers,
        "L0590 H6 optimization did not activate every Waffle worker",
    )
    require(
        parallel["evaluator_seconds"] < serial["evaluator_seconds"],
        "L0590 all-core evaluator did not accelerate",
    )
    require(
        parallel["end_to_end_seconds"] < serial["end_to_end_seconds"],
        "L0590 all-core optimization did not accelerate",
    )
    receipt = {
        "status": "pass",
        "training": {
            "sample_count": args.sample_count,
            "maximum_epochs": args.maximum_epochs,
            "paper_target_mse": 1e-6,
            "paper_target_reached":
                training[args.total_workers]["validation_mse"] <= 1e-6,
            "serial": training[1],
            "parallel": training[args.total_workers],
            "speedup":
                training[1]["seconds"]
                / training[args.total_workers]["seconds"],
        },
        "optimization": {
            "case_id": "l0590_e4",
            "physical_fes": 64 * 839,
            "scientific_hash": serial["scientific_hash"],
            "serial": serial,
            "parallel": parallel,
            "evaluator_speedup":
                serial["evaluator_seconds"]
                / parallel["evaluator_seconds"],
            "optimization_speedup":
                serial["end_to_end_seconds"]
                / parallel["end_to_end_seconds"],
        },
        "claim_boundary":
            "same pure-C++ source, training samples, seeds, physical "
            "budget and scientific trajectories; one versus all twenty "
            "Waffle workers",
    }
    (h6_root / "summary.json").write_text(
        json.dumps(receipt, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    return (
        receipt,
        weight_paths[args.total_workers],
        training[args.total_workers]["weights_sha256"],
    )


def validate_run(payload: dict, case_id: str) -> list[str]:
    failures: list[str] = []
    expected_fes = (
        1 if GENERATIONS[case_id] == 0
        else 64 * (GENERATIONS[case_id] + 1)
    )
    if payload.get("case_id") != case_id:
        failures.append(f"{case_id}: case ID mismatch")
    if payload.get("problem_semantic_id") != (
        "l0590_shiren_3d_ann_layout_height_v1"
    ):
        failures.append(f"{case_id}: problem semantic ID mismatch")
    if payload.get("method_semantic_id") != "l0590_real_ga_completed_v1":
        failures.append(f"{case_id}: method semantic ID mismatch")
    if payload.get("physical_fes") != expected_fes:
        failures.append(f"{case_id}: physical FES mismatch")
    best = payload.get("best_evaluation", {})
    initial = payload.get("initial_best", {})
    if not best.get("feasible", False):
        failures.append(f"{case_id}: infeasible final layout")
    if not math.isfinite(best.get("objective", math.nan)):
        failures.append(f"{case_id}: non-finite objective")
    if best.get("objective", -math.inf) + 1e-9 < initial.get(
        "objective", math.inf
    ):
        failures.append(f"{case_id}: final objective regressed")
    if not payload.get("scientific_hash"):
        failures.append(f"{case_id}: scientific hash absent")
    return failures


def summarize_case(payloads: list[dict]) -> dict:
    objectives = [
        item["best_evaluation"]["objective"] for item in payloads
    ]
    powers = [
        item["best_evaluation"]["total_power_kw"] for item in payloads
    ]
    costs = [
        item["best_evaluation"]["cost_of_power_usd_per_kw"]
        for item in payloads
    ]
    walls = [item["runner_wall_seconds"] for item in payloads]
    return {
        "repeat_count": len(payloads),
        "mean_objective": statistics.fmean(objectives),
        "standard_deviation_objective":
            statistics.stdev(objectives) if len(objectives) > 1 else 0.0,
        "minimum_objective": min(objectives),
        "median_objective": statistics.median(objectives),
        "maximum_objective": max(objectives),
        "mean_total_power_kw": statistics.fmean(powers),
        "mean_cost_of_power_usd_per_kw": statistics.fmean(costs),
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
    parser.add_argument("--sample-count", type=int, default=32768)
    parser.add_argument("--maximum-epochs", type=int, default=1000)
    parser.add_argument("--training-seed", type=int, default=2026059001)
    parser.add_argument("--seed-base", type=int, default=2026059000)
    args = parser.parse_args()
    require(args.total_workers == 20, "Waffle must use all twenty cores")
    require(args.repeat_count == 25, "missing paper repeats use 25-run policy")
    require(args.maximum_epochs == 1000, "paper maximum epoch drift")
    require(args.sample_count == 32768, "formal training sample drift")

    root = Path(args.output_root)
    root.mkdir(parents=True, exist_ok=True)
    campaign_started = time.monotonic()
    h6, weights, weights_sha256 = h6_probe(args, root)
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
                weights=weights,
                output=output,
                source_commit=args.source_commit,
                weights_sha256=weights_sha256,
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
                    f"L0590 completed {completed_count}/{len(tasks)}",
                    flush=True,
                )

    failures: list[str] = []
    for case_id in CASES:
        for repeat in range(1, args.repeat_count + 1):
            failures.extend(validate_run(results[(case_id, repeat)], case_id))
    total_fes = sum(
        results[(case_id, repeat)]["physical_fes"]
        for case_id in CASES
        for repeat in range(1, args.repeat_count + 1)
    )
    summary = {
        "campaign":
            "L0590 Waffle training, H6 and eight-case 25-repeat campaign",
        "generated_at_utc": datetime.now(timezone.utc).isoformat(),
        "source_commit": args.source_commit,
        "case_count": len(CASES),
        "repeat_count_per_case": args.repeat_count,
        "formal_run_count": len(tasks),
        "total_physical_fes": total_fes,
        "maximum_aggregate_cpu_workers": args.total_workers,
        "resource_mapping":
            "H6 uses one/all-twenty persistent teams; formal uses twenty "
            "concurrent one-worker case/seed processes and no nested "
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
            "formal results for the declared equation-backed reconstruction; "
            "not author data, weights, exact GA, cost curve or numerical replay",
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
