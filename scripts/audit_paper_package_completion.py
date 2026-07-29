#!/usr/bin/env python3
"""Audit the paper-paired Gao-Tao WFLOP completion authority.

The inventory is append-only.  Later phases progressively replace explicit
``pending`` and ``open`` values; this audit never upgrades an unresolved row.
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
MATRIX = ROOT / "docs/paper_package_completion.tsv"
PROTOCOLS = ROOT / "docs/paper_experiment_protocols.tsv"
GAPS = ROOT / "docs/paper_asset_gap_ledger.tsv"
LINEAGE = ROOT / "docs/author_lineage_registry.tsv"
SCALAR_REGISTRY = ROOT / "docs/scalar_problem_package_registry.tsv"

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


def canonical_hash(value: object) -> str:
    encoded = json.dumps(
        value, sort_keys=True, separators=(",", ":"), ensure_ascii=True
    ).encode()
    return hashlib.sha256(encoded).hexdigest()


def audit_scalar_discrete(
    matrix: dict[str, dict[str, str]],
    protocols: dict[str, dict[str, str]],
) -> int:
    rows = read_tsv(SCALAR_REGISTRY)
    scalar = index(rows, "scalar package")
    expected_ids = {
        "S03", "L0608", "T37", "T38", "T39", "S05", "T41",
        "T47", "S01", "S02", "Y34", "T40", "Y35",
    }
    if set(scalar) != expected_ids:
        raise RuntimeError(
            "scalar package coverage mismatch "
            f"expected={sorted(expected_ids)} found={sorted(scalar)}"
        )
    seen_contracts: dict[Path, dict] = {}
    total_cases = 0
    for corpus_id, row in scalar.items():
        authority = matrix[corpus_id]
        protocol = protocols[corpus_id]
        if authority["target_algorithm"].lower() != row["target_algorithm"].lower():
            raise RuntimeError(f"{corpus_id}: scalar target algorithm mismatch")
        if authority["paper_native_problem_id"] != row["problem_id"]:
            raise RuntimeError(f"{corpus_id}: scalar problem ID mismatch")
        if authority["problem_semantic_id"] != row["problem_semantic_id"]:
            raise RuntimeError(f"{corpus_id}: scalar semantic ID mismatch")
        if protocol["problem_semantic_id"] != row["problem_semantic_id"]:
            raise RuntimeError(f"{corpus_id}: protocol semantic ID mismatch")
        if row["native_status"] not in {"executable", "executable_reconstruction"}:
            raise RuntimeError(f"{corpus_id}: native scalar package is not executable")
        contract_path = ROOT / row["case_contract"]
        if not contract_path.is_file():
            raise RuntimeError(f"{corpus_id}: missing {row['case_contract']}")
        contract = seen_contracts.get(contract_path)
        if contract is None:
            contract = json.loads(contract_path.read_text(encoding="utf-8"))
            seen_contracts[contract_path] = contract
        cases = contract.get("cases", [])
        expected_count = int(row["expected_cases"])
        if len(cases) != expected_count:
            raise RuntimeError(
                f"{corpus_id}: expected {expected_count} cases, found {len(cases)}"
            )
        if contract.get("case_count") != expected_count:
            raise RuntimeError(f"{corpus_id}: case_count field mismatch")
        contract_semantics = (
            row["problem_semantic_id"]
            if contract_path.name == "benchmark_cases.json"
            else contract.get(
                "semantics_id",
                cases[0].get("semantics_id") if cases else "",
            )
        )
        if contract_semantics != row["problem_semantic_id"]:
            raise RuntimeError(f"{corpus_id}: case contract semantic mismatch")
        contract_budget = contract.get("physical_fes_budget")
        if contract_budget is not None and contract_budget != int(row["physical_fes"]):
            raise RuntimeError(f"{corpus_id}: physical-FES budget mismatch")
        if "contract_hash" in contract:
            frozen = contract.pop("contract_hash")
            actual = canonical_hash(contract)
            contract["contract_hash"] = frozen
            if actual != frozen:
                raise RuntimeError(f"{corpus_id}: contract hash mismatch")
        case_ids: set[str] = set()
        for case in cases:
            case_id = case["case_id"]
            if case_id in case_ids:
                raise RuntimeError(f"{corpus_id}: duplicate case {case_id}")
            case_ids.add(case_id)
            probabilities = [
                float(value)
                for probability_row in case["joint_probabilities"]
                for value in probability_row
            ]
            if abs(sum(probabilities) - 1.0) > 1.0e-4:
                raise RuntimeError(f"{corpus_id}/{case_id}: probability mass")
            available = (
                int(case["rows"]) * int(case["cols"])
                - len(case["unavailable_cells_1based"])
            )
            if int(case["turbine_count"]) > available:
                raise RuntimeError(f"{corpus_id}/{case_id}: infeasible turbine count")
            for model_field in (
                "cell_width", "rotor_diameter", "hub_height",
                "surface_roughness", "wake_deficit_coefficient",
                "power_curve_cubic_coefficient", "power_curve_rated_kw",
                "power_curve_cutin_mps", "power_curve_rated_mps",
                "power_curve_cutout_mps",
            ):
                if contract_path.name != "benchmark_cases.json" and model_field not in case:
                    raise RuntimeError(
                        f"{corpus_id}/{case_id}: missing {model_field}"
                    )
        total_cases += expected_count
    print(
        "scalar_discrete_package_audit_pass "
        f"papers={len(rows)} unique_contracts={len(seen_contracts)} "
        f"paper_case_rows={total_cases}"
    )
    return len(rows)


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
    if args.family:
        if args.family != "scalar_discrete":
            raise RuntimeError(f"unknown paper family: {args.family}")
        audit_scalar_discrete(matrix, protocols)

    print(
        "paper_package_completion_audit_pass "
        f"phase={args.phase} papers={len(matrix_rows)} "
        f"protocols={len(protocol_rows)} gaps={len(gap_rows)} "
        f"unresolved_formal={unresolved}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
