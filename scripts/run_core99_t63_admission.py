#!/usr/bin/env python3
"""
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: resumable T63 Waffle full-resource H6/formal runner
Paper/DOI: Wind Farm Layout Optimization on Complex Terrains - Integrating a
CFD Wake Model with Mixed-Integer Programming;
10.1016/j.apenergy.2016.06.085
Public source/missing/reconstruction: hpc/core99_cpp/include/core99/kuo_t63.hpp
Controlling contract: shared/contracts/core99_t63_kuo_2016.json
Claim boundary: five paper relaxation protocols on declared proxy; not author
CFD/Gurobi numerical replay
HPC schedule: five independent relaxation cases partition the available node
cores; dependent MIP iterations remain ordered inside every case
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
"""
from __future__ import annotations

import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed
import json
import math
import os
from pathlib import Path
import subprocess
import time


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", required=True)
    parser.add_argument("--proxy", required=True)
    parser.add_argument("--output-root", required=True)
    parser.add_argument("--source-commit", required=True)
    parser.add_argument("--workers", type=int, default=0)
    parser.add_argument("--maximum-iterations", type=int, default=401)
    parser.add_argument("--mip-time-limit-seconds", type=float, default=30.0)
    args = parser.parse_args()
    workers = args.workers or (os.cpu_count() or 1)
    root = Path(args.output_root)
    root.mkdir(parents=True, exist_ok=True)
    cases = [
        ("c1p0", 1.0),
        ("c0p7", 0.7),
        ("c0p4", 0.4),
        ("c0p2", 0.2),
        ("c0p0", 0.0),
    ]
    runs: dict[str, dict] = {}
    failures: list[str] = []
    active_cases = min(len(cases), workers)
    base_workers, remainder = divmod(workers, active_cases)
    case_workers = {
        label: base_workers + (index < remainder)
        for index, (label, _) in enumerate(cases)
    }

    def converged(payload: dict) -> bool:
        history = payload.get("history", [])
        return bool(history) and history[-1].get("new_cfd_locations") == 0

    def execute_case(label: str, relaxation: float) -> tuple[str, dict]:
        path = root / f"{label}.json"
        if path.exists():
            payload = json.loads(path.read_text(encoding="utf-8"))
            if payload.get("scientific_hash") and converged(payload):
                return label, payload
        started = time.monotonic()
        environment = os.environ.copy()
        environment["OMP_NUM_THREADS"] = str(case_workers[label])
        environment["OPENBLAS_NUM_THREADS"] = "1"
        completed = subprocess.run(
            [
                args.binary,
                "--mode", "optimize",
                "--proxy", args.proxy,
                "--workers", str(case_workers[label]),
                "--relaxation", str(relaxation),
                "--maximum-iterations", str(args.maximum_iterations),
                "--mip-time-limit-seconds", str(args.mip_time_limit_seconds),
                "--output", str(path),
            ],
            text=True,
            capture_output=True,
            env=environment,
        )
        if completed.returncode != 0:
            raise RuntimeError(
                f"{label} failed: {completed.stderr or completed.stdout}"
            )
        payload = json.loads(path.read_text(encoding="utf-8"))
        payload["runner_wall_seconds"] = time.monotonic() - started
        path.write_text(
            json.dumps(payload, indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
        )
        return label, payload

    with ThreadPoolExecutor(max_workers=active_cases) as pool:
        futures = {
            pool.submit(execute_case, label, relaxation): label
            for label, relaxation in cases
        }
        for future in as_completed(futures):
            label, payload = future.result()
            runs[label] = payload
    runs = {label: runs[label] for label, _ in cases}

    for label, payload in runs.items():
        layout = payload["final_layout"]
        if len(layout) != 20 or len(set(layout)) != 20:
            failures.append(f"{label}: invalid cardinality")
        for index, left in enumerate(layout):
            left_row, left_column = divmod(left, 20)
            for right in layout[index + 1 :]:
                right_row, right_column = divmod(right, 20)
                if 140.0 * math.hypot(
                    left_row - right_row, left_column - right_column
                ) < 400.0 - 1.0e-12:
                    failures.append(f"{label}: spacing violation")
        if payload["observed_workers"] < min(2, case_workers[label]):
            failures.append(f"{label}: no multicore field evidence")
        if payload["cfd_simulations"] != 12 * payload["cfd_locations"]:
            failures.append(f"{label}: CFD lifecycle count mismatch")
        if not (
            0.0 < payload["final_true_objective"]
            <= payload["no_wake_upper_bound"]
        ):
            failures.append(f"{label}: objective/upper ordering")
        if payload["history"][-1]["new_cfd_locations"] != 0:
            failures.append(f"{label}: did not converge before safety ceiling")
        for receipt in payload["history"]:
            if len(receipt["selected_cells"]) != 20:
                failures.append(
                    f"{label}: iteration {receipt['iteration']} cardinality"
                )
            if receipt["mip_status"] not in {
                "Optimal",
                "Time limit reached",
            }:
                failures.append(
                    f"{label}: unaccepted MIP status {receipt['mip_status']}"
                )

    summary = {
        "campaign": "T63 Waffle full-resource five-case H6/formal",
        "source_commit": args.source_commit,
        "requested_workers": workers,
        "parallel_case_count": active_cases,
        "workers_per_case": case_workers,
        "mip_backend": {
            "name": "HiGHS",
            "version": "1.15.1",
            "revision": "04024d701f79feb8e2f18bc3df0dffc04ef05088"
        },
        "mip_time_limit_seconds": args.mip_time_limit_seconds,
        "maximum_iterations": args.maximum_iterations,
        "termination_bound": (
            "at most 400 newly known locations plus one final no-new-location solve"
        ),
        "paper_relaxation_values": [1.0, 0.7, 0.4, 0.2, 0.0],
        "runs": runs,
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
