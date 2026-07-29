#!/usr/bin/env python3
"""Audit pair-specific H0-H4 theory dossiers.

The default mode is the formal Plan-002 gate and therefore fails on draft
scaffolds or missing native implementations. ``--inventory-only`` validates
the scaffold mechanically without admitting it as H0-H4 evidence.
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
EXPRESSION_NAMES = {"ceil", "log2"}


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--all-paper-packages", action="store_true")
    parser.add_argument("--inventory-only", action="store_true")
    parser.add_argument("--scope", choices=("all", "core"), default="all")
    parser.add_argument("--allow-draft", action="store_true")
    args = parser.parse_args()
    if args.scope == "all" and not args.all_paper_packages:
        parser.error("--all-paper-packages is required")
    registry = (
        "docs/hpc_core_target_pairs.tsv"
        if args.scope == "core"
        else "docs/hpc_required_pairs.tsv"
    )
    with (ROOT / registry).open(
        encoding="utf-8", newline=""
    ) as handle:
        rows = list(csv.DictReader(handle, delimiter="\t"))
    if not rows:
        raise RuntimeError("required pair registry is empty")
    pair_ids = set()
    missing_implementation = 0
    missing_pairs: list[str] = []
    draft_pairs: list[str] = []
    for row in rows:
        if row["pair_id"] in pair_ids:
            raise RuntimeError(f"duplicate pair {row['pair_id']}")
        pair_ids.add(row["pair_id"])
        path = ROOT / row["analysis_path"]
        if hashlib.sha256(path.read_bytes()).hexdigest() != row[
            "analysis_sha256"
        ]:
            raise RuntimeError(f"{row['pair_id']}: analysis hash differs")
        data = json.loads(path.read_text(encoding="utf-8"))
        required = {
            "H0_scientific_state_machine",
            "H1_work_and_data_movement",
            "H2_dependency_and_parallel_width",
            "H3_performance_and_granularity",
            "H4_implementation_mapping",
            "architecture",
        }
        if required - set(data):
            raise RuntimeError(f"{row['pair_id']}: missing H0-H4 section")
        if args.scope == "core":
            if data.get("review_status") != "reviewed_plan003_target_specific":
                raise RuntimeError(f"{row['pair_id']}: target review absent")
            boundary = data.get("pair_specific_boundary", "")
            if (
                row["corpus_id"] not in boundary
                or row["method_semantic_id"] not in boundary
                or row["problem_semantic_id"] not in boundary
            ):
                raise RuntimeError(
                    f"{row['pair_id']}: pair-specific boundary incomplete"
                )
        variables = data["H1_work_and_data_movement"]["defined_variables"]
        actual = data["H1_work_and_data_movement"]["actual_values"]
        if not variables or set(actual) != {
            "smallest", "representative", "largest"
        }:
            raise RuntimeError(f"{row['pair_id']}: incomplete work values")
        stages = data["H1_work_and_data_movement"]["stage_ledger"]
        if len(stages) < 5 or any(
            not stage.get("work")
            or not stage.get("span")
            or set(stage.get("actual_case_substitutions", {}))
            != {"smallest", "representative", "largest"}
            for stage in stages
        ):
            raise RuntimeError(f"{row['pair_id']}: incomplete stage ledger")
        if args.scope == "core":
            defined = set(variables)
            for stage in stages:
                identifiers = set(
                    re.findall(
                        r"[A-Za-z_][A-Za-z0-9_]*",
                        f"{stage['work']} {stage['span']}",
                    )
                )
                undefined = identifiers - defined - EXPRESSION_NAMES
                if undefined:
                    raise RuntimeError(
                        f"{row['pair_id']}: undefined work variables "
                        f"{sorted(undefined)}"
                    )
            mapping = data["H4_implementation_mapping"]
            for key in (
                "primary_symbol",
                "evaluator_symbol",
                "persistent_team_symbol",
                "stage_symbols",
                "backend_id",
            ):
                if not mapping.get(key):
                    raise RuntimeError(
                        f"{row['pair_id']}: H4 mapping lacks {key}"
                    )
            if row["corpus_id"] in {"Y36", "T42", "T45"}:
                required_training = {
                    "corpus_or_environment", "forward", "loss", "backward",
                    "gradient_aggregation", "optimizer", "artifact",
                    "inference", "optimization_loop",
                }
                if required_training - set(data.get("training_subdossier", {})):
                    raise RuntimeError(
                        f"{row['pair_id']}: learning subdossier incomplete"
                    )
        if not data["H2_dependency_and_parallel_width"]["dependency_edges"]:
            raise RuntimeError(f"{row['pair_id']}: dependency DAG is empty")
        if len(data["H1_work_and_data_movement"]["reuse_proofs"]) < 2:
            raise RuntimeError(f"{row['pair_id']}: reuse proof is absent")
        if "dispatch crossover" not in data[
            "H3_performance_and_granularity"
        ]["granularity_rule"]:
            raise RuntimeError(f"{row['pair_id']}: granularity rule is absent")
        symbol = data["H4_implementation_mapping"]["primary_symbol"]
        if (
            symbol == "planned_unimplemented_native_comparator"
            or row["implementation_status"]
            == "planned_missing_native_comparator"
        ):
            missing_implementation += 1
            missing_pairs.append(row["pair_id"])
        if (
            data.get("dossier_maturity")
            != "accepted_pair_specific_h0_h4"
            or row.get("theory_status")
            != "accepted_pair_specific_h0_h4"
        ):
            draft_pairs.append(row["pair_id"])
    if (
        not args.inventory_only
        and not args.allow_draft
        and (missing_pairs or draft_pairs)
    ):
        detail = {
            "missing_native_implementations": missing_pairs,
            "draft_unadmitted_theory_pairs": draft_pairs,
        }
        raise RuntimeError(
            "Plan-002 H0-H4 gate is blocked:\n"
            + json.dumps(detail, indent=2, sort_keys=True)
        )
    print(
        "hpc_theory_plan_inventory_pass "
        f"scope={args.scope} "
        f"pairs={len(rows)} pair_specific_json={len(rows)} "
        f"planned_missing_native_comparators={missing_implementation} "
        f"draft_unadmitted={len(draft_pairs)}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
