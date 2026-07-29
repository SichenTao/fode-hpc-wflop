#!/usr/bin/env python3
"""Audit independent algorithm/problem registration and fail-closed pairing."""

from __future__ import annotations

import argparse
import json
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def lines(binary: str, flag: str) -> list[str]:
    completed = subprocess.run(
        [binary, flag],
        check=True,
        capture_output=True,
        text=True,
    )
    return [line.strip() for line in completed.stdout.splitlines() if line.strip()]


def explain(binary: str, algorithm: str, problem: str) -> dict:
    completed = subprocess.run(
        [binary, "--explain-compatibility", algorithm, problem],
        check=True,
        capture_output=True,
        text=True,
    )
    return json.loads(completed.stdout)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", required=True)
    parser.add_argument("--formal-only", action="store_true")
    args = parser.parse_args()

    algorithms = lines(args.binary, "--list-algorithms")
    problems = lines(args.binary, "--list-problems")
    training = lines(args.binary, "--list-training")
    backends = lines(args.binary, "--list-compute-backends")
    if len(algorithms) < 23:
        raise RuntimeError(f"expected at least 23 algorithms, found {len(algorithms)}")
    if len(problems) < 5:
        raise RuntimeError(f"expected at least 5 problems, found {len(problems)}")
    if len(training) < 5:
        raise RuntimeError(f"expected at least 5 training lifecycles, found {len(training)}")
    if not any(line.startswith("cpu\tsupported\t") for line in backends):
        raise RuntimeError("CPU backend is not executable")
    for unsupported in ("auto", "hybrid", "gpu"):
        if not any(
            line.startswith(f"{unsupported}\tfail_closed\t")
            for line in backends
        ):
            raise RuntimeError(f"{unsupported} backend is not truthfully fail-closed")

    accepted = explain(args.binary, "fode", "fode_e0_common")
    rejected = explain(
        args.binary,
        "fode",
        "taae_zhangbei_structured_declared_proxy_v1",
    )
    if not accepted["compatible"] or rejected["compatible"]:
        raise RuntimeError("compatibility decisions are incorrect")
    if not accepted["reason"] or not rejected["reason"]:
        raise RuntimeError("compatibility decision lacks a reason")

    failed = subprocess.run(
        [
            args.binary,
            "--algorithm", "fode",
            "--problem", "taae_zhangbei_structured_declared_proxy_v1",
            "--cases",
            str(
                ROOT
                / "shared/contracts/"
                "taae_zhangbei_structured_declared_proxy_cases.json"
            ),
            "--case", "TAAE_Proxy_NC1_Budget600k_tn15",
            "--physical-fes", "1",
            "--workers", "1",
        ],
        capture_output=True,
        text=True,
    )
    if failed.returncode == 0:
        raise RuntimeError("incompatible pair executed")
    if "not admitted for problem" not in failed.stderr:
        raise RuntimeError("incompatible pair did not report a precise reason")

    print(
        "algorithm_problem_compatibility_audit_pass "
        f"algorithms={len(algorithms)} problems={len(problems)} "
        f"training={len(training)} backends={len(backends)}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
