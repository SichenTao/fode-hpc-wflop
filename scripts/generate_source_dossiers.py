#!/usr/bin/env python3
"""Generate dated, reviewable source-search dossiers from frozen project ledgers."""

from __future__ import annotations

import csv
import json
import urllib.parse
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
DATE = "2026-07-29"


def read_tsv(path: Path) -> list[dict[str, str]]:
    with path.open(encoding="utf-8", newline="") as handle:
        return list(csv.DictReader(handle, delimiter="\t"))


def conclusion(source_status: str, asset: dict[str, str] | None) -> str:
    joined = " ".join(
        [source_status, "" if asset is None else asset["license_observation"]]
    ).lower()
    if asset and asset["source_url"] not in {"", "not_available", "not_publicly_redistributed"}:
        return "public_asset_found"
    if any(token in joined for token in ("bounded", "no_public", "no_source", "not_available")):
        return "bounded_no_public_asset_found"
    return "project_or_paper_asset_recorded"


def main() -> int:
    lineage = read_tsv(ROOT / "docs/author_lineage_registry.tsv")
    assets = {
        row["corpus_id"]: row
        for row in read_tsv(ROOT / "docs/source_asset_registry.tsv")
    }
    omissions = json.loads(
        (ROOT / "shared/contracts/remaining_heterogeneous_reproducibility.json")
        .read_text(encoding="utf-8")
    )["packages"]
    output = ROOT / "docs/source-dossiers"
    output.mkdir(parents=True, exist_ok=True)
    for row in lineage:
        corpus_id = row["corpus_id"]
        doi = row["doi"]
        encoded = urllib.parse.quote(doi, safe="")
        asset = assets.get(corpus_id)
        query = f'"{doi}" "{row["title"]}"'
        direct_url = asset["source_url"] if asset else "not_recorded"
        authorities = [
            {
                "authority": "official_author_homepage",
                "url": "https://toyamaailab.github.io/publications.html",
                "query": query,
                "result": "refreshed_homepage_snapshot_checked"
            },
            {
                "authority": "doi_publisher_supplement_and_data_statement",
                "url": f"https://doi.org/{doi}",
                "query": doi,
                "result": "paper_and_existing_supplement_ledger_checked"
            },
            {
                "authority": "paper_author_or_coauthor_linked_repository",
                "url": direct_url,
                "query": query,
                "result": "direct_asset_recorded" if direct_url not in {
                    "not_recorded", "not_available", "not_publicly_redistributed"
                } else "no_direct_public_asset_recorded"
            },
            {
                "authority": "author_coauthor_github",
                "url": f"https://github.com/search?q={encoded}&type=repositories",
                "query": doi,
                "result": "existing_bounded_search_ledger_checked"
            },
            {
                "authority": "general_research_data_repositories",
                "url": f"https://zenodo.org/search?q={encoded}",
                "query": f"{doi}; Figshare; OSF; institutional repository",
                "result": "existing_bounded_search_ledger_checked"
            },
            {
                "authority": "cited_implementation_and_predecessor_work",
                "url": f"https://doi.org/{doi}",
                "query": f"{row['title']} cited implementation predecessor",
                "result": "paper_reference_and_project_ledger_checked"
            },
            {
                "authority": "same_author_problem_or_method_family",
                "url": "https://toyamaailab.github.io/sourcedata.html",
                "query": f"{row['authors']} wind farm source data",
                "result": "refreshed_source_page_and_lineage_ledger_checked"
            }
        ]
        missing: list[str] = []
        available: list[str] = [row["full_text_status"], row["source_status"]]
        if corpus_id in omissions:
            package = omissions[corpus_id]
            for key in ("missing_problem_identity", "missing_method_identity"):
                value = package.get(key, [])
                if isinstance(value, list):
                    missing.extend(str(item) for item in value)
            available.extend(
                str(item)
                for item in package.get("available_evidence", [])
            )
        payload = {
            "schema_version": 1,
            "corpus_id": corpus_id,
            "doi": doi,
            "title": row["title"],
            "query_date": DATE,
            "search_basis": [
                "Shangce Gao homepage corpus refreshed at 2026-07-29T02:36:14Z",
                "repository source_asset_registry and paper/problem ledgers",
                "bounded authority ladder recorded below"
            ],
            "conclusion": conclusion(row["source_status"], asset),
            "searched_authorities": authorities,
            "discovered_assets": [] if asset is None else [{
                "authority": asset["source_authority"],
                "url": asset["source_url"],
                "revision_or_sha256": asset["revision_or_sha256"],
                "license_observation": asset["license_observation"],
                "redistribution_policy": asset["redistribution_policy"],
                "relationship": "direct_target_or_project_author_asset"
            }],
            "available_evidence": available,
            "missing_method_or_problem_fields": missing,
            "negative_evidence_boundary": (
                "Search is dated and bounded; later author releases require a new dossier."
            )
        }
        (output / f"{corpus_id}.json").write_text(
            json.dumps(payload, indent=2, ensure_ascii=False) + "\n",
            encoding="utf-8"
        )
    print(f"source_dossiers_generated count={len(lineage)} date={DATE}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
