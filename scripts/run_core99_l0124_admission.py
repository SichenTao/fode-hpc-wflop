#!/usr/bin/env python3
"""
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: resumable L0124 Waffle full-resource H6 and six-case,
five-repeat paper-native formal runner
Paper/DOI: Wind farm layout optimization using a Gaussian-based wake model;
10.1016/j.renene.2017.02.017
Public source/missing/reconstruction:
hpc/core99_cpp/include/core99/parada_l0124.hpp
Controlling contract: shared/contracts/core99_l0124_parada_2017.json
Claim boundary: academic declared reproduction; not author-source, author
random-state, or exact-layout replay
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
import subprocess
import time


CASES = [
    "l0124_case_a_grid10",
    "l0124_case_a_grid20",
    "l0124_case_b_grid10",
    "l0124_case_b_grid20",
    "l0124_case_c_grid10",
    "l0124_case_c_grid20",
]


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", required=True)
    parser.add_argument("--output-root", required=True)
    parser.add_argument("--source-commit", required=True)
    parser.add_argument("--workers-per-run", type=int, default=1)
    parser.add_argument("--concurrent-runs", type=int, default=20)
    parser.add_argument("--population", type=int, default=600)
    parser.add_argument("--generations", type=int, default=500)
    parser.add_argument("--repeat-count", type=int, default=5)
    parser.add_argument("--seed-base", type=int, default=2026073100)
    args = parser.parse_args()
    require(args.workers_per_run >= 1, "workers per run must be positive")
    require(args.concurrent_runs >= 1, "concurrent runs must be positive")
    require(args.population == 600, "paper-native population must be 600")
    require(args.generations == 500, "paper-native generations must be 500")
    require(args.repeat_count == 5, "paper-native repeat count must be five")

    root = Path(args.output_root)
    root.mkdir(parents=True, exist_ok=True)
    campaign_started = time.monotonic()
    tasks = [
        (case_id, repeat, args.seed_base + repeat + 1)
        for case_id in CASES
        for repeat in range(args.repeat_count)
    ]

    def execute(task: tuple[str, int, int]) -> tuple[str, int, dict]:
        case_id, repeat, seed = task
        case_root = root / case_id
        case_root.mkdir(parents=True, exist_ok=True)
        output = case_root / f"repeat-{repeat + 1:02d}.json"
        if output.exists():
            payload = json.loads(output.read_text(encoding="utf-8"))
            if (
                payload.get("scientific_hash")
                and payload.get("problem_id") == case_id
                and payload.get("physical_fes") == 300600
            ):
                return case_id, repeat, payload
        command = [
            args.binary,
            "--problem", case_id,
            "--workers", str(args.workers_per_run),
            "--population", str(args.population),
            "--generations", str(args.generations),
            "--seed", str(seed),
            "--output", str(output),
        ]
        started = time.monotonic()
        completed = subprocess.run(command, text=True, capture_output=True)
        if completed.returncode != 0:
            raise RuntimeError(
                f"{case_id} repeat {repeat + 1} failed: "
                f"{completed.stderr or completed.stdout}"
            )
        payload = json.loads(output.read_text(encoding="utf-8"))
        payload["runner_wall_seconds"] = time.monotonic() - started
        payload["source_commit"] = args.source_commit
        payload["formal_repeat"] = repeat + 1
        output.write_text(
            json.dumps(payload, indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
        )
        return case_id, repeat, payload

    results: dict[tuple[str, int], dict] = {}
    with ThreadPoolExecutor(max_workers=args.concurrent_runs) as pool:
        futures = [pool.submit(execute, task) for task in tasks]
        for future in as_completed(futures):
            case_id, repeat, payload = future.result()
            results[(case_id, repeat)] = payload
            print(
                f"L0124 completed {len(results)}/{len(tasks)}: "
                f"{case_id} repeat {repeat + 1}",
                flush=True,
            )

    failures: list[str] = []
    expected_fes = args.population * (args.generations + 1)
    for (case_id, repeat), payload in results.items():
        label = f"{case_id} repeat {repeat + 1}"
        if payload.get("problem_semantic_id") != (
            "l0124_parada_gaussian_grid_v1"
        ):
            failures.append(f"{label}: problem semantic ID mismatch")
        if payload.get("method_semantic_id") != (
            "l0124_mi_lxpm_target_survival_completed_v1"
        ):
            failures.append(f"{label}: method semantic ID mismatch")
        if payload.get("physical_fes") != expected_fes:
            failures.append(f"{label}: physical FES mismatch")
        best = payload.get("best_evaluation", {})
        if not best.get("feasible", False):
            failures.append(f"{label}: no feasible final incumbent")
        if not math.isfinite(best.get("objective", math.nan)):
            failures.append(f"{label}: non-finite objective")
        if payload.get("observed_workers", 0) < 1:
            failures.append(f"{label}: no worker participation")

    case_summaries = {}
    for case_id in CASES:
        ordered = [
            results[(case_id, repeat)]
            for repeat in range(args.repeat_count)
        ]
        objectives = sorted(
            item["best_evaluation"]["objective"] for item in ordered
        )
        powers = sorted(
            item["best_evaluation"]["expected_power_kw"] for item in ordered
        )
        case_summaries[case_id] = {
            "repeat_count": len(ordered),
            "minimum_objective": objectives[0],
            "median_objective": objectives[len(objectives) // 2],
            "maximum_objective": objectives[-1],
            "minimum_expected_power_kw": powers[0],
            "median_expected_power_kw": powers[len(powers) // 2],
            "maximum_expected_power_kw": powers[-1],
            "scientific_hashes": [
                item["scientific_hash"] for item in ordered
            ],
        }

    summary = {
        "campaign":
            "L0124 Waffle H6 plus six-case five-repeat formal run",
        "generated_at_utc": datetime.now(timezone.utc).isoformat(),
        "source_commit": args.source_commit,
        "problem_semantic_id": "l0124_parada_gaussian_grid_v1",
        "method_semantic_id": "l0124_mi_lxpm_target_survival_completed_v1",
        "workers_per_run": args.workers_per_run,
        "concurrent_runs": args.concurrent_runs,
        "maximum_requested_cpu_workers":
            args.workers_per_run * args.concurrent_runs,
        "resource_mapping":
            "paper-native independent case/repeat tasks are the outer "
            "full-resource axis",
        "case_count": len(CASES),
        "repeat_count_per_case": args.repeat_count,
        "formal_run_count": len(tasks),
        "population": args.population,
        "generations": args.generations,
        "physical_fes_per_run": expected_fes,
        "total_physical_fes": expected_fes * len(tasks),
        "campaign_wall_seconds": time.monotonic() - campaign_started,
        "case_summaries": case_summaries,
        "status": "pass" if not failures else "fail",
        "failures": failures,
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
