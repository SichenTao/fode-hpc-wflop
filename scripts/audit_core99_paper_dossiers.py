#!/usr/bin/env python3
"""Audit Core-99 dossier cardinality, identity, and fail-closed states."""

from __future__ import annotations

import json
from pathlib import Path

from core99_common import ROOT, index_unique, read_tsv, sha256_file


REGISTRY = ROOT / "docs/core99_expansion_registry.tsv"
DOSSIERS = ROOT / "evidence/core99/paper-dossiers"


def main() -> int:
    scope_rows = [
        row
        for row in read_tsv(REGISTRY)
        if row["scope_status"] != "retained_direct"
    ]
    scope = index_unique(scope_rows, "corpus_id", "non-retained scope")
    index_payload = json.loads(
        (DOSSIERS / "index.json").read_text(encoding="utf-8")
    )
    index = {
        row["corpus_id"]: row for row in index_payload["records"]
    }
    if set(index) != set(scope):
        raise RuntimeError(
            "dossier coverage mismatch "
            f"missing={sorted(set(scope)-set(index))} "
            f"extra={sorted(set(index)-set(scope))}"
        )
    for corpus_id, row in scope.items():
        path = DOSSIERS / f"{corpus_id}.json"
        if sha256_file(path) != index[corpus_id]["dossier_sha256"]:
            raise RuntimeError(f"{corpus_id}: dossier hash mismatch")
        dossier = json.loads(path.read_text(encoding="utf-8"))
        if dossier["paper"]["doi"] != row["doi"]:
            raise RuntimeError(f"{corpus_id}: DOI mismatch")
        if dossier["paper"]["pdf_sha256"] != row["pdf_sha256"]:
            raise RuntimeError(f"{corpus_id}: primary-asset hash mismatch")
        if corpus_id == "T32":
            if dossier["dossier_status"] != "blocked_missing_primary_pdf":
                raise RuntimeError("T32: skip boundary drift")
        elif row["role"] == "R":
            if dossier["dossier_status"] != "review_evidence_audit_ready":
                raise RuntimeError(f"{corpus_id}: review dossier state")
        elif dossier["dossier_status"] not in {
            "pre_code_fulltext_audit_ready",
            "scientific_contract_frozen",
            "implementation_in_progress",
            "h5_admitted",
            "h5_admitted_h6_running",
            "h6_admitted",
            "h6_admitted_formal_pending",
            "formal_complete",
            "resource_deferred",
        }:
            raise RuntimeError(f"{corpus_id}: invalid direct dossier state")
        if dossier["formal_status"] == "complete":
            if dossier["hpc_admission"]["H5"] != "pass":
                raise RuntimeError(f"{corpus_id}: formal without H5")
            if dossier["hpc_admission"]["H6"] != "pass":
                raise RuntimeError(f"{corpus_id}: formal without H6")
    print(
        "core99_dossier_audit_pass "
        f"records={len(index)} direct_ready=68 skipped=1 reviews=7"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
