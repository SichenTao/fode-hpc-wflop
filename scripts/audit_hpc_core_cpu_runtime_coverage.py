#!/usr/bin/env python3
"""Fail closed unless all 23 core targets have an executable CPU-HPC test."""

from __future__ import annotations

import argparse
import csv
import json
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
REGISTRY = ROOT / "docs/hpc_core_target_pairs.tsv"
CONTRACT = ROOT / "shared/contracts/hpc_core_cpu_runtime_coverage.json"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", type=Path, required=True)
    args = parser.parse_args()

    with REGISTRY.open(encoding="utf-8", newline="") as handle:
        rows = list(csv.DictReader(handle, delimiter="\t"))
    contract = json.loads(CONTRACT.read_text(encoding="utf-8"))
    if contract["backend_id"] != "cpu_hpc_v1":
        raise RuntimeError("core runtime coverage backend is not cpu_hpc_v1")

    routed: dict[str, str] = {}
    expected_tests: set[str] = set()
    for group in contract["coverage_groups"]:
        test = group["ctest_name"]
        expected_tests.add(test)
        if not group.get("evidence"):
            raise RuntimeError(f"{test}: empty evidence description")
        for corpus_id in group["corpus_ids"]:
            if corpus_id in routed:
                raise RuntimeError(f"{corpus_id}: duplicate runtime route")
            routed[corpus_id] = test

    expected = {row["corpus_id"] for row in rows}
    if len(rows) != 23 or set(routed) != expected:
        raise RuntimeError(
            "core runtime coverage mismatch: "
            f"rows={len(rows)} missing={sorted(expected - set(routed))} "
            f"extra={sorted(set(routed) - expected)}"
        )

    listing = subprocess.run(
        [
            "ctest",
            "--test-dir",
            str(args.build_dir),
            "--show-only=json-v1",
        ],
        check=True,
        capture_output=True,
        text=True,
    )
    available = {
        test["name"] for test in json.loads(listing.stdout)["tests"]
    }
    missing_tests = sorted(expected_tests - available)
    if missing_tests:
        raise RuntimeError(f"missing CTest routes: {missing_tests}")

    print(
        "hpc_core_cpu_runtime_coverage_pass "
        f"targets={len(rows)} ctests={len(expected_tests)} "
        "backend=cpu_hpc_v1"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
