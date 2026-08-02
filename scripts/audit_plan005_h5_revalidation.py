#!/usr/bin/env python3
"""Audit the append-only Plan-005 post-thread-control H5 revalidation."""

from __future__ import annotations

import hashlib
import json
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
RECEIPT = (
    ROOT
    / "evidence/development/"
    "plan005_h5_post_thread_topology_revalidation_20260730.json"
)


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


def main() -> int:
    require(RECEIPT.is_file(), "Plan-005 H5 revalidation receipt absent")
    data = json.loads(RECEIPT.read_text(encoding="utf-8"))
    require(
        data["schema_version"] == 1
        and data["status"]
        == "accepted_h5_revalidated_after_nonsemantic_thread_control",
        "Plan-005 H5 receipt state drift",
    )
    original = ROOT / data["original_plan004_h5_receipt"]
    require(
        sha256(original) == data["original_plan004_h5_receipt_sha256"],
        "original Plan-004 H5 receipt changed",
    )
    require(
        data["full_torch_test_count"] >= 85
        and "100% tests passed, 0 tests failed"
        in data["full_torch_ctest"]["output"],
        "full fresh Torch suite did not pass",
    )
    require(
        set(data["selected_h5_tests"]) == {
            "plan004_learning_backend_matrix",
            "plan004_artifact_target_optimization",
            "plan004_learning_full_optimizer_artifacts",
            "plan004_taae_reference_equivalence",
            "plan004_alga_reference_equivalence",
            "plan004_rlpso_reference_equivalence",
        },
        "selected H5 regression coverage drift",
    )
    require(
        data["thread_topology_contract"]["bounded_h5_observed_topology"]
        == {
            "outer_workers": 1,
            "torch_intraop_threads": 1,
            "torch_interop_threads": 1,
        },
        "bounded H5 thread topology drift",
    )
    for name, receipt in data["binary_receipts"].items():
        path = ROOT / receipt["logical_path"]
        require(
            path.is_file() and sha256(path) == receipt["sha256"],
            f"{name}: revalidated binary absent or changed",
        )
    commit = subprocess.run(
        ["git", "cat-file", "-e", f"{data['source_commit']}^{{commit}}"],
        cwd=ROOT,
        capture_output=True,
    )
    require(commit.returncode == 0, "H5 revalidation source commit absent")
    print(
        "plan005_h5_revalidation_audit_pass "
        f"full_tests={data['full_torch_test_count']} "
        "learning_methods=3 topology=controlled"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
