#!/usr/bin/env python3
"""Verify that every required paper pair maps to a real C++ implementation.

Core CPU mode is the Plan-003 Step-3 gate: it verifies the complete H0-H4
stage-to-source mapping without requiring the later H5-H6 admission receipt.
All-package mode retains the fail-closed H5-H6 requirement.
"""

from __future__ import annotations

import argparse
import csv
import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def rows(scope: str = "all") -> list[dict[str, str]]:
    registry = (
        "docs/hpc_core_target_pairs.tsv"
        if scope == "core"
        else "docs/hpc_required_pairs.tsv"
    )
    with (ROOT / registry).open(
        encoding="utf-8", newline=""
    ) as handle:
        return list(csv.DictReader(handle, delimiter="\t"))


def check_symbol(mapping: str, label: str, reasons: list[str]) -> None:
    if not mapping or "::" not in mapping:
        reasons.append(f"{label}_malformed")
        return
    relative, symbol = mapping.split("::", 1)
    source = ROOT / relative
    if not source.is_file():
        reasons.append(f"{label}_source_file_absent")
    elif symbol not in source.read_text(encoding="utf-8"):
        reasons.append(f"{label}_source_symbol_absent")


def trace(
    row: dict[str, str], *, scope: str, backend: str | None
) -> list[str]:
    reasons: list[str] = []
    analysis = json.loads(
        (ROOT / row["analysis_path"]).read_text(encoding="utf-8")
    )
    h4 = analysis["H4_implementation_mapping"]
    mapping = h4["primary_symbol"]
    if mapping == "planned_unimplemented_native_comparator":
        reasons.append("native_implementation_absent")
    else:
        check_symbol(mapping, "primary", reasons)

    if scope == "core" and backend == "cpu":
        stage_ids = list(
            analysis["H0_scientific_state_machine"]["stages"]
        )
        stage_symbols = h4.get("stage_symbols", {})
        if set(stage_ids) != set(stage_symbols):
            reasons.append("stage_symbol_coverage_mismatch")
        for stage_id in stage_ids:
            check_symbol(
                stage_symbols.get(stage_id, ""),
                f"stage_{stage_id}",
                reasons,
            )
        check_symbol(
            h4.get("evaluator_symbol", ""), "evaluator", reasons
        )
        check_symbol(
            h4.get("persistent_team_symbol", ""),
            "persistent_team",
            reasons,
        )
        if (
            h4.get("pair_specific_composition", {}).get(
                "ordered_stage_ids"
            )
            != stage_ids
        ):
            reasons.append("pair_specific_stage_order_mismatch")
        return reasons

    validation = Path(row["analysis_path"]).with_name(
        Path(row["analysis_path"]).name.replace(
            "_hpc_analysis.json", "_hpc_validation.json"
        )
    )
    if not (ROOT / validation).is_file():
        reasons.append("h5_h6_validation_absent")
    if row.get("validation_status") != "accepted_h5_h6":
        reasons.append("h5_h6_not_accepted")
    return reasons


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--all-paper-packages", action="store_true")
    parser.add_argument("--inventory-only", action="store_true")
    parser.add_argument("--scope", choices=("all", "core"), default="all")
    parser.add_argument("--backend", choices=("cpu", "gpu", "hybrid"))
    args = parser.parse_args()
    if args.scope == "all" and not args.all_paper_packages:
        parser.error("--all-paper-packages is required")
    if args.scope == "core" and args.backend != "cpu":
        parser.error("Plan-003 Step-3 core traceability requires --backend cpu")
    blockers = {
        row["pair_id"]: trace(
            row, scope=args.scope, backend=args.backend
        )
        for row in rows(args.scope)
    }
    blockers = {pair: reasons for pair, reasons in blockers.items() if reasons}
    if blockers and not args.inventory_only:
        raise RuntimeError(
            "HPC implementation traceability is blocked:\n"
            + json.dumps(blockers, indent=2, sort_keys=True)
        )
    print(
        "hpc_implementation_traceability_pass "
        f"scope={args.scope} backend={args.backend or 'all'} "
        f"pairs={len(rows(args.scope))} blocked_pairs={len(blockers)} "
        f"h5_h6_required={'no' if args.scope == 'core' else 'yes'}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
