#!/usr/bin/env python3
"""Smoke and replay checks for the declared RPSO-derived compact proxy."""

from __future__ import annotations

import argparse
import json
import subprocess


def run(binary: str, cases: str, algorithm: str, workers: int) -> dict:
    completed = subprocess.run(
        [
            binary,
            "--algorithm", algorithm,
            "--problem", "rpso2024_source_problem_ws1_ws4",
            "--cases", cases,
            "--case", "RPSO-WS1-tn30",
            "--physical-fes", "480",
            "--workers", str(workers),
            "--seed", "2026072901",
        ],
        check=True,
        text=True,
        capture_output=True,
    )
    return json.loads(completed.stdout)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", required=True)
    parser.add_argument("--cases", required=True)
    args = parser.parse_args()
    profiles = ["rlpso_compact_policy_declared_reconstruction_v1"]
    for profile in profiles:
        first = run(args.binary, args.cases, profile, 20)
        second = run(args.binary, args.cases, profile, 20)
        scalar = run(args.binary, args.cases, profile, 1)
        if first["physical_fes"] != 480:
            raise RuntimeError(f"{profile}: exact FES failed")
        if first["training_physical_fes"] <= 0:
            raise RuntimeError(f"{profile}: training FES was not exposed")
        if (
            first["training_physical_fes"] + first["inference_physical_fes"]
            != first["physical_fes"]
        ):
            raise RuntimeError(f"{profile}: FES ledger does not reconcile")
        if first["best_layout_1based"] != second["best_layout_1based"]:
            raise RuntimeError(f"{profile}: deterministic layout replay failed")
        if first["best_expected_power_kw"] != second["best_expected_power_kw"]:
            raise RuntimeError(f"{profile}: deterministic objective replay failed")
        if (
            first["best_layout_1based"] != scalar["best_layout_1based"]
            or first["best_expected_power_kw"]
            != scalar["best_expected_power_kw"]
        ):
            raise RuntimeError(f"{profile}: 1/20 worker semantics differ")
    blocked = subprocess.run(
        [
            args.binary,
            "--algorithm", "rlpso",
            "--problem", "rpso2024_source_problem_ws1_ws4",
            "--cases", args.cases,
            "--case", "RPSO-WS1-tn30",
            "--physical-fes", "10",
            "--workers", "1",
        ],
        text=True,
        capture_output=True,
    )
    if blocked.returncode == 0 or "intentionally blocked at R2" not in blocked.stderr:
        raise RuntimeError("original rlpso identifier was not guarded")
    print("rlpso_reconstruction_test_pass profiles=1 workers=20 fes=480")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
