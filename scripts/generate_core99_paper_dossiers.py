#!/usr/bin/env python3
"""Create one public-safe evidence dossier for every non-retained Core-99 item."""

from __future__ import annotations

import argparse
import json
from pathlib import Path

from core99_common import ROOT, read_tsv, sha256_file, write_json


AUTHORITY_LADDER = (
    "paper_and_supplement",
    "paper_linked_public_source_and_data",
    "cited_predecessor",
    "same_lineage_public_source",
    "deterministic_declared_reconstruction",
)
SEMANTIC_FIELDS = (
    "decision_variables",
    "encoding_and_decoder",
    "objective_and_direction",
    "constraints_and_repair",
    "wake_power_cost_or_coupled_model",
    "wind_and_scenario_sampling",
    "initialization",
    "update_order",
    "stopping_and_physical_fes",
    "parameters",
    "training_and_inference_lifecycle",
    "paper_native_cases",
    "metrics_and_repeats",
)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--registry",
        type=Path,
        default=ROOT / "docs/core99_expansion_registry.tsv",
    )
    parser.add_argument(
        "--text-index",
        type=Path,
        default=ROOT / ".source-cache/core99-pdf-text/index.json",
    )
    parser.add_argument(
        "--source-mentions",
        type=Path,
        default=ROOT / "evidence/core99/source-mentions",
    )
    parser.add_argument(
        "--output-root",
        type=Path,
        default=ROOT / "evidence/core99/paper-dossiers",
    )
    parser.add_argument(
        "--admissions-manifest",
        type=Path,
        default=ROOT / "shared/contracts/core99_paper_admissions.json",
    )
    return parser.parse_args()


def pending_contract(role: str) -> dict[str, dict[str, str]]:
    status = "not_applicable_review" if role == "R" else "pending_fulltext_audit"
    return {
        field: {
            "status": status,
            "authority": "not_assigned",
            "resolution": "not_assigned",
        }
        for field in SEMANTIC_FIELDS
    }


def main() -> int:
    args = parse_args()
    rows = [
        row
        for row in read_tsv(args.registry)
        if row["scope_status"] != "retained_direct"
    ]
    text_index = json.loads(args.text_index.read_text(encoding="utf-8"))
    text_by_id = {
        record["corpus_id"]: record for record in text_index["records"]
    }
    admission_by_id: dict[str, dict] = {}
    if args.admissions_manifest.is_file():
        admissions = json.loads(
            args.admissions_manifest.read_text(encoding="utf-8")
        )
        admission_by_id = admissions["papers"]
    output_index: list[dict[str, str]] = []
    for row in rows:
        corpus_id = row["corpus_id"]
        skipped = row["scope_status"] == "new_direct_skipped_primary_pdf"
        mention_path = args.source_mentions / f"{corpus_id}.json"
        mention = (
            json.loads(mention_path.read_text(encoding="utf-8"))
            if mention_path.is_file()
            else {
                "candidate_public_asset_urls": [],
                "embedded_annotation_asset_urls": [],
                "availability_mention_line_numbers": [],
            }
        )
        text_receipt = text_by_id.get(corpus_id)
        if skipped and text_receipt is not None:
            raise RuntimeError(f"{corpus_id}: skipped paper has extracted text")
        if not skipped and text_receipt is None:
            raise RuntimeError(f"{corpus_id}: missing private text receipt")

        dossier_status = (
            "blocked_missing_primary_pdf"
            if skipped
            else (
                "review_evidence_audit_ready"
                if row["role"] == "R"
                else "pre_code_fulltext_audit_ready"
            )
        )
        dossier = {
            "schema_version": 1,
            "corpus_id": corpus_id,
            "paper": {
                "title": row["title"],
                "authors": row["authors"],
                "year": int(row["year"]),
                "venue": row["venue"],
                "doi": row["doi"],
                "role": row["role"],
                "reading_tier": row["reading_tier"],
                "primary_review_class": row["primary_review_class"],
                "secondary_review_classes": row["secondary_review_classes"],
                "pdf_basename": row["pdf_basename"],
                "pdf_sha256": row["pdf_sha256"],
                "primary_asset_status": row["primary_asset_status"],
            },
            "scope": {
                "scope_status": row["scope_status"],
                "target_contribution_type": row[
                    "target_contribution_type"
                ],
                "execution_wave": row["execution_wave"],
                "package_status": row["package_status"],
                "claim_boundary": row["claim_boundary"],
            },
            "private_fulltext_receipt": (
                {
                    "consumed": True,
                    "page_count": text_receipt["page_count"],
                    "text_sha256": text_receipt["text_sha256"],
                }
                if text_receipt is not None
                else {
                    "consumed": False,
                    "reason": "primary PDF explicitly skipped",
                }
            ),
            "paper_linked_public_asset_candidates": mention[
                "candidate_public_asset_urls"
            ],
            "public_asset_candidate_evidence": {
                "embedded_annotation_urls": mention[
                    "embedded_annotation_asset_urls"
                ],
                "availability_mention_line_numbers": mention[
                    "availability_mention_line_numbers"
                ],
                "status": (
                    "candidate_only_requires_revision_license_semantic_audit"
                    if mention["candidate_public_asset_urls"]
                    else "no_paper_linked_public_asset_candidate_detected"
                ),
            },
            "authority_ladder": list(AUTHORITY_LADDER),
            "semantic_contract": pending_contract(row["role"]),
            "paper_source_conflicts": [],
            "missing_information": (
                ["primary PDF"]
                if skipped
                else ["full field-level audit not yet completed"]
            ),
            "completion_decisions": [],
            "hpc_admission": {
                f"H{level}": "not_started" for level in range(7)
            },
            "formal_status": "not_started",
            "dossier_status": dossier_status,
        }
        if corpus_id in admission_by_id:
            admission = admission_by_id[corpus_id]
            for key, value in admission.items():
                if key == "scope":
                    dossier["scope"].update(value)
                else:
                    dossier[key] = value
        output_path = args.output_root / f"{corpus_id}.json"
        write_json(output_path, dossier)
        output_index.append(
            {
                "corpus_id": corpus_id,
                "role": row["role"],
                "dossier_status": dossier["dossier_status"],
                "dossier_sha256": sha256_file(output_path),
            }
        )
    write_json(
        args.output_root / "index.json",
        {
            "schema_version": 1,
            "record_count": len(output_index),
            "records": output_index,
        },
    )
    counts = {
        "direct_ready_or_admitted": sum(
            row["role"] != "R"
            and row["dossier_status"] != "blocked_missing_primary_pdf"
            for row in output_index
        ),
        "direct_skipped": sum(
            row["dossier_status"] == "blocked_missing_primary_pdf"
            for row in output_index
        ),
        "reviews": sum(row["role"] == "R" for row in output_index),
    }
    print(
        "core99_dossiers_generated "
        f"records={len(output_index)} "
        f"direct_ready_or_admitted={counts['direct_ready_or_admitted']} "
        f"direct_skipped={counts['direct_skipped']} "
        f"reviews={counts['reviews']}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
