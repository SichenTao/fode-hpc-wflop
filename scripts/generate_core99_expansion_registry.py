#!/usr/bin/env python3
"""Freeze the Core-99 paper scope without embedding private filesystem paths."""

from __future__ import annotations

import argparse
from pathlib import Path

from core99_common import (
    CLASS_CONTRIBUTION_TYPE,
    EXPECTED_CORE_SHA256,
    EXPECTED_COUNTS,
    EXPECTED_EXISTING_SHA256,
    REGISTRY_COLUMNS,
    ROOT,
    SKIPPED_PRIMARY_PDF_IDS,
    count_states,
    index_unique,
    inspect_pdf,
    normalize_doi,
    read_tsv,
    sha256_file,
    write_json,
    write_tsv,
)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--core-registry", type=Path, required=True)
    parser.add_argument("--existing", type=Path, required=True)
    parser.add_argument("--pdf-root", type=Path, required=True)
    parser.add_argument(
        "--output",
        type=Path,
        default=ROOT / "docs/core99_expansion_registry.tsv",
    )
    parser.add_argument(
        "--receipt",
        type=Path,
        default=ROOT / "docs/core99_scope_receipt.json",
    )
    return parser.parse_args()


def retained_fields(
    existing: dict[str, dict[str, str]], corpus_id: str
) -> tuple[str, str, str, str, str]:
    row = existing[corpus_id]
    return (
        row["target_algorithm"],
        row["paper_native_problem_id"],
        row["method_semantic_id"],
        row["problem_semantic_id"],
        row["paper_protocol_id"],
    )


def main() -> int:
    args = parse_args()
    core_hash = sha256_file(args.core_registry)
    existing_hash = sha256_file(args.existing)
    if core_hash != EXPECTED_CORE_SHA256:
        raise RuntimeError(
            "Core registry drift: "
            f"expected {EXPECTED_CORE_SHA256}, found {core_hash}"
        )
    if existing_hash != EXPECTED_EXISTING_SHA256:
        raise RuntimeError(
            "existing package registry drift: "
            f"expected {EXPECTED_EXISTING_SHA256}, found {existing_hash}"
        )

    core_rows = read_tsv(args.core_registry)
    existing_rows = read_tsv(args.existing)
    core = index_unique(core_rows, "corpus_id", "Core-99")
    existing = index_unique(existing_rows, "corpus_id", "retained packages")
    unknown_existing = sorted(set(existing) - set(core))
    if unknown_existing:
        raise RuntimeError(
            f"retained packages absent from Core-99: {unknown_existing}"
        )

    output_rows: list[dict[str, str]] = []
    for source in core_rows:
        corpus_id = source["corpus_id"].strip()
        role = source["role"].strip()
        review_class = source["primary_review_class"].strip()
        pdf_basename = source["pdf"].strip()
        if corpus_id in SKIPPED_PRIMARY_PDF_IDS:
            if pdf_basename:
                raise RuntimeError(
                    f"{corpus_id}: skip exception unexpectedly has a PDF name"
                )
            pdf_hash = "not_available"
            primary_asset_status = "missing_primary_pdf_explicit_skip"
        else:
            if not pdf_basename:
                raise RuntimeError(f"{corpus_id}: missing PDF basename")
            pdf_hash = inspect_pdf(args.pdf_root / pdf_basename)
            primary_asset_status = "local_primary_pdf_hashed"

        if role == "R":
            scope_status = "review_evidence_package"
            target_type = "review_evidence_taxonomy_and_protocol"
            method_or_driver = "not_applicable"
            problem_or_model = f"{corpus_id.lower()}_review_evidence_v1"
            method_id = "not_applicable"
            problem_id = f"{corpus_id.lower()}_review_evidence_v1"
            protocol_id = f"{corpus_id.lower()}_review_protocol_v1"
            package_status = "queued_evidence_dossier"
            claim_boundary = (
                "review evidence and taxonomy package; no invented optimizer"
            )
        elif corpus_id in existing:
            (
                method_or_driver,
                problem_or_model,
                method_id,
                problem_id,
                protocol_id,
            ) = retained_fields(existing, corpus_id)
            scope_status = "retained_direct"
            target_type = "retained_target_contribution"
            package_status = "retained_plan005_package"
            claim_boundary = existing[corpus_id]["claim_boundary"]
        elif corpus_id in SKIPPED_PRIMARY_PDF_IDS:
            scope_status = "new_direct_skipped_primary_pdf"
            target_type = CLASS_CONTRIBUTION_TYPE[review_class]
            method_or_driver = "pending_primary_pdf"
            problem_or_model = "pending_primary_pdf"
            method_id = "pending_primary_pdf"
            problem_id = "pending_primary_pdf"
            protocol_id = "pending_primary_pdf"
            package_status = "blocked_missing_primary_pdf"
            claim_boundary = (
                "scope identity only; primary PDF required before reproduction"
            )
        else:
            scope_status = "new_direct_ready"
            target_type = CLASS_CONTRIBUTION_TYPE[review_class]
            method_or_driver = "pending_full_text_contract"
            problem_or_model = "pending_full_text_contract"
            method_id = f"{corpus_id.lower()}_method_pending_contract"
            problem_id = f"{corpus_id.lower()}_problem_pending_contract"
            protocol_id = f"{corpus_id.lower()}_protocol_pending_contract"
            package_status = "queued_full_text_contract"
            claim_boundary = (
                "scope and primary PDF verified; scientific contract pending "
                "paper/source reconciliation"
            )

        output_rows.append(
            {
                "corpus_id": corpus_id,
                "role": role,
                "reading_tier": source["reading_tier"].strip(),
                "primary_review_class": review_class,
                "secondary_review_classes": source[
                    "secondary_review_classes"
                ].strip(),
                "year": source["year"].strip(),
                "title": source["title"].strip(),
                "authors": source["authors"].strip(),
                "venue": source["venue"].strip(),
                "doi": normalize_doi(source["doi_or_identifier"]),
                "pdf_basename": pdf_basename or "not_available",
                "pdf_sha256": pdf_hash,
                "primary_asset_status": primary_asset_status,
                "scope_status": scope_status,
                "target_contribution_type": target_type,
                "target_method_or_driver": method_or_driver,
                "target_problem_or_model": problem_or_model,
                "method_semantic_id": method_id,
                "problem_semantic_id": problem_id,
                "paper_protocol_id": protocol_id,
                "execution_wave": review_class,
                "package_status": package_status,
                "claim_boundary": claim_boundary,
            }
        )

    counts = count_states(output_rows)
    if counts != EXPECTED_COUNTS:
        raise RuntimeError(
            f"Core-99 count mismatch expected={EXPECTED_COUNTS} found={counts}"
        )
    write_tsv(args.output, output_rows, REGISTRY_COLUMNS)
    registry_hash = sha256_file(args.output)
    write_json(
        args.receipt,
        {
            "schema_version": 1,
            "scope_id": "wflop_core99_expansion_20260731_v1",
            "core_registry_basename": args.core_registry.name,
            "core_registry_sha256": core_hash,
            "existing_registry_basename": args.existing.name,
            "existing_registry_sha256": existing_hash,
            "pdf_root_identity": "private_core99_primary_pdf_collection",
            "pdf_verification": "basename, PDF signature, and SHA-256",
            "expansion_registry_basename": args.output.name,
            "expansion_registry_sha256": registry_hash,
            "counts": counts,
            "explicit_skip_ids": sorted(SKIPPED_PRIMARY_PDF_IDS),
        },
    )
    print(
        "core99_registry_generated "
        f"rows={counts['total']} retained={counts['retained_direct']} "
        f"new_ready={counts['new_direct_ready']} "
        f"skipped={counts['new_direct_skipped']} "
        f"reviews={counts['reviews']} sha256={registry_hash}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
