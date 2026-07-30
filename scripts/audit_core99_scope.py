#!/usr/bin/env python3
"""Fail-closed audit for the frozen Core-99 expansion boundary."""

from __future__ import annotations

import argparse
import json
from pathlib import Path

from core99_common import (
    EXPECTED_COUNTS,
    REGISTRY_COLUMNS,
    ROOT,
    SKIPPED_PRIMARY_PDF_IDS,
    count_states,
    index_unique,
    normalize_doi,
    read_tsv,
    sha256_file,
)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--registry",
        type=Path,
        default=ROOT / "docs/core99_expansion_registry.tsv",
    )
    parser.add_argument(
        "--receipt",
        type=Path,
        default=ROOT / "docs/core99_scope_receipt.json",
    )
    parser.add_argument("--expect-total", type=int, default=99)
    parser.add_argument("--expect-existing-direct", type=int, default=23)
    parser.add_argument("--expect-new-direct", type=int, default=69)
    parser.add_argument("--expect-reviews", type=int, default=7)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    rows = read_tsv(args.registry)
    if tuple(rows[0]) != REGISTRY_COLUMNS:
        raise RuntimeError(
            f"registry columns differ from contract: {tuple(rows[0])}"
        )
    by_id = index_unique(rows, "corpus_id", "Core-99 expansion")
    doi_index: dict[str, str] = {}
    for row in rows:
        doi = normalize_doi(row["doi"])
        if not doi:
            raise RuntimeError(f"{row['corpus_id']}: empty DOI")
        if doi in doi_index:
            raise RuntimeError(
                f"duplicate DOI {doi}: {doi_index[doi]}, {row['corpus_id']}"
            )
        doi_index[doi] = row["corpus_id"]
        if row["role"] not in {"D", "R"}:
            raise RuntimeError(f"{row['corpus_id']}: invalid role")
        if row["role"] == "R":
            if row["target_method_or_driver"] != "not_applicable":
                raise RuntimeError(
                    f"{row['corpus_id']}: review has an invented method"
                )
            if row["package_status"] != "queued_evidence_dossier":
                raise RuntimeError(
                    f"{row['corpus_id']}: invalid review package state"
                )
        if row["corpus_id"] in SKIPPED_PRIMARY_PDF_IDS:
            if (
                row["scope_status"] != "new_direct_skipped_primary_pdf"
                or row["package_status"] != "blocked_missing_primary_pdf"
                or row["pdf_sha256"] != "not_available"
            ):
                raise RuntimeError(
                    f"{row['corpus_id']}: explicit skip contract drift"
                )
        elif (
            row["primary_asset_status"] != "local_primary_pdf_hashed"
            or len(row["pdf_sha256"]) != 64
        ):
            raise RuntimeError(
                f"{row['corpus_id']}: primary PDF was not hash-verified"
            )

    counts = count_states(rows)
    expected = dict(EXPECTED_COUNTS)
    expected["total"] = args.expect_total
    expected["retained_direct"] = args.expect_existing_direct
    expected["reviews"] = args.expect_reviews
    expected["new_direct_ready"] = (
        args.expect_new_direct - len(SKIPPED_PRIMARY_PDF_IDS)
    )
    expected["new_direct_skipped"] = len(SKIPPED_PRIMARY_PDF_IDS)
    expected["direct"] = (
        expected["retained_direct"]
        + expected["new_direct_ready"]
        + expected["new_direct_skipped"]
    )
    if counts != expected:
        raise RuntimeError(f"count mismatch expected={expected} found={counts}")

    receipt = json.loads(args.receipt.read_text(encoding="utf-8"))
    registry_hash = sha256_file(args.registry)
    if receipt["expansion_registry_sha256"] != registry_hash:
        raise RuntimeError("scope receipt registry hash mismatch")
    if receipt["counts"] != counts:
        raise RuntimeError("scope receipt count mismatch")
    if receipt["explicit_skip_ids"] != sorted(SKIPPED_PRIMARY_PDF_IDS):
        raise RuntimeError("scope receipt skip-set mismatch")

    ready_ids = sorted(
        row["corpus_id"]
        for row in rows
        if row["scope_status"] == "new_direct_ready"
    )
    print(
        "core99_scope_audit_pass "
        f"total={counts['total']} direct={counts['direct']} "
        f"retained={counts['retained_direct']} "
        f"new_ready={counts['new_direct_ready']} "
        f"skipped={counts['new_direct_skipped']} "
        f"reviews={counts['reviews']} "
        f"first_ready={ready_ids[0]} last_ready={ready_ids[-1]}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
