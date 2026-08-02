#!/usr/bin/env python3
"""
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: resumable T74 Waffle H6 and 15-case 30-repeat campaign
Paper/source/reconstruction: hpc/core99_cpp/include/core99/ju_t74.hpp
Resource rule: one process owns one persistent all-twenty-core team; paper
cases execute sequentially, MARS artifacts are trained once per case and
reused across that case's 30 independent optimization repeats.
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
    f"t74_case{wind_case}_{size}"
    for wind_case in range(1, 6)
    for size in ("small", "medium", "large")
)
H6_CASE = "t74_case5_medium"


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


def execute(
    binary: str,
    output: Path,
    arguments: list[str],
    source_commit: str,
) -> dict:
    output.parent.mkdir(parents=True, exist_ok=True)
    if output.exists():
        payload = json.loads(output.read_text(encoding="utf-8"))
        if payload.get("source_commit") == source_commit:
            return payload
    temporary = output.with_suffix(".tmp")
    started = time.monotonic()
    completed = subprocess.run(
        [binary, *arguments, "--output", str(temporary)],
        text=True,
        capture_output=True,
        timeout=12 * 60 * 60,
    )
    if completed.returncode != 0:
        raise RuntimeError(completed.stderr or completed.stdout)
    payload = json.loads(temporary.read_text(encoding="utf-8"))
    payload["source_commit"] = source_commit
    payload["runner_wall_seconds"] = time.monotonic() - started
    output.write_text(
        json.dumps(payload, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    temporary.unlink()
    return payload


def validate_run(run: dict, case_id: str, expected_fes: int) -> list[str]:
    failures: list[str] = []
    if run.get("case_id") != case_id:
        failures.append(f"{case_id}: case ID mismatch")
    if run.get("method_semantic_id") != "t74_siga_paper_probability_v1":
        failures.append(f"{case_id}: primary method identity mismatch")
    if run.get("population") != 100 or run.get("generations") != 200:
        failures.append(f"{case_id}: paper population/generation mismatch")
    if run.get("physical_fes") != expected_fes:
        failures.append(f"{case_id}: physical FES mismatch")
    if run.get("requested_workers") != 20:
        failures.append(f"{case_id}: all-core request mismatch")
    if run.get("observed_workers") != 20:
        failures.append(f"{case_id}: all-core team not observed")
    best = run.get("best_evaluation", {})
    initial = run.get("initial_best", {})
    if not best.get("feasible", False):
        failures.append(f"{case_id}: final layout infeasible")
    if not math.isfinite(best.get("efficiency_percent", math.nan)):
        failures.append(f"{case_id}: final efficiency non-finite")
    if best.get("efficiency_percent", -math.inf) < initial.get(
        "efficiency_percent", math.inf
    ) - 1.0e-12:
        failures.append(f"{case_id}: retained best regressed")
    if len(run.get("best_efficiency_history_percent", [])) != 200:
        failures.append(f"{case_id}: convergence history incomplete")
    if not run.get("scientific_hash"):
        failures.append(f"{case_id}: scientific hash absent")
    return failures


def run_h6(args: argparse.Namespace, root: Path) -> dict:
    rows = {}
    for workers in (1, args.total_workers):
        rows[workers] = execute(
            args.binary,
            root / "h6" / f"workers-{workers:02d}.json",
            [
                "--mode", "optimize",
                "--case", H6_CASE,
                "--variant", "paper_probability",
                "--workers", str(workers),
                "--seed", str(args.seed_base - 1),
            ],
            args.source_commit,
        )["runs"][0]
    serial = rows[1]
    parallel = rows[args.total_workers]
    require(
        serial["scientific_hash"] == parallel["scientific_hash"],
        "T74 H6 one/all-core scientific trajectory mismatch",
    )
    require(
        serial["physical_fes"] == 30000
        and parallel["physical_fes"] == 30000,
        "T74 H6 paper work mismatch",
    )
    require(
        parallel["observed_workers"] == args.total_workers,
        "T74 H6 did not activate all Waffle cores",
    )
    require(
        parallel["monte_carlo_truth_seconds"]
            < serial["monte_carlo_truth_seconds"]
        and parallel["population_truth_seconds"]
            < serial["population_truth_seconds"],
        "T74 all-core truth kernels did not accelerate",
    )
    summary = {
        "status": "pass",
        "case_id": H6_CASE,
        "physical_fes": 30000,
        "scientific_hash": serial["scientific_hash"],
        "serial": serial,
        "parallel": parallel,
        "speedup": {
            "monte_carlo_truth":
                serial["monte_carlo_truth_seconds"]
                / parallel["monte_carlo_truth_seconds"],
            "population_truth":
                serial["population_truth_seconds"]
                / parallel["population_truth_seconds"],
            "algorithm":
                serial["algorithm_seconds"]
                / parallel["algorithm_seconds"],
            "end_to_end":
                serial["end_to_end_seconds"]
                / parallel["end_to_end_seconds"],
        },
        "claim_boundary":
            "same pure-C++ source, paper problem, seed, work and scientific "
            "trajectory; one versus all twenty Waffle workers",
    }
    (root / "h6" / "summary.json").write_text(
        json.dumps(summary, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    return summary


def case_summary(runs: list[dict]) -> dict:
    efficiencies = [
        item["best_evaluation"]["efficiency_percent"] for item in runs
    ]
    return {
        "repeat_count": len(runs),
        "total_physical_fes": sum(item["physical_fes"] for item in runs),
        "mean_efficiency_percent": statistics.fmean(efficiencies),
        "standard_deviation_efficiency_percent":
            statistics.stdev(efficiencies),
        "minimum_efficiency_percent": min(efficiencies),
        "median_efficiency_percent": statistics.median(efficiencies),
        "maximum_efficiency_percent": max(efficiencies),
        "median_end_to_end_seconds":
            statistics.median(item["end_to_end_seconds"] for item in runs),
        "scientific_hashes": [item["scientific_hash"] for item in runs],
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", required=True)
    parser.add_argument("--output-root", required=True)
    parser.add_argument("--source-commit", required=True)
    parser.add_argument("--total-workers", type=int, default=20)
    parser.add_argument("--repeat-count", type=int, default=30)
    parser.add_argument("--seed-base", type=int, default=2026074000)
    args = parser.parse_args()
    require(args.total_workers == 20, "T74 Waffle run requires all 20 cores")
    require(args.repeat_count == 30, "T74 paper protocol requires 30 repeats")

    root = Path(args.output_root)
    root.mkdir(parents=True, exist_ok=True)
    campaign_started = time.monotonic()
    h6 = run_h6(args, root)
    case_payloads: dict[str, dict] = {}
    failures: list[str] = []
    for case_index, case_id in enumerate(CASES):
        payload = execute(
            args.binary,
            root / "formal" / f"{case_id}.json",
            [
                "--mode", "formal",
                "--case", case_id,
                "--variant", "paper_probability",
                "--workers", str(args.total_workers),
                "--seed", str(args.seed_base + case_index * 100),
                "--repeats", str(args.repeat_count),
            ],
            args.source_commit,
        )
        runs = payload["runs"]
        require(
            len(runs) == args.repeat_count,
            f"{case_id}: formal repeat count mismatch",
        )
        for repeat, run in enumerate(runs):
            failures.extend(
                validate_run(
                    run,
                    case_id,
                    30000 if repeat == 0 else 20000,
                )
            )
            if repeat == 0 and run.get("surrogate_reused"):
                failures.append(f"{case_id}: first surrogate unexpectedly reused")
            if repeat > 0 and not run.get("surrogate_reused"):
                failures.append(f"{case_id}: surrogate artifact not reused")
        case_payloads[case_id] = payload
        print(
            f"T74 completed {case_index + 1}/{len(CASES)} paper cases",
            flush=True,
        )

    summary = {
        "campaign": "T74 Waffle H6 and 15-case 30-repeat paper campaign",
        "generated_at_utc": datetime.now(timezone.utc).isoformat(),
        "source_commit": args.source_commit,
        "case_count": len(CASES),
        "repeat_count_per_case": args.repeat_count,
        "formal_run_count": len(CASES) * args.repeat_count,
        "maximum_aggregate_cpu_workers": args.total_workers,
        "total_physical_fes": sum(
            run["physical_fes"]
            for payload in case_payloads.values()
            for run in payload["runs"]
        ),
        "resource_mapping":
            "one paper case at a time; one persistent all-twenty-core C++ "
            "team; one trained MARS artifact reused across 30 repeats",
        "h6": h6,
        "case_summaries": {
            case_id: case_summary(case_payloads[case_id]["runs"])
            for case_id in CASES
        },
        "campaign_wall_seconds": time.monotonic() - campaign_started,
        "status": "pass" if not failures else "fail",
        "failures": failures,
        "claim_boundary":
            "academic paper/source reconstruction; not author numerical or "
            "random-bitstream replay",
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
