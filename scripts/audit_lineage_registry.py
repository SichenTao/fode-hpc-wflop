#!/usr/bin/env python3
"""Validate the append-only Gao-Tao WFLOP lineage coverage contract."""

from __future__ import annotations

import csv
import json
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
REGISTRY = ROOT / "docs" / "author_lineage_registry.tsv"
CONTRACT = ROOT / "docs" / "lineage_scope_contract.json"


def fail(message: str) -> None:
    raise RuntimeError(message)


def normalize_doi(value: str) -> str:
    return value.strip().lower()


def main() -> int:
    contract = json.loads(CONTRACT.read_text(encoding="utf-8"))
    with REGISTRY.open(encoding="utf-8", newline="") as handle:
        rows = list(csv.DictReader(handle, delimiter="\t"))

    required_columns = {
        "corpus_id",
        "doi",
        "title",
        "authors",
        "lineage",
        "full_text_status",
        "source_status",
        "current_platform_status",
        "problem_family",
        "next_gate",
    }
    if not rows:
        fail("lineage registry is empty")
    missing_columns = required_columns.difference(rows[0])
    if missing_columns:
        fail(f"missing registry columns: {sorted(missing_columns)}")

    if len(rows) < int(contract["minimum_record_count"]):
        fail(
            "registry fell below the frozen minimum: "
            f"{len(rows)} < {contract['minimum_record_count']}"
        )

    dois = [normalize_doi(row["doi"]) for row in rows]
    if len(dois) != len(set(dois)):
        fail("duplicate DOI in lineage registry")
    ids = [row["corpus_id"].strip() for row in rows]
    if len(ids) != len(set(ids)):
        fail("duplicate corpus_id in lineage registry")

    required_dois = {
        normalize_doi(value) for value in contract["required_dois"]
    }
    absent_dois = sorted(required_dois.difference(dois))
    if absent_dois:
        fail(f"required DOI disappeared: {absent_dois}")

    for row in rows:
        authors = row["authors"]
        if "Shangce Gao" not in authors and "Sichen Tao" not in authors:
            fail(
                f"{row['corpus_id']} is outside the signer boundary: {authors}"
            )
        if row["full_text_status"] not in {
            "validated_complete",
            "missing_recorded",
        }:
            fail(
                f"{row['corpus_id']} has an undeclared full-text state: "
                f"{row['full_text_status']}"
            )

    all_authors = "\n".join(row["authors"] for row in rows)
    absent_coauthors = [
        name
        for name in contract["required_named_coauthors"]
        if name not in all_authors
    ]
    if absent_coauthors:
        fail(f"named coauthor lineage disappeared: {absent_coauthors}")

    print(
        "lineage_audit_pass "
        f"records={len(rows)} dois={len(dois)} "
        f"named_coauthors={len(contract['required_named_coauthors'])}"
    )
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError, RuntimeError) as error:
        print(f"lineage_audit_fail: {error}", file=sys.stderr)
        sys.exit(1)
