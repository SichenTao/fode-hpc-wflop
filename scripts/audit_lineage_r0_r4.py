#!/usr/bin/env python3
"""Audit the one-row-per-paper R0-R4 completion and campaign mapping."""

from __future__ import annotations

import csv
import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
LINEAGE = ROOT / "docs/author_lineage_registry.tsv"
MATRIX = ROOT / "docs/lineage_r0_r4_completion.tsv"
CAMPAIGNS = {
    "eighteen_algorithm_cpp_hpc_waffle_v1": (
        ROOT / "formal/contracts/eighteen_algorithm_cpp_hpc_waffle_v1.json"
    ),
    "bde_source_replay_waffle_v1": (
        ROOT / "formal/contracts/bde_source_replay_waffle_v1.json"
    ),
    "pbea_six_algorithm_waffle_v1": (
        ROOT / "formal/contracts/pbea_six_algorithm_waffle_v1.json"
    ),
    "offshore_cpp_hpc_waffle_v1": (
        ROOT / "formal/contracts/offshore_cpp_hpc_waffle_v1.json"
    ),
}


def read_tsv(path: Path) -> list[dict[str, str]]:
    with path.open(newline="") as stream:
        return list(csv.DictReader(stream, delimiter="\t"))


def campaign_algorithms(path: Path) -> set[str]:
    payload = json.loads(path.read_text())
    if "algorithms" in payload:
        return set(payload["algorithms"])
    if "algorithm_id" in payload:
        return {payload["algorithm_id"]}
    return {profile["algorithm_id"] for profile in payload["profiles"]}


def main() -> int:
    lineage = read_tsv(LINEAGE)
    matrix = read_tsv(MATRIX)
    if len(lineage) != 23 or len(matrix) != 23:
        raise RuntimeError(
            "expected 23 lineage rows and 23 matrix rows, got "
            f"{len(lineage)} and {len(matrix)}"
        )
    lineage_by_id = {row["corpus_id"]: row for row in lineage}
    matrix_by_id = {row["corpus_id"]: row for row in matrix}
    if len(lineage_by_id) != 23 or len(matrix_by_id) != 23:
        raise RuntimeError("duplicate corpus_id in lineage or R0-R4 matrix")
    if set(lineage_by_id) != set(matrix_by_id):
        raise RuntimeError("lineage and R0-R4 matrix corpus IDs differ")

    admitted = {
        campaign_id: campaign_algorithms(path)
        for campaign_id, path in CAMPAIGNS.items()
    }
    blocked = 0
    pending = 0
    for corpus_id, row in matrix_by_id.items():
        source = lineage_by_id[corpus_id]
        if row["doi"].lower() != source["doi"].lower():
            raise RuntimeError(f"{corpus_id}: DOI differs from lineage registry")
        if row["r0_status"] != "pass":
            raise RuntimeError(f"{corpus_id}: R0 identity must pass")
        for relative in row["evidence_paths"].split(";"):
            evidence = ROOT / relative
            if not evidence.exists():
                raise RuntimeError(
                    f"{corpus_id}: missing evidence path {relative}"
                )

        campaign_id = row["campaign_id"]
        if campaign_id == "none":
            blocked += 1
            if row["r4_status"] != "blocked":
                raise RuntimeError(
                    f"{corpus_id}: no campaign requires blocked R4"
                )
            if "blocked" not in row["r1_status"] + row["r2_status"]:
                raise RuntimeError(
                    f"{corpus_id}: blocked R4 lacks an R1/R2 blocker"
                )
            continue
        if campaign_id not in admitted:
            raise RuntimeError(f"{corpus_id}: unknown campaign {campaign_id}")
        if row["executable_id"] not in admitted[campaign_id]:
            raise RuntimeError(
                f"{corpus_id}: {row['executable_id']} absent from {campaign_id}"
            )
        if row["r4_status"] != "pending_waffle":
            raise RuntimeError(
                f"{corpus_id}: admitted row must be pending Waffle before receipt"
            )
        if row["r3_status"] != "pass_development":
            raise RuntimeError(
                f"{corpus_id}: formal mapping lacks development R3 admission"
            )
        pending += 1

    if blocked != 4 or pending != 19:
        raise RuntimeError(
            "expected 4 blocked and 19 Waffle-pending papers, got "
            f"{blocked} and {pending}"
        )
    print(
        "lineage_r0_r4_audit_pass "
        f"papers={len(matrix)} blocked={blocked} pending_waffle={pending}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
