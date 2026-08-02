#!/usr/bin/env python3
"""Record that the H6 topology repair is nonsemantic with respect to H5."""

from __future__ import annotations

import hashlib
import json
import subprocess
import sys
from datetime import datetime
from pathlib import Path
from zoneinfo import ZoneInfo


ROOT = Path(__file__).resolve().parents[1]
PRIOR = (
    ROOT
    / "evidence/development/"
    "plan005_h5_post_thread_topology_revalidation_20260730.json"
)
OUTPUT = (
    ROOT
    / "evidence/development/"
    "plan005_h5_performance_first_topology_nonsemantic_revalidation_20260730.json"
)


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def run(command: list[str]) -> str:
    return subprocess.run(
        command,
        cwd=ROOT,
        check=True,
        capture_output=True,
        text=True,
    ).stdout


def main() -> int:
    if OUTPUT.exists():
        raise RuntimeError(f"append-only receipt exists: {OUTPUT}")
    prior = json.loads(PRIOR.read_text(encoding="utf-8"))
    source_commit = run(["git", "rev-parse", "HEAD"]).strip()
    changed = [
        line
        for line in run([
            "git", "diff", "--name-only",
            f"{prior['source_commit']}..{source_commit}",
        ]).splitlines()
        if line
    ]
    prohibited_prefixes = (
        "hpc/",
        "shared/",
        ".source-cache/",
        "docs/hpc_core_target_pairs.tsv",
    )
    if any(path.startswith(prohibited_prefixes) for path in changed):
        raise RuntimeError("topology repair touched H5 semantic or binary inputs")
    binary_receipts = {}
    for name, receipt in prior["binary_receipts"].items():
        path = ROOT / receipt["logical_path"]
        observed = sha256(path)
        if observed != receipt["sha256"]:
            raise RuntimeError(f"{name}: H5 binary changed")
        binary_receipts[name] = {
            "logical_path": receipt["logical_path"],
            "sha256": observed,
        }
    h5_audit = run([sys.executable, "scripts/audit_plan005_h5_revalidation.py"])
    fixture = run([sys.executable, "scripts/test_plan005_h6_receipt_audit.py"])
    dry_run = json.loads(run([
        sys.executable,
        "scripts/run_hpc_core_target_scaling.py",
        "--workers", "1,2,4,8,12,16,20",
        "--repetitions", "5",
        "--balanced-order",
        "--scope", "core",
        "--production-representative",
        "--dry-run",
    ]))
    if dry_run["pair_count"] != 23 or dry_run["observation_count"] != 805:
        raise RuntimeError("performance-first dry-run cardinality drift")
    topology = {}
    for item in dry_run["specifications"]:
        topology.setdefault(str(item["workers"]), {
            "selection_order": item["selection_order"],
            "affinity_set": item["affinity_cpus"],
            "core_type_counts": item["core_type_counts"],
        })
    document = {
        "schema_version": 1,
        "receipt_id": (
            "plan005_h5_performance_first_topology_nonsemantic_"
            "revalidation_spark_20260730"
        ),
        "recorded_at": datetime.now(ZoneInfo("Asia/Tokyo")).isoformat(),
        "source_commit": source_commit,
        "prior_h5_revalidation": {
            "logical_path": str(PRIOR.relative_to(ROOT)),
            "sha256": sha256(PRIOR),
            "source_commit": prior["source_commit"],
            "status": prior["status"],
        },
        "changed_paths_since_prior_h5_source_commit": changed,
        "prohibited_semantic_prefixes_absent": list(prohibited_prefixes),
        "binary_receipts_unchanged": binary_receipts,
        "checks": {
            "prior_h5_audit": h5_audit.strip(),
            "h6_receipt_fixture": fixture.strip(),
            "performance_first_dry_run": {
                "pair_count": dry_run["pair_count"],
                "observation_count": dry_run["observation_count"],
                "worker_topology": topology,
            },
        },
        "semantic_invariants": [
            "method and problem identities unchanged",
            "random-event ownership and seeds unchanged",
            "physical-FES budgets unchanged",
            "H0 stage definitions unchanged",
            "measured binaries unchanged",
        ],
        "claim_boundary": (
            "This receipt proves only that replacing numeric-prefix CPU "
            "selection with a frozen architecture-aware affinity policy is a "
            "measurement-topology change. It does not rerun or supersede H5."
        ),
        "status": "accepted_h5_preserved_after_nonsemantic_topology_repair",
    }
    OUTPUT.write_text(
        json.dumps(document, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    print(
        "plan005_h5_topology_nonsemantic_revalidation_pass "
        f"changed_paths={len(changed)} binaries={len(binary_receipts)} "
        "dry_run_observations=805"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
