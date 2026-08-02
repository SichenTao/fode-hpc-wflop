#!/usr/bin/env python3
"""
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: resumable T82 Waffle H6 and three-case formal campaign
Paper/DOI: Wind Farm Layout Optimization to Minimize the Wake-Induced
Turbulence Effect on Wind Turbines; 10.1016/j.apenergy.2022.119599
Public source, missing assets, conflicts, reconstruction completion,
semantic IDs, production backend, and claim boundary:
hpc/core99_cpp/include/core99/cao_t82.hpp
Resource rule: H6 compares the identical pure-C++ full-paper trajectory with
one and all twenty workers; formal runs use one all-core process and execute
the three paper cases by 25 independent seeds sequentially
Controlling contract: shared/contracts/core99_t82_cao_2022.json
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
"""

from __future__ import annotations

import argparse
import json
import math
from pathlib import Path
import statistics
import subprocess
import time


CASES = ("ideal_i", "ideal_ii", "zhuanghe")
EXPECTED_IDS = {
    "ideal_i": "t82_ideal_case_i_n30",
    "ideal_ii": "t82_ideal_case_ii_n39",
    "zhuanghe": "t82_zhuanghe_n72",
}
METHOD_ID = "t82_nsga2_mo_turbulence_declared_reconstruction_v1"
PROBLEM_ID = "t82_cao_power_turbulence_three_case_v1"


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


def execute(
    binary: str,
    output: Path,
    case_name: str,
    workers: int,
    seed: int,
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
        [
            binary,
            "--mode", "optimize",
            "--case", case_name,
            "--workers", str(workers),
            "--population", "100",
            "--generations", "20",
            "--seed", str(seed),
            "--output", str(temporary),
        ],
        text=True,
        capture_output=True,
        timeout=60 * 60,
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


def validate(
    payload: dict,
    case_name: str,
    workers: int,
) -> None:
    require(
        payload.get("problem_id") == EXPECTED_IDS[case_name],
        f"{case_name}: problem ID mismatch",
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
        payload.get("population") == 100
        and payload.get("generations") == 20
        and payload.get("physical_fes") == 2100,
        f"{case_name}: declared paper-work completion mismatch",
    )
    require(
        payload.get("requested_workers") == workers
        and payload.get("observed_workers") == workers,
        f"{case_name}: worker activation mismatch",
    )
    require(payload.get("front"), f"{case_name}: empty feasible front")
    for point in payload["front"]:
        require(
            math.isfinite(point["expected_power_kw"])
            and point["expected_power_kw"] > 0.0,
            f"{case_name}: nonfinite power",
        )
        require(
            math.isfinite(point["maximum_comprehensive_turbulence"])
            and point["maximum_comprehensive_turbulence"] > 0.0,
            f"{case_name}: nonfinite turbulence",
        )


def run_h6(args: argparse.Namespace, root: Path) -> dict:
    rows = {}
    for workers in (1, args.total_workers):
        rows[workers] = execute(
            args.binary,
            root / "h6" / f"workers-{workers:02d}.json",
            "ideal_ii",
            workers,
            args.seed_base - 1,
            args.source_commit,
        )
        validate(rows[workers], "ideal_ii", workers)
    serial = rows[1]
    parallel = rows[args.total_workers]
    require(
        serial["scientific_hash"] == parallel["scientific_hash"],
        "T82 one/all-core scientific trajectory mismatch",
    )
    speedup = {
        "evaluator":
            serial["evaluator_seconds"] / parallel["evaluator_seconds"],
        "algorithm":
            serial["algorithm_seconds"] / parallel["algorithm_seconds"],
        "end_to_end":
            serial["end_to_end_seconds"] / parallel["end_to_end_seconds"],
    }
    require(
        speedup["evaluator"] > 1.0 and speedup["end_to_end"] > 1.0,
        f"T82 dominant stages did not accelerate: {speedup}",
    )
    result = {
        "status": "pass",
        "case": "ideal_ii",
        "complete_layout_evaluations": 2100,
        "serial": serial,
        "parallel": parallel,
        "speedup": speedup,
        "claim_boundary":
            "same pure-C++ source, paper problem, seed, complete-layout "
            "work, and scientific trajectory; one versus all twenty "
            "Waffle workers",
    }
    (root / "h6" / "summary.json").write_text(
        json.dumps(result, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    return result


def summarize_case(payloads: list[dict]) -> dict:
    maximum_power = [
        max(point["expected_power_kw"] for point in item["front"])
        for item in payloads
    ]
    minimum_turbulence = [
        min(
            point["maximum_comprehensive_turbulence"]
            for point in item["front"]
        )
        for item in payloads
    ]
    return {
        "repeat_count": len(payloads),
        "total_complete_layout_evaluations":
            sum(item["physical_fes"] for item in payloads),
        "mean_maximum_front_power_kw": statistics.fmean(maximum_power),
        "standard_deviation_maximum_front_power_kw":
            statistics.stdev(maximum_power),
        "mean_minimum_front_turbulence":
            statistics.fmean(minimum_turbulence),
        "standard_deviation_minimum_front_turbulence":
            statistics.stdev(minimum_turbulence),
        "median_front_cardinality":
            statistics.median(len(item["front"]) for item in payloads),
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
    parser.add_argument("--output-root", required=True)
    parser.add_argument("--source-commit", required=True)
    parser.add_argument("--total-workers", type=int, default=20)
    parser.add_argument("--repeat-count", type=int, default=25)
    parser.add_argument("--seed-base", type=int, default=2026082000)
    args = parser.parse_args()
    require(args.total_workers == 20, "T82 Waffle requires all 20 cores")
    require(args.repeat_count == 25, "T82 formal requires 25 seeds")

    root = Path(args.output_root)
    root.mkdir(parents=True, exist_ok=True)
    started = time.monotonic()
    h6 = run_h6(args, root)
    case_summaries = {}
    completed_runs = 0
    for case_index, case_name in enumerate(CASES):
        payloads = []
        for repeat in range(args.repeat_count):
            payload = execute(
                args.binary,
                root / "formal" / case_name / f"repeat-{repeat:02d}.json",
                case_name,
                args.total_workers,
                args.seed_base + 1000 * case_index + repeat,
                args.source_commit,
            )
            validate(payload, case_name, args.total_workers)
            payloads.append(payload)
            completed_runs += 1
            print(
                f"t82_formal_progress completed={completed_runs}/"
                f"{len(CASES) * args.repeat_count} case={case_name} "
                f"repeat={repeat}",
                flush=True,
            )
        case_summaries[case_name] = summarize_case(payloads)
    summary = {
        "schema_version": 1,
        "corpus_id": "T82",
        "status": "pass",
        "source_commit": args.source_commit,
        "selected_workers": args.total_workers,
        "h6": h6,
        "formal_run_count": completed_runs,
        "total_complete_layout_evaluations":
            completed_runs * 2100,
        "cases": case_summaries,
        "campaign_wall_seconds": time.monotonic() - started,
        "claim_boundary":
            "formal results for the declared paper-equation and "
            "figure-reconstructed academic reproduction; not author-private "
            "data, source, random states, or numerical replay",
    }
    (root / "summary.json").write_text(
        json.dumps(summary, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    print(
        "t82_h6_formal_pass "
        f"runs={completed_runs} fes={completed_runs * 2100}",
        flush=True,
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
