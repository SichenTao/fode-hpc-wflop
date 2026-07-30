#!/usr/bin/env python3
"""Audit the append-only post-learning-phase Plan-005 H5 receipt."""

from __future__ import annotations

import hashlib
import json
import subprocess
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
RECEIPT = (
    ROOT
    / "evidence/development/"
    "plan005_h5_post_learning_phase_topology_revalidation_20260730.json"
)


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def validate_phase_report(report: dict[str, Any]) -> None:
    require(report["status"] == "pass", "phase topology report failed")
    taae = report["taae"]
    require(
        taae["discrete_status"] == "exact"
        and taae["scientific_state"]["physical_fes"] == 350
        and taae["numerical_state"]["status"] == "accepted"
        and taae["numerical_state"]["relative_tolerance"] == 1.0e-12
        and taae["numerical_state"]["absolute_tolerance"] == 1.0e-9,
        "TAAE exact science/FES/numerical receipt drift",
    )
    for item in taae["runs"]:
        workers = item["workers"]
        require(
            item["peak_os_threads"] <= 3 * workers + 4
            and item["cpu_time_to_wall"] <= workers + 1.0,
            "TAAE thread-bound receipt drift",
        )
    for method, physical_fes in (("alga", 90), ("rlpso", 100)):
        section = report[method]
        require(
            section["status"] == "raw_bit_and_discrete_exact"
            and section["scientific_state"]["physical_fes"] == physical_fes,
            f"{method}: exact learned-state/FES receipt drift",
        )
        for item in section["runs"]:
            workers = item["workers"]
            require(
                item["peak_os_threads"] <= 3 * workers + 4
                and item["cpu_time_to_wall"] <= workers + 1.0,
                f"{method}: thread-bound receipt drift",
            )


def main() -> int:
    require(RECEIPT.is_file(), "learning-phase H5 receipt absent")
    data = json.loads(RECEIPT.read_text(encoding="utf-8"))
    require(
        data["schema_version"] == 1
        and data["status"]
        == "accepted_h5_revalidated_after_learning_phase_topology_repair",
        "learning-phase H5 status drift",
    )
    prior = ROOT / data["prior_plan005_h5_receipt"]
    require(
        prior.is_file()
        and sha256(prior) == data["prior_plan005_h5_receipt_sha256"],
        "prior Plan-005 H5 receipt changed",
    )
    require(
        data["full_cpu_test_count"] >= 74
        and data["full_torch_test_count"] >= 86
        and "100% tests passed, 0 tests failed"
        in data["full_cpu_ctest"]["output"]
        and "100% tests passed, 0 tests failed"
        in data["full_torch_ctest"]["output"],
        "fresh full CPU/Torch suite receipt drift",
    )
    validate_phase_report(data["phase_topology_report"])
    require(
        "rejected_nested_threads=1"
        in data["h6_receipt_tamper_fixture"]["output"]
        and "rejected_numerical_state=1"
        in data["h6_receipt_tamper_fixture"]["output"],
        "H6 tamper-negative receipt drift",
    )
    for name, build in data["fresh_builds"].items():
        cache = ROOT / build["logical_path"]
        require(
            cache.is_file() and sha256(cache) == build["cmake_cache_sha256"],
            f"{name}: fresh CMake cache absent or changed",
        )
    for name, binary in data["binary_receipts"].items():
        path = ROOT / binary["logical_path"]
        require(
            path.is_file() and sha256(path) == binary["sha256"],
            f"{name}: H5 binary absent or changed",
        )
    commit = subprocess.run(
        ["git", "cat-file", "-e", f"{data['source_commit']}^{{commit}}"],
        cwd=ROOT,
        capture_output=True,
    )
    require(commit.returncode == 0, "H5 source commit absent")
    print(
        "plan005_h5_learning_phase_audit_pass "
        f"cpu_tests={data['full_cpu_test_count']} "
        f"torch_tests={data['full_torch_test_count']} "
        "taae_numerical=accepted alga_rlpso_state=exact"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
