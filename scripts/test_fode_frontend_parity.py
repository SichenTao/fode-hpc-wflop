#!/usr/bin/env python3
"""Compare the analysis and formal CLIs over one canonical FODE core."""

from __future__ import annotations

import argparse
import json
import math
import os
import subprocess


def run(command: list[str], workers: int) -> dict:
    environment = os.environ.copy()
    environment["OMP_NUM_THREADS"] = str(workers)
    environment["OMP_DYNAMIC"] = "FALSE"
    completed = subprocess.run(
        command,
        check=True,
        capture_output=True,
        text=True,
        env=environment,
    )
    lines = [line for line in completed.stdout.splitlines() if line.startswith("{")]
    if len(lines) != 1:
        raise RuntimeError(
            f"expected one JSON result from {' '.join(command)}, got {len(lines)}"
        )
    return json.loads(lines[0])


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--analysis-binary", required=True)
    parser.add_argument("--formal-binary", required=True)
    parser.add_argument("--cases", required=True)
    args = parser.parse_args()

    case_id = "WS2tn20"
    seed = 2026073002
    physical_fes = 160
    workers = 2
    analysis = run(
        [
            args.analysis_binary,
            "--cases", args.cases,
            "--case", case_id,
            "--physical-fes", str(physical_fes),
            "--seed", str(seed),
            "--workers", str(workers),
        ],
        workers,
    )
    formal = run(
        [
            args.formal_binary,
            "--algorithm", "fode",
            "--problem", "fode_e0_common",
            "--cases", args.cases,
            "--case", case_id,
            "--physical-fes", str(physical_fes),
            "--seed", str(seed),
            "--workers", str(workers),
            "--compute-backend", "cpu",
        ],
        workers,
    )

    exact_fields = (
        "method_id", "case_id", "seed", "physical_fes", "generations",
        "initial_population", "final_population", "best_layout_1based",
    )
    for field in exact_fields:
        if analysis[field] != formal[field]:
            raise RuntimeError(
                f"frontend parity mismatch {field}: "
                f"{analysis[field]!r} != {formal[field]!r}"
            )
    if not math.isclose(
        analysis["best_expected_power_kw"],
        formal["best_expected_power_kw"],
        rel_tol=0.0,
        abs_tol=1e-12,
    ):
        raise RuntimeError("frontend objective mismatch")
    if analysis["requested_workers"] != formal["requested_workers"]:
        raise RuntimeError("frontend requested-worker receipt mismatch")
    if analysis["observed_workers"] != formal["observed_workers"]:
        raise RuntimeError("frontend observed-worker receipt mismatch")
    print(
        "fode_frontend_parity_pass "
        f"case={case_id} seed={seed} physical_fes={physical_fes} "
        f"workers={workers}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
