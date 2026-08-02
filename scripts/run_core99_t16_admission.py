#!/usr/bin/env python3
"""
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: resumable T16 Waffle full-resource H6 and paper-native
200-start formal runner
Paper/DOI: Comparison of Wind Farm Layout Optimization Results Using a
Simple Wake Model and Gradient-Based Optimization to Large Eddy Simulations;
10.2514/6.2019-0538
Public source/missing/reconstruction: hpc/core99_cpp/include/core99/thomas_t16.hpp
Controlling contract: shared/contracts/core99_t16_thomas_2019.json
Claim boundary: full reconstructed paper lifecycle with open SLSQP/exact AD;
not author SNOPT/Tapenade random-state or SOWFA numerical replay
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


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", required=True)
    parser.add_argument("--data", required=True)
    parser.add_argument("--output-root", required=True)
    parser.add_argument("--source-commit", required=True)
    parser.add_argument("--workers-per-run", type=int, default=1)
    parser.add_argument("--concurrent-runs", type=int, default=20)
    parser.add_argument("--maxeval-per-stage", type=int, default=220)
    parser.add_argument("--seed", type=int, default=20260731)
    parser.add_argument("--start-count", type=int, default=200)
    args = parser.parse_args()
    require(1 <= args.start_count <= 200, "start count must be in [1, 200]")
    require(args.workers_per_run >= 1, "workers per run must be positive")
    require(args.concurrent_runs >= 1, "concurrent runs must be positive")

    root = Path(args.output_root)
    root.mkdir(parents=True, exist_ok=True)
    campaign_started = time.monotonic()

    def execute(start_index: int) -> tuple[int, dict]:
        output = root / f"start-{start_index:03d}.json"
        if output.exists():
            payload = json.loads(output.read_text(encoding="utf-8"))
            if (
                payload.get("scientific_hash")
                and payload.get("start_index") == start_index
                and len(payload.get("stages", [])) == 10
            ):
                return start_index, payload
        command = [
            args.binary,
            "--mode", "optimize",
            "--data", args.data,
            "--workers", str(args.workers_per_run),
            "--start-index", str(start_index),
            "--seed", str(args.seed),
            "--maxeval-per-stage", str(args.maxeval_per_stage),
            "--output", str(output),
        ]
        started = time.monotonic()
        completed = subprocess.run(command, text=True, capture_output=True)
        if completed.returncode != 0:
            raise RuntimeError(
                f"start {start_index} failed: "
                f"{completed.stderr or completed.stdout}"
            )
        payload = json.loads(output.read_text(encoding="utf-8"))
        payload["runner_wall_seconds"] = time.monotonic() - started
        output.write_text(
            json.dumps(payload, indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
        )
        return start_index, payload

    runs: dict[int, dict] = {}
    with ThreadPoolExecutor(max_workers=args.concurrent_runs) as pool:
        futures = [
            pool.submit(execute, start_index)
            for start_index in range(args.start_count)
        ]
        for future in as_completed(futures):
            start_index, payload = future.result()
            runs[start_index] = payload
            print(
                f"T16 completed {len(runs)}/{args.start_count}: "
                f"start {start_index:03d}",
                flush=True,
            )

    failures: list[str] = []
    for start_index, payload in runs.items():
        if payload.get("problem_semantic_id") != (
            "t16_nantucket38_author_lineage_reconstructed_v1"
        ):
            failures.append(f"{start_index}: problem semantic ID mismatch")
        if payload.get("method_semantic_id") != (
            "t16_wec_slsqp_autodiff_reconstruction_v1"
        ):
            failures.append(f"{start_index}: method semantic ID mismatch")
        stages = payload.get("stages", [])
        if len(stages) != 10:
            failures.append(f"{start_index}: expected ten WEC/TI stages")
        elif (
            [stage["wec_factor"] for stage in stages[:9]]
            != [3.0, 2.75, 2.5, 2.25, 2.0, 1.75, 1.5, 1.25, 1.0]
            or stages[9]["wec_factor"] != 1.0
            or stages[9]["turbulence"] != "smooth"
        ):
            failures.append(f"{start_index}: paper WEC lifecycle mismatch")
        assessment = payload.get("final_paper_assessment", {})
        if not math.isfinite(assessment.get("aep_gwh", math.nan)):
            failures.append(f"{start_index}: non-finite final AEP")
        if assessment.get("maximum_constraint_violation_m", math.inf) > 1e-3:
            failures.append(f"{start_index}: infeasible final layout")
        if payload.get("observed_workers", 0) < 1:
            failures.append(f"{start_index}: no worker participation")

    values = [
        payload["final_paper_assessment"]["aep_gwh"]
        for payload in runs.values()
    ]
    values.sort()
    summary = {
        "campaign": "T16 Waffle H6 plus paper-native 200-start formal run",
        "generated_at_utc": datetime.now(timezone.utc).isoformat(),
        "source_commit": args.source_commit,
        "problem_semantic_id":
            "t16_nantucket38_author_lineage_reconstructed_v1",
        "method_semantic_id":
            "t16_wec_slsqp_autodiff_reconstruction_v1",
        "protocol_semantic_id": "t16_200start_tenstage_wec_v1",
        "workers_per_run": args.workers_per_run,
        "concurrent_runs": args.concurrent_runs,
        "maximum_requested_cpu_workers":
            args.workers_per_run * args.concurrent_runs,
        "resource_mapping":
            "independent paper multistarts are the outer full-resource axis",
        "paper_start_count": 200,
        "executed_start_count": args.start_count,
        "maximum_evaluations_per_stage": args.maxeval_per_stage,
        "paper_lifecycle":
            "WEC 3.00 to 1.00 by 0.25, then smooth local TI, "
            "then 100-point hard-TI assessment",
        "campaign_wall_seconds": time.monotonic() - campaign_started,
        "minimum_final_aep_gwh": values[0],
        "median_final_aep_gwh": values[len(values) // 2],
        "maximum_final_aep_gwh": values[-1],
        "status": "pass" if not failures else "fail",
        "failures": failures,
        "completed_start_indices": sorted(runs),
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
