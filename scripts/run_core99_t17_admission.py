#!/usr/bin/env python3
"""
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: resumable T17 Waffle full-resource H6 runner
Paper/DOI: A New Wake Model and Comparison of Eight Algorithms for Layout
Optimization of Wind Farms in Complex Terrain; 10.1016/j.apenergy.2019.114189
Public source/missing/reconstruction: hpc/core99_cpp/include/core99/brogna_t17.hpp
Controlling contract: shared/contracts/core99_t17_brogna_2020.json
Claim boundary: fixed-work HPC admission over one digitized and ten random
open-proxy starts; not the paper's private-site formal numerical result
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
"""
from __future__ import annotations

import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed
import json
from pathlib import Path
import subprocess
import time


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", required=True)
    parser.add_argument("--proxy", required=True)
    parser.add_argument("--output-root", required=True)
    parser.add_argument("--source-commit", required=True)
    parser.add_argument("--workers-per-run", type=int, default=10)
    parser.add_argument("--concurrent-runs", type=int, default=2)
    parser.add_argument("--stage1-fes", type=int, default=500)
    parser.add_argument("--stage2-fes", type=int, default=2000)
    parser.add_argument("--seed", type=int, default=20260731)
    args = parser.parse_args()
    root = Path(args.output_root)
    root.mkdir(parents=True, exist_ok=True)

    jobs = [("original", args.seed, False)]
    jobs.extend(
        (f"random-{index:02d}", args.seed + index, True)
        for index in range(1, 11)
    )
    jobs.append(("original-replay", args.seed, False))

    def execute(job: tuple[str, int, bool]) -> tuple[str, dict]:
        label, seed, random_initial = job
        output = root / f"{label}.json"
        if output.exists():
            payload = json.loads(output.read_text(encoding="utf-8"))
            if payload.get("scientific_hash"):
                return label, payload
        command = [
            args.binary,
            "--mode", "optimize",
            "--proxy", args.proxy,
            "--workers", str(args.workers_per_run),
            "--stage1-fes", str(args.stage1_fes),
            "--stage2-fes", str(args.stage2_fes),
            "--seed", str(seed),
            "--output", str(output),
        ]
        if random_initial:
            command.append("--random-initial")
        started = time.monotonic()
        completed = subprocess.run(command, text=True, capture_output=True)
        if completed.returncode != 0:
            raise RuntimeError(
                f"{label} failed: {completed.stderr or completed.stdout}"
            )
        payload = json.loads(output.read_text(encoding="utf-8"))
        payload["runner_wall_seconds"] = time.monotonic() - started
        output.write_text(
            json.dumps(payload, indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
        )
        return label, payload

    runs: dict[str, dict] = {}
    with ThreadPoolExecutor(max_workers=args.concurrent_runs) as pool:
        futures = [pool.submit(execute, job) for job in jobs]
        for future in as_completed(futures):
            label, payload = future.result()
            runs[label] = payload

    failures: list[str] = []
    expected_fes = args.stage1_fes + args.stage2_fes
    for label, payload in runs.items():
        if payload["physical_fes"] != expected_fes:
            failures.append(f"{label}: physical FES mismatch")
        if payload["final_evaluation"]["constraint_violation_m"] != 0.0:
            failures.append(f"{label}: infeasible")
        if payload["observed_workers"] < min(2, args.workers_per_run):
            failures.append(f"{label}: no multicore evidence")
        if payload["final_evaluation"]["includes_wakes"] is not True:
            failures.append(f"{label}: stage-2 result omits wakes")
    if (
        runs["original"]["scientific_hash"]
        != runs["original-replay"]["scientific_hash"]
    ):
        failures.append("fixed-seed replay hash mismatch")
    summary = {
        "campaign": "T17 Waffle full-resource H6",
        "source_commit": args.source_commit,
        "workers_per_run": args.workers_per_run,
        "concurrent_runs": args.concurrent_runs,
        "maximum_requested_cpu_workers":
            args.workers_per_run * args.concurrent_runs,
        "stage1_physical_fes": args.stage1_fes,
        "stage2_physical_fes": args.stage2_fes,
        "paper_initial_layout_protocol":
            "one Figure-2 digitization plus ten random feasible layouts",
        "runs": dict(sorted(runs.items())),
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
