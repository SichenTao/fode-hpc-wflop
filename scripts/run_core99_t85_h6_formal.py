#!/usr/bin/env python3
"""
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: resumable T85 Waffle H6 and six-case formal campaign
Paper/DOI: Particle Swarm Optimization of a Wind Farm Layout with Active
Control of Turbine Yaws; 10.1016/j.renene.2023.02.058
Public source, cited predecessor, missing assets, reconstruction completion,
semantic IDs, production backend, and claim boundary:
hpc/core99_cpp/include/core99/song_t85.hpp
Resource rule: H6 compares the identical pure-C++ full-paper trajectory with
one and all twenty workers; formal runs use one all-core process and execute
the six paper cases by 25 independent seeds sequentially
Controlling contract: shared/contracts/core99_t85_song_2023.json
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


CASES = ("wf1", "wf1_u6", "wf1_v112", "wf2", "wf3", "wf4")
EXPECTED_IDS = {
    "wf1": "t85_wf1_v80_u8_n25",
    "wf1_u6": "t85_wf1_v80_u6_n25",
    "wf1_v112": "t85_wf1_v112_u8_n25",
    "wf2": "t85_wf2_v80_u8_n25",
    "wf3": "t85_wf3_v80_u8_n36",
    "wf4": "t85_wf4_v80_uneven_n25",
}
METHOD_ID = "t85_agldpso_joint_yaw_declared_reconstruction_v1"
PROBLEM_ID = "t85_song_joint_layout_yaw_six_case_v1"
POPULATION = 500
PHYSICAL_FES = 10_000


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
            "--population", str(POPULATION),
            "--physical-fes-limit", str(PHYSICAL_FES),
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


def validate(payload: dict, case_name: str, workers: int) -> None:
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
        payload.get("population") == POPULATION
        and payload.get("physical_fes") == PHYSICAL_FES,
        f"{case_name}: declared paper-work completion mismatch",
    )
    require(
        payload.get("requested_workers") == workers
        and payload.get("observed_workers") == workers,
        f"{case_name}: worker activation mismatch",
    )
    require(
        math.isfinite(payload.get("best_aep_gwh", float("nan")))
        and payload["best_aep_gwh"] > 0.0,
        f"{case_name}: nonfinite best AEP",
    )
    require(
        payload["best_aep_gwh"] >= payload["initial_best_aep_gwh"],
        f"{case_name}: optimizer regressed from initialization",
    )
    require(payload.get("scientific_hash"), f"{case_name}: missing hash")


def run_h6(args: argparse.Namespace, root: Path) -> dict:
    rows = {}
    for workers in (1, args.total_workers):
        rows[workers] = execute(
            args.binary,
            root / "h6" / f"workers-{workers:02d}.json",
            "wf3",
            workers,
            args.seed_base - 1,
            args.source_commit,
        )
        validate(rows[workers], "wf3", workers)
    serial = rows[1]
    parallel = rows[args.total_workers]
    require(
        serial["scientific_hash"] == parallel["scientific_hash"],
        "T85 one/all-core scientific trajectory mismatch",
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
        f"T85 dominant stages did not accelerate: {speedup}",
    )
    result = {
        "status": "pass",
        "case": "wf3",
        "complete_layout_evaluations": PHYSICAL_FES,
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
    best = [item["best_aep_gwh"] for item in payloads]
    runtime = [item["end_to_end_seconds"] for item in payloads]
    return {
        "repeat_count": len(payloads),
        "total_complete_layout_evaluations":
            sum(item["physical_fes"] for item in payloads),
        "mean_best_aep_gwh": statistics.fmean(best),
        "standard_deviation_best_aep_gwh": statistics.stdev(best),
        "median_best_aep_gwh": statistics.median(best),
        "minimum_best_aep_gwh": min(best),
        "maximum_best_aep_gwh": max(best),
        "median_end_to_end_seconds": statistics.median(runtime),
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
    parser.add_argument("--seed-base", type=int, default=2026085000)
    args = parser.parse_args()
    require(args.total_workers == 20, "T85 Waffle requires all 20 cores")
    require(args.repeat_count == 25, "T85 formal requires 25 seeds")

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
                f"t85_formal_progress completed={completed_runs}/"
                f"{len(CASES) * args.repeat_count} case={case_name} "
                f"repeat={repeat}",
                flush=True,
            )
        case_summaries[case_name] = summarize_case(payloads)
    summary = {
        "schema_version": 1,
        "corpus_id": "T85",
        "status": "pass",
        "source_commit": args.source_commit,
        "selected_workers": args.total_workers,
        "h6": h6,
        "formal_run_count": completed_runs,
        "total_complete_layout_evaluations":
            completed_runs * PHYSICAL_FES,
        "cases": case_summaries,
        "campaign_wall_seconds": time.monotonic() - started,
        "claim_boundary":
            "formal results for the declared paper-equation and "
            "figure-reconstructed academic reproduction; not author-private "
            "source, data, random states, or numerical replay",
    }
    (root / "summary.json").write_text(
        json.dumps(summary, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    print(
        "t85_h6_formal_pass "
        f"runs={completed_runs} fes={completed_runs * PHYSICAL_FES}",
        flush=True,
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
