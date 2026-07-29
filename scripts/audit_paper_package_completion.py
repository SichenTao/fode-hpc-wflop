#!/usr/bin/env python3
"""Audit the paper-paired Gao-Tao WFLOP completion authority.

The inventory is append-only.  Later phases progressively replace explicit
``pending`` and ``open`` values; this audit never upgrades an unresolved row.
"""

from __future__ import annotations

import argparse
import csv
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
MATRIX = ROOT / "docs/paper_package_completion.tsv"
PROTOCOLS = ROOT / "docs/paper_experiment_protocols.tsv"
GAPS = ROOT / "docs/paper_asset_gap_ledger.tsv"
LINEAGE = ROOT / "docs/author_lineage_registry.tsv"

MATRIX_COLUMNS = {
    "corpus_id", "doi", "target_algorithm", "method_semantic_id",
    "paper_native_problem_id", "problem_semantic_id", "training_artifact_id",
    "paper_protocol_id", "conflict_variants", "comparator_set", "r0_status",
    "r1_status", "r2_status", "r3_status", "r4_status", "required_pair_ids",
    "hpc_analysis_id", "theory_status", "architecture_id",
    "implementation_traceability_status", "performance_receipt_hash",
    "parallel_coverage_status", "serial_waiver_id", "hpc_claim_class",
    "native_formal_campaign_id", "formal_run_count", "formal_result_status",
    "claim_boundary",
}
PROTOCOL_COLUMNS = {
    "corpus_id", "paper_protocol_id", "problem_semantic_id", "case_matrix",
    "physical_budget", "comparator_algorithms", "ablations",
    "objective_and_direction", "metrics", "paper_repeat_count",
    "project_repeat_count", "convergence_sampling", "protocol_source",
    "protocol_status",
}


def read_tsv(path: Path) -> list[dict[str, str]]:
    with path.open(encoding="utf-8", newline="") as handle:
        rows = list(csv.DictReader(handle, delimiter="\t"))
    if not rows:
        raise RuntimeError(f"{path.relative_to(ROOT)} is empty")
    for line, row in enumerate(rows, start=2):
        if None in row:
            raise RuntimeError(
                f"{path.relative_to(ROOT)}:{line}: unexpected extra columns"
            )
        missing = [key for key, value in row.items() if not value.strip()]
        if missing:
            raise RuntimeError(
                f"{path.relative_to(ROOT)}:{line}: empty fields {missing}"
            )
    return rows


def index(rows: list[dict[str, str]], name: str) -> dict[str, dict[str, str]]:
    result: dict[str, dict[str, str]] = {}
    for row in rows:
        corpus_id = row["corpus_id"]
        if corpus_id in result:
            raise RuntimeError(f"duplicate {name} corpus_id {corpus_id}")
        result[corpus_id] = row
    return result


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--phase",
        choices=("inventory", "evidence", "admission", "formal", "closure"),
        default="inventory",
    )
    parser.add_argument("--family", default="")
    args = parser.parse_args()

    lineage = index(read_tsv(LINEAGE), "lineage")
    matrix_rows = read_tsv(MATRIX)
    protocol_rows = read_tsv(PROTOCOLS)
    gap_rows = read_tsv(GAPS)
    matrix = index(matrix_rows, "matrix")
    protocols = index(protocol_rows, "protocol")

    missing_matrix_columns = MATRIX_COLUMNS.difference(matrix_rows[0])
    missing_protocol_columns = PROTOCOL_COLUMNS.difference(protocol_rows[0])
    if missing_matrix_columns:
        raise RuntimeError(f"matrix missing columns {sorted(missing_matrix_columns)}")
    if missing_protocol_columns:
        raise RuntimeError(
            f"protocol table missing columns {sorted(missing_protocol_columns)}"
        )
    if set(matrix) != set(lineage) or set(protocols) != set(lineage):
        raise RuntimeError(
            "paper authority coverage mismatch "
            f"lineage={len(lineage)} matrix={len(matrix)} protocols={len(protocols)}"
        )

    gaps_by_paper: dict[str, list[dict[str, str]]] = {}
    seen_gap_ids: set[str] = set()
    for row in gap_rows:
        if row["gap_id"] in seen_gap_ids:
            raise RuntimeError(f"duplicate gap_id {row['gap_id']}")
        seen_gap_ids.add(row["gap_id"])
        if row["corpus_id"] not in lineage:
            raise RuntimeError(f"gap outside lineage: {row['corpus_id']}")
        gaps_by_paper.setdefault(row["corpus_id"], []).append(row)

    for corpus_id, row in matrix.items():
        if row["doi"].lower() != lineage[corpus_id]["doi"].lower():
            raise RuntimeError(f"{corpus_id}: DOI mismatch")
        protocol = protocols[corpus_id]
        if row["paper_protocol_id"] != protocol["paper_protocol_id"]:
            raise RuntimeError(f"{corpus_id}: protocol ID mismatch")
        if row["problem_semantic_id"] != protocol["problem_semantic_id"]:
            raise RuntimeError(f"{corpus_id}: problem semantic mismatch")
        if protocol["project_repeat_count"] != "25":
            raise RuntimeError(f"{corpus_id}: project repeat count must equal 25")
        if "transfer" in row["paper_native_problem_id"].lower():
            raise RuntimeError(
                f"{corpus_id}: transfer cannot be the primary native problem"
            )
        if row["formal_result_status"] == "complete":
            if row["theory_status"] != "accepted_h0_h4":
                raise RuntimeError(f"{corpus_id}: formal result lacks H0-H4")
            if row["hpc_claim_class"] not in {
                "optimized_parallel", "serial_limited"
            }:
                raise RuntimeError(f"{corpus_id}: invalid HPC claim class")

    unresolved = sum(
        row["formal_result_status"] not in {"complete", "historical_exact_reuse"}
        for row in matrix_rows
    )
    if args.phase in {"evidence", "admission", "formal", "closure"}:
        for corpus_id, row in matrix.items():
            if any(
                token in row["r0_status"].lower()
                for token in ("pending", "blocked", "not_started")
            ):
                raise RuntimeError(f"{corpus_id}: evidence phase R0 incomplete")
    if args.phase in {"admission", "formal", "closure"}:
        for corpus_id, row in matrix.items():
            if row["theory_status"] != "accepted_h0_h4":
                raise RuntimeError(f"{corpus_id}: admission lacks H0-H4")
            if row["implementation_traceability_status"] != "accepted":
                raise RuntimeError(
                    f"{corpus_id}: admission lacks implementation traceability"
                )
    if args.phase in {"formal", "closure"} and unresolved:
        raise RuntimeError(f"{unresolved} paper rows are not formally complete")

    print(
        "paper_package_completion_audit_pass "
        f"phase={args.phase} papers={len(matrix_rows)} "
        f"protocols={len(protocol_rows)} gaps={len(gap_rows)} "
        f"unresolved_formal={unresolved}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
