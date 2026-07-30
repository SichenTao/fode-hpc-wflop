#!/usr/bin/env python3
"""
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: resumable T62 Waffle full-resource H6 runner
Paper title/DOI: Optimization of Wind Turbine Layout Position in a Wind Farm
Using a Newly-Developed Two-Dimensional Wake Model;
10.1016/j.apenergy.2016.04.098
Public source: none located
Missing/reconstruction/semantic IDs/contract/claim boundary:
hpc/core99_cpp/include/core99/gao_t62.hpp and
shared/contracts/core99_t62_gao_2016.json
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
"""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import subprocess
import time


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", required=True)
    parser.add_argument("--output-root", required=True)
    parser.add_argument("--workers", type=int, default=0)
    parser.add_argument("--seed", type=int, default=20260731)
    parser.add_argument("--source-commit", required=True)
    args = parser.parse_args()
    workers = args.workers or (os.cpu_count() or 1)
    root = Path(args.output_root)
    root.mkdir(parents=True, exist_ok=True)
    runs: dict[str, dict] = {}

    def execute(label: str, turbines: int, seed: int) -> dict:
        path = root / f"{label}.json"
        if path.exists():
            payload = json.loads(path.read_text(encoding="utf-8"))
            if payload.get("scientific_hash"):
                return payload
        started = time.monotonic()
        completed = subprocess.run(
            [
                args.binary,
                "--mode", "optimize",
                "--site-mode", "grid",
                "--turbines", str(turbines),
                "--seed", str(seed),
                "--workers", str(workers),
                "--demes", "10",
                "--individuals", "20",
                "--stagnation", "500",
                "--max-generations", "5000",
                "--migration-period", "20",
                "--output", str(path),
            ],
            text=True,
            capture_output=True,
        )
        if completed.returncode != 0:
            raise RuntimeError(
                f"{label} failed: {completed.stderr or completed.stdout}"
            )
        payload = json.loads(path.read_text(encoding="utf-8"))
        payload["runner_wall_seconds"] = time.monotonic() - started
        return payload

    for turbines in (38, 39, 40):
        label = f"n{turbines}-seed-{args.seed}"
        runs[label] = execute(label, turbines, args.seed)
    replay_label = f"n39-seed-{args.seed}-replay"
    replay_path = root / f"{replay_label}.json"
    if replay_path.exists():
        replay_path.unlink()
    runs[replay_label] = execute(replay_label, 39, args.seed)

    primary = runs[f"n39-seed-{args.seed}"]
    replay = runs[replay_label]
    failures = []
    for label, payload in runs.items():
        evaluation = payload["best_evaluation"]
        if evaluation["constraint_violation"] != 0.0:
            failures.append(f"{label}: infeasible")
        if not (0.0 < evaluation["efficiency"] <= 1.0):
            failures.append(f"{label}: invalid efficiency")
        if payload["observed_workers"] < min(2, workers):
            failures.append(f"{label}: no multicore evidence")
        if payload["generations"] < 500:
            failures.append(f"{label}: did not exercise paper stop")
    if primary["scientific_hash"] != replay["scientific_hash"]:
        failures.append("fixed-seed replay hash mismatch")
    summary = {
        "campaign": "T62 Waffle full-resource H6",
        "source_commit": args.source_commit,
        "requested_workers": workers,
        "paper_stop": "500 unchanged generations",
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
