#!/usr/bin/env python3
"""Audit all Plan-003 target-only H5 validation receipts."""

from __future__ import annotations

import csv
import hashlib
import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
REGISTRY = ROOT / "docs/hpc_core_target_pairs.tsv"
LEARNING = {"Y36", "T42", "T45"}


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main() -> int:
    with REGISTRY.open(encoding="utf-8", newline="") as handle:
        rows = list(csv.DictReader(handle, delimiter="\t"))
    if len(rows) != 23:
        raise RuntimeError(f"expected 23 core rows, found {len(rows)}")
    accepted = 0
    for row in rows:
        analysis = ROOT / row["analysis_path"]
        validation = analysis.with_name(
            analysis.name.replace(
                "_hpc_analysis.json", "_hpc_validation.json"
            )
        )
        if not validation.is_file():
            raise RuntimeError(f"{row['pair_id']}: validation absent")
        data = json.loads(validation.read_text(encoding="utf-8"))
        h5 = data["H5_bounded_equivalence"]
        if (
            data["pair_id"] != row["pair_id"]
            or data["analysis_sha256"] != sha256(analysis)
            or data["native_asset_sha256"]
            != sha256(ROOT / row["native_asset"])
            or h5["status"] != "accepted_h5"
            or data["overall_status"] != "accepted_h5_pending_h6"
            or data["H6_performance_validation"]["status"]
            != "pending_h6_measurement"
            or data["H6_performance_validation"]["accepted_backend"]
            is not None
        ):
            raise RuntimeError(f"{row['pair_id']}: H5 state mismatch")
        raw = ROOT / h5["runtime_test_receipt"]
        if sha256(raw) != h5["runtime_test_receipt_sha256"]:
            raise RuntimeError(f"{row['pair_id']}: raw receipt hash mismatch")
        raw_data = json.loads(raw.read_text(encoding="utf-8"))
        test = raw_data["tests"].get(h5["runtime_test"])
        if (
            not test
            or test["return_code"] != 0
            or test["output_sha256"]
            != h5["runtime_test_output_sha256"]
            or "100% tests passed" not in test["output"]
        ):
            raise RuntimeError(f"{row['pair_id']}: runtime test invalid")
        if row["corpus_id"] in LEARNING:
            learning = raw_data["tests"].get(
                "hpc_learning_libtorch_backends"
            )
            if (
                len(h5["candidate_backends"]) != 4
                or not learning
                or learning["output_sha256"]
                != h5["learning_backend_output_sha256"]
            ):
                raise RuntimeError(
                    f"{row['pair_id']}: learning H5 evidence invalid"
                )
        accepted += 1
    print(
        "hpc_core_h5_validation_audit_pass "
        f"pairs={accepted} learning_pairs={len(LEARNING)} "
        "h6_status=pending"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
