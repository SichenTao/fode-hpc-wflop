#!/usr/bin/env python3
"""Audit the append-only H5 nonsemantic topology-repair receipt."""

from __future__ import annotations

import hashlib
import json
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
RECEIPT = (
    ROOT
    / "evidence/development/"
    "plan005_h5_performance_first_topology_nonsemantic_revalidation_20260730.json"
)


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


def main() -> int:
    require(RECEIPT.is_file(), "topology nonsemantic H5 receipt absent")
    data = json.loads(RECEIPT.read_text(encoding="utf-8"))
    require(
        data["schema_version"] == 1
        and data["status"]
        == "accepted_h5_preserved_after_nonsemantic_topology_repair",
        "topology nonsemantic H5 receipt state drift",
    )
    prior = data["prior_h5_revalidation"]
    require(
        sha256(ROOT / prior["logical_path"]) == prior["sha256"]
        and prior["status"]
        == "accepted_h5_revalidated_after_nonsemantic_thread_control",
        "prior H5 revalidation changed",
    )
    require(
        all(
            not path.startswith(tuple(data["prohibited_semantic_prefixes_absent"]))
            for path in data["changed_paths_since_prior_h5_source_commit"]
        ),
        "semantic source path entered topology-only repair",
    )
    for name, receipt in data["binary_receipts_unchanged"].items():
        path = ROOT / receipt["logical_path"]
        require(
            path.is_file() and sha256(path) == receipt["sha256"],
            f"{name}: H5 binary changed",
        )
    topology = data["checks"]["performance_first_dry_run"]
    require(
        topology["pair_count"] == 23
        and topology["observation_count"] == 805
        and set(topology["worker_topology"])
        == {"1", "2", "4", "8", "12", "16", "20"}
        and "plan005_h5_revalidation_audit_pass"
        in data["checks"]["prior_h5_audit"]
        and "plan005_h6_receipt_audit_fixture_pass"
        in data["checks"]["h6_receipt_fixture"],
        "nonsemantic topology check coverage drift",
    )
    commit = subprocess.run(
        ["git", "cat-file", "-e", f"{data['source_commit']}^{{commit}}"],
        cwd=ROOT,
        capture_output=True,
    )
    require(commit.returncode == 0, "topology repair source commit absent")
    print(
        "plan005_h5_topology_nonsemantic_revalidation_audit_pass "
        "binaries=unchanged pairs=23 dry_run_observations=805"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
