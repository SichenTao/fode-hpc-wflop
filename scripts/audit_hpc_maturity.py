#!/usr/bin/env python3
"""Independently reconstruct the paper-pair HPC maturity gate."""

from __future__ import annotations

import argparse
import csv
import json
from collections import Counter
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read_tsv(path: Path) -> list[dict[str, str]]:
    with path.open(encoding="utf-8", newline="") as handle:
        return list(csv.DictReader(handle, delimiter="\t"))


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--all-paper-packages", action="store_true")
    parser.add_argument("--inventory-only", action="store_true")
    parser.add_argument("--scope", choices=("all", "core"), default="all")
    args = parser.parse_args()
    if args.scope == "all" and not args.all_paper_packages:
        parser.error("--all-paper-packages is required")

    papers = read_tsv(ROOT / "docs/paper_package_completion.tsv")
    protocols = {
        row["corpus_id"]: row
        for row in read_tsv(ROOT / "docs/paper_experiment_protocols.tsv")
    }
    pairs = read_tsv(
        ROOT
        / (
            "docs/hpc_core_target_pairs.tsv"
            if args.scope == "core"
            else "docs/hpc_required_pairs.tsv"
        )
    )
    by_paper: dict[str, list[dict[str, str]]] = {}
    for row in pairs:
        by_paper.setdefault(row["corpus_id"], []).append(row)

    blockers: dict[str, list[str]] = {}
    for paper in papers:
        corpus = paper["corpus_id"]
        reasons: list[str] = []
        native_pairs = by_paper.get(corpus, [])
        if corpus not in protocols:
            reasons.append("paper_protocol_absent")
        if not native_pairs:
            reasons.append("required_pair_registry_absent")
        if sum(row["role"] == "target" for row in native_pairs) != 1:
            reasons.append("target_pair_cardinality_not_one")
        if any(
            row["implementation_status"]
            != "executable_baseline"
            for row in native_pairs
        ):
            reasons.append("native_algorithm_or_comparator_absent")
        if any(
            row.get("theory_status")
            != "accepted_pair_specific_h0_h4"
            for row in native_pairs
        ):
            reasons.append("h0_h4_not_accepted")
        if any(
            row["validation_status"] != "accepted_h5_h6"
            for row in native_pairs
        ):
            reasons.append("h5_h6_not_accepted")
        if reasons:
            blockers[corpus] = reasons

    if args.scope == "all":
        taae = json.loads(
            (
                ROOT
                / "evidence/development/"
                "taae_paper_scale_cpu_feasibility_spark2_20260730.json"
            ).read_text(encoding="utf-8")
        )
        if taae["status"] == "profile_specific_stop":
            blockers.setdefault("Y36", []).append(
                "paper_scale_training_profile_specific_stop"
            )

    status_counts = Counter(row["implementation_status"] for row in pairs)
    result = {
        "paper_count": len(papers),
        "required_pair_count": len(pairs),
        "implementation_status_counts": dict(status_counts),
        "blocked_papers": blockers,
    }
    if blockers and not args.inventory_only:
        raise RuntimeError(
            "Plan-002 HPC maturity is blocked:\n"
            + json.dumps(result, indent=2, sort_keys=True)
        )
    print(
        "hpc_maturity_inventory_pass "
        f"scope={args.scope} "
        f"papers={len(papers)} pairs={len(pairs)} "
        f"blocked_papers={len(blockers)}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
