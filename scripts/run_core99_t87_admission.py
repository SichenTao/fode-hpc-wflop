#!/usr/bin/env python3
"""
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: resumable T87 Waffle full-core H6 and four-case,
ten-seed declared formal runner
Paper/DOI: Wind Farm Layout Optimization in Complex Terrain Based on CFD and
IGA-PSO; 10.1016/j.energy.2023.129745
Public source/missing/reconstruction:
hpc/core99_cpp/include/core99/hu_t87.hpp
Controlling contract: shared/contracts/core99_t87_hu_iga_pso_2024.json
Claim boundary: academic declared reproduction on a figure-derived proxy;
not author CFD/data/source/random state/exact numerical replay
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
"""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import json
import math
from pathlib import Path
import subprocess
import time


CASES = [
    "t87_case1_jensen_aep",
    "t87_case2_gwm_aep",
    "t87_case3_dgwm_aep",
    "t87_case4_jensen_nav",
]


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", required=True)
    parser.add_argument("--data", required=True)
    parser.add_argument("--output-root", required=True)
    parser.add_argument("--source-commit", required=True)
    parser.add_argument("--workers", type=int, default=20)
    parser.add_argument("--iga-population", type=int, default=300)
    parser.add_argument("--iga-generations", type=int, default=1000)
    parser.add_argument("--pso-population", type=int, default=100)
    parser.add_argument("--pso-iterations", type=int, default=200)
    parser.add_argument("--repeat-count", type=int, default=10)
    parser.add_argument("--seed-base", type=int, default=2026078700)
    args = parser.parse_args()
    require(args.workers == 20, "Waffle paper runs must use all 20 cores")
    require(args.iga_population == 300, "paper IGA population must be 300")
    require(args.iga_generations == 1000, "paper IGA iterations must be 1000")
    require(args.pso_population == 100, "paper PSO population must be 100")
    require(args.pso_iterations == 200, "paper PSO iterations must be 200")
    require(
        args.repeat_count == 10,
        "declared uncertainty protocol requires ten seeds",
    )

    root = Path(args.output_root)
    root.mkdir(parents=True, exist_ok=True)
    campaign_started = time.monotonic()
    results: dict[tuple[str, int], dict] = {}
    tasks = len(CASES) * args.repeat_count
    completed_count = 0
    for case_index, case_id in enumerate(CASES):
        case_root = root / case_id
        case_root.mkdir(parents=True, exist_ok=True)
        for repeat in range(args.repeat_count):
            seed = args.seed_base + case_index * 100 + repeat + 1
            output = case_root / f"repeat-{repeat + 1:02d}.json"
            payload: dict
            if output.exists():
                payload = json.loads(output.read_text(encoding="utf-8"))
                reusable = (
                    payload.get("scientific_hash")
                    and payload.get("case_id") == case_id
                    and payload.get("proposed_fes") == 320400
                    and payload.get("requested_workers") == 20
                )
            else:
                reusable = False
            if not reusable:
                command = [
                    args.binary,
                    "--mode", "optimize",
                    "--case", case_id,
                    "--data", args.data,
                    "--workers", str(args.workers),
                    "--iga-population", str(args.iga_population),
                    "--iga-generations", str(args.iga_generations),
                    "--pso-population", str(args.pso_population),
                    "--pso-iterations", str(args.pso_iterations),
                    "--seed", str(seed),
                    "--output", str(output),
                ]
                started = time.monotonic()
                completed = subprocess.run(
                    command, text=True, capture_output=True
                )
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
            results[(case_id, repeat)] = payload
            completed_count += 1
            print(
                f"T87 completed {completed_count}/{tasks}: "
                f"{case_id} repeat {repeat + 1}",
                flush=True,
            )

    failures: list[str] = []
    for (case_id, repeat), payload in results.items():
        label = f"{case_id} repeat {repeat + 1}"
        if payload.get("problem_semantic_id") != (
            "t87_qianjiang_figure_proxy_v1"
        ):
            failures.append(f"{label}: problem semantic ID mismatch")
        if payload.get("method_semantic_id") != (
            "t87_iga_pso_predecessor_completed_v1"
        ):
            failures.append(f"{label}: method semantic ID mismatch")
        if payload.get("proposed_fes") != 320400:
            failures.append(f"{label}: proposed FES mismatch")
        if not 1 <= payload.get("physical_unique_fes", 0) <= 320400:
            failures.append(f"{label}: physical unique FES mismatch")
        if payload.get("observed_workers", 0) != 20:
            failures.append(f"{label}: all-core participation mismatch")
        for stage in ("best_grid_evaluation", "best_continuous_evaluation"):
            best = payload.get(stage, {})
            if not best.get("feasible", False):
                failures.append(f"{label}: {stage} infeasible")
            if not math.isfinite(best.get("fitness", math.nan)):
                failures.append(f"{label}: {stage} non-finite")
        if (
            payload["best_continuous_evaluation"]["fitness"] + 1.0e-8
            < payload["best_grid_evaluation"]["fitness"]
        ):
            failures.append(f"{label}: PSO regressed from IGA")

    case_summaries = {}
    for case_id in CASES:
        ordered = [
            results[(case_id, repeat)]
            for repeat in range(args.repeat_count)
        ]
        fitness = sorted(
            item["best_continuous_evaluation"]["fitness"]
            for item in ordered
        )
        aep = sorted(
            item["best_continuous_evaluation"]["aep_mwh"]
            for item in ordered
        )
        physical = sorted(item["physical_unique_fes"] for item in ordered)
        walls = sorted(item["end_to_end_seconds"] for item in ordered)
        case_summaries[case_id] = {
            "repeat_count": len(ordered),
            "minimum_fitness": fitness[0],
            "median_fitness": fitness[len(fitness) // 2],
            "maximum_fitness": fitness[-1],
            "minimum_aep_mwh": aep[0],
            "median_aep_mwh": aep[len(aep) // 2],
            "maximum_aep_mwh": aep[-1],
            "median_physical_unique_fes": physical[len(physical) // 2],
            "median_end_to_end_seconds": walls[len(walls) // 2],
            "scientific_hashes": [
                item["scientific_hash"] for item in ordered
            ],
        }

    summary = {
        "campaign":
            "T87 Waffle H6 plus four-case ten-seed declared formal run",
        "generated_at_utc": datetime.now(timezone.utc).isoformat(),
        "source_commit": args.source_commit,
        "problem_semantic_id": "t87_qianjiang_figure_proxy_v1",
        "method_semantic_id": "t87_iga_pso_predecessor_completed_v1",
        "workers_per_run": args.workers,
        "concurrent_runs": 1,
        "maximum_requested_cpu_workers": args.workers,
        "resource_mapping":
            "each paper-native IGA-PSO run uses all twenty Waffle cores; "
            "case/seed runs are sequential to avoid nested oversubscription",
        "case_count": len(CASES),
        "repeat_count_per_case": args.repeat_count,
        "formal_run_count": tasks,
        "iga_population": args.iga_population,
        "iga_generations": args.iga_generations,
        "pso_population": args.pso_population,
        "pso_iterations": args.pso_iterations,
        "proposed_fes_per_run": 320400,
        "total_proposed_fes": 320400 * tasks,
        "campaign_wall_seconds": time.monotonic() - campaign_started,
        "case_summaries": case_summaries,
        "status": "pass" if not failures else "fail",
        "failures": failures,
        "claim_boundary":
            "formal results for the declared figure-derived reproduction, "
            "not author CFD/data or exact numerical replay",
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
