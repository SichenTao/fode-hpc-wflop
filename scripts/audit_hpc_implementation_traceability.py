#!/usr/bin/env python3
"""Verify that every required paper pair maps to a real C++ symbol and H5-H6.

The formal mode fails closed. ``--inventory-only`` is provided solely to
materialize an exact development blocker list without upgrading draft work.
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


def trace(row: dict[str, str]) -> list[str]:
    reasons: list[str] = []
    analysis = json.loads(
        (ROOT / row["analysis_path"]).read_text(encoding="utf-8")
    )
    mapping = analysis["H4_implementation_mapping"]["primary_symbol"]
    if mapping == "planned_unimplemented_native_comparator":
        reasons.append("native_implementation_absent")
    elif "::" not in mapping:
        reasons.append("malformed_source_symbol")
    else:
        relative, symbol = mapping.split("::", 1)
        source = ROOT / relative
        if not source.is_file():
            reasons.append("source_file_absent")
        elif symbol not in source.read_text(encoding="utf-8"):
            reasons.append("source_symbol_absent")
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
    blockers = {
        row["pair_id"]: trace(row)
        for row in rows(args.scope)
    }
    blockers = {pair: reasons for pair, reasons in blockers.items() if reasons}
    if blockers and not args.inventory_only:
        raise RuntimeError(
            "Plan-002 implementation traceability is blocked:\n"
            + json.dumps(blockers, indent=2, sort_keys=True)
        )
    print(
        "hpc_implementation_traceability_inventory_pass "
        f"scope={args.scope} backend={args.backend or 'all'} "
        f"pairs={len(rows(args.scope))} blocked_pairs={len(blockers)}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
