#!/usr/bin/env python3
"""Audit one dated source-search dossier for every scoped WFLOP DOI."""

from __future__ import annotations

import csv
import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
DOSSIERS = ROOT / "docs/source-dossiers"
AUTHORITIES = {
    "official_author_homepage",
    "doi_publisher_supplement_and_data_statement",
    "paper_author_or_coauthor_linked_repository",
    "author_coauthor_github",
    "general_research_data_repositories",
    "cited_implementation_and_predecessor_work",
    "same_author_problem_or_method_family",
}


def main() -> int:
    with (ROOT / "docs/author_lineage_registry.tsv").open(
        encoding="utf-8", newline=""
    ) as handle:
        lineage = list(csv.DictReader(handle, delimiter="\t"))
    expected = {row["corpus_id"]: row for row in lineage}
    files = {path.stem: path for path in DOSSIERS.glob("*.json")}
    if set(files) != set(expected):
        raise RuntimeError(
            f"dossier coverage mismatch missing={sorted(set(expected)-set(files))} "
            f"extra={sorted(set(files)-set(expected))}"
        )
    bounded_negative = 0
    for corpus_id, path in files.items():
        payload = json.loads(path.read_text(encoding="utf-8"))
        if payload["doi"].lower() != expected[corpus_id]["doi"].lower():
            raise RuntimeError(f"{corpus_id}: DOI mismatch")
        if payload["query_date"] != "2026-07-29":
            raise RuntimeError(f"{corpus_id}: stale or unexpected query date")
        observed = {
            row["authority"] for row in payload["searched_authorities"]
        }
        if observed != AUTHORITIES:
            raise RuntimeError(f"{corpus_id}: incomplete authority ladder")
        for row in payload["searched_authorities"]:
            if not row["url"] or not row["query"] or not row["result"]:
                raise RuntimeError(f"{corpus_id}: incomplete search observation")
        if payload["conclusion"] == "bounded_no_public_asset_found":
            bounded_negative += 1
            if not payload["negative_evidence_boundary"]:
                raise RuntimeError(f"{corpus_id}: unbounded negative conclusion")
    print(
        "source_dossier_audit_pass "
        f"papers={len(files)} bounded_negative={bounded_negative}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
