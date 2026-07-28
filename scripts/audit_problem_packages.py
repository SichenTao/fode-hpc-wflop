#!/usr/bin/env python3
"""Validate coverage and identity of heterogeneous WFLOP problem packages."""

from __future__ import annotations

import csv
import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
LINEAGE = ROOT / "docs" / "author_lineage_registry.tsv"
PACKAGES = ROOT / "docs" / "problem_package_registry.tsv"
REPRODUCIBILITY = (
    ROOT / "shared" / "contracts" / "remaining_heterogeneous_reproducibility.json"
)


def main() -> int:
    with LINEAGE.open(encoding="utf-8", newline="") as handle:
        lineage = list(csv.DictReader(handle, delimiter="\t"))
    with PACKAGES.open(encoding="utf-8", newline="") as handle:
        packages = list(csv.DictReader(handle, delimiter="\t"))
    reproducibility = json.loads(REPRODUCIBILITY.read_text(encoding="utf-8"))

    required_columns = {
        "corpus_id",
        "problem_id",
        "dimensionality",
        "objective_structure",
        "decision_space",
        "required_problem_assets",
        "source_evidence",
        "current_state",
        "next_gate",
    }
    if not packages or required_columns.difference(packages[0]):
        raise RuntimeError("problem package registry schema is incomplete")

    package_ids = [row["corpus_id"] for row in packages]
    if len(package_ids) != len(set(package_ids)):
        raise RuntimeError("duplicate corpus_id in problem package registry")
    problem_ids = [row["problem_id"] for row in packages]
    if len(problem_ids) != len(set(problem_ids)):
        raise RuntimeError("duplicate problem_id in problem package registry")

    expected = {
        row["corpus_id"]
        for row in lineage
        if row["current_platform_status"] in {
            "queued",
            "current_cpp_smoke_pass",
            "current_cpp_admitted",
            "blocked_original_r1_r2",
            "reconstruction_queued",
            "nysted_reconstruction_cpp_smoke_pass",
            "proxy_cpp_smoke_pass",
        }
        and row["problem_family"] != "fode_e0_common"
    }
    observed = set(package_ids)
    if observed != expected:
        raise RuntimeError(
            "heterogeneous problem coverage mismatch: "
            f"missing={sorted(expected - observed)} "
            f"extra={sorted(observed - expected)}"
        )
    for row in packages:
        evidence = row["source_evidence"]
        if "paper_sha256:" not in evidence and "commit:" not in evidence:
            raise RuntimeError(
                f"{row['corpus_id']} lacks immutable source evidence"
            )
        if not row["next_gate"].startswith("R"):
            raise RuntimeError(
                f"{row['corpus_id']} lacks an R-gate transition"
            )

    audited_ids = set(reproducibility["scope"])
    if audited_ids != set(reproducibility["packages"]):
        raise RuntimeError("remaining heterogeneous audit scope is inconsistent")
    if not audited_ids.issubset(observed):
        raise RuntimeError(
            "audited heterogeneous packages are missing from the registry: "
            f"{sorted(audited_ids - observed)}"
        )
    for corpus_id, record in reproducibility["packages"].items():
        for key in ("paper_doi", "method", "original_problem_status"):
            if not record.get(key):
                raise RuntimeError(f"{corpus_id} lacks required audit field {key}")
        if record["original_problem_status"].startswith("blocked") and not record.get(
            "missing_problem_identity"
        ):
            raise RuntimeError(
                f"{corpus_id} blocks the original problem without an omission list"
            )
        if record.get("original_method_status", "").startswith("blocked") and not record.get(
            "missing_method_identity"
        ):
            raise RuntimeError(
                f"{corpus_id} blocks the original method without an omission list"
            )

    print(
        "problem_package_audit_pass "
        f"packages={len(packages)} queued_heterogeneous={len(expected)} "
        f"omission_audited={len(audited_ids)}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
