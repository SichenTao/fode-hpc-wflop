#!/usr/bin/env python3
"""Shared, dependency-free helpers for the Core-99 expansion registry."""

from __future__ import annotations

import csv
import hashlib
import json
from pathlib import Path
from typing import Iterable


ROOT = Path(__file__).resolve().parents[1]
EXPECTED_CORE_SHA256 = (
    "60342bf2669d26c7706300f746839a1f8d18f224c344786d6a9b732ce56c3d74"
)
EXPECTED_EXISTING_SHA256 = (
    "6a29eb43a8280158462205efccf48a6a1a8985d8e3b1cae33f808a5ac442e123"
)
EXPECTED_COUNTS = {
    "total": 99,
    "direct": 92,
    "reviews": 7,
    "retained_direct": 23,
    "new_direct_ready": 68,
    "new_direct_skipped": 1,
}
SKIPPED_PRIMARY_PDF_IDS = {"T32"}

REGISTRY_COLUMNS = (
    "corpus_id",
    "role",
    "reading_tier",
    "primary_review_class",
    "secondary_review_classes",
    "year",
    "title",
    "authors",
    "venue",
    "doi",
    "pdf_basename",
    "pdf_sha256",
    "primary_asset_status",
    "scope_status",
    "target_contribution_type",
    "target_method_or_driver",
    "target_problem_or_model",
    "method_semantic_id",
    "problem_semantic_id",
    "paper_protocol_id",
    "execution_wave",
    "package_status",
    "claim_boundary",
)

CLASS_CONTRIBUTION_TYPE = {
    "REV1": "problem_definition_or_comparison_protocol",
    "REV2": "wake_power_cost_or_physical_evaluator",
    "REV3": "optimization_search_or_learning_method",
    "REV4": "coupled_or_real_world_problem_and_method",
    "REV5": "scaling_computation_or_reproducible_execution",
}


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def read_tsv(path: Path) -> list[dict[str, str]]:
    with path.open(encoding="utf-8", newline="") as handle:
        rows = list(csv.DictReader(handle, delimiter="\t"))
    if not rows:
        raise RuntimeError(f"{path.name}: empty TSV")
    for line_number, row in enumerate(rows, start=2):
        if None in row:
            raise RuntimeError(f"{path.name}:{line_number}: extra columns")
        if any(value is None for value in row.values()):
            raise RuntimeError(f"{path.name}:{line_number}: malformed row")
    return rows


def write_tsv(
    path: Path,
    rows: Iterable[dict[str, str]],
    columns: tuple[str, ...] = REGISTRY_COLUMNS,
) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(
            handle,
            fieldnames=columns,
            delimiter="\t",
            lineterminator="\n",
            extrasaction="raise",
        )
        writer.writeheader()
        writer.writerows(rows)


def write_json(path: Path, payload: object) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(
        json.dumps(payload, indent=2, sort_keys=True, ensure_ascii=False) + "\n",
        encoding="utf-8",
    )


def normalize_doi(value: str) -> str:
    doi = value.strip().lower()
    for prefix in ("https://doi.org/", "http://doi.org/", "doi:"):
        if doi.startswith(prefix):
            doi = doi[len(prefix) :]
    return doi


def index_unique(
    rows: Iterable[dict[str, str]], key: str, label: str
) -> dict[str, dict[str, str]]:
    result: dict[str, dict[str, str]] = {}
    for row in rows:
        value = row[key].strip()
        if not value:
            raise RuntimeError(f"{label}: empty {key}")
        if value in result:
            raise RuntimeError(f"{label}: duplicate {key} {value}")
        result[value] = row
    return result


def inspect_pdf(path: Path) -> str:
    if not path.is_file():
        raise RuntimeError(f"missing PDF {path.name}")
    with path.open("rb") as handle:
        magic = handle.read(5)
    if magic != b"%PDF-":
        raise RuntimeError(f"{path.name}: invalid PDF signature")
    return sha256_file(path)


def count_states(rows: list[dict[str, str]]) -> dict[str, int]:
    return {
        "total": len(rows),
        "direct": sum(row["role"] == "D" for row in rows),
        "reviews": sum(row["role"] == "R" for row in rows),
        "retained_direct": sum(
            row["scope_status"] == "retained_direct" for row in rows
        ),
        "new_direct_ready": sum(
            row["scope_status"] == "new_direct_ready" for row in rows
        ),
        "new_direct_skipped": sum(
            row["scope_status"] == "new_direct_skipped_primary_pdf"
            for row in rows
        ),
    }
