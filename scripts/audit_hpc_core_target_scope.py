#!/usr/bin/env python3
"""Audit the exact 23 Plan-003 target algorithm/problem tuples."""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
CORE = ROOT / "docs/hpc_core_target_pairs.tsv"
FULL = ROOT / "docs/hpc_required_pairs.tsv"

MANDATORY = {
    ("Y36", "taae_transformer_evolution_declared_reconstruction_v1",
     "taae_zhangbei_structured_declared_proxy_v1"),
    ("L0608", "alshade", "alshade_complex_wake_117"),
    ("T42", "rlpso_paper_corrected_training_reconstruction_v1",
     "rpso2024_source_problem_ws1_ws4"),
    ("T37", "agpso", "agpso_aiga_hgpso_landuse_156"),
    ("T45", "alga_attention_declared_reconstruction_v1",
     "alga_guishan_3d_declared_proxy_v1"),
    ("T38", "clshade", "clshade_landuse_117"),
    ("T39", "ise", "ise_landuse_117"),
    ("T46", "moead_p", "zhang2025_three_objective"),
    ("S04", "fqfode_seeded_training_declared_reconstruction_v1",
     "fode_e0_common"),
    ("S03", "fode", "fode_e0_common"),
    ("S05", "wfadde", "wfadde_native_declared_24"),
    ("T41", "aiga", "agpso_aiga_hgpso_landuse_156"),
    ("T47", "ciga", "ciga_native_declared_4"),
    ("S01", "cede", "fode_e0_common"),
    ("S02", "msshade", "msshade_native_declared_16"),
    ("Y34", "lsde", "lsde_large_declared_12"),
    ("T44", "bde", "bde2025_standard_daegwallyeong"),
    ("T43", "ppga", "ppga_nantong_structured_3d_declared_proxy_v1"),
    ("Y06", "gga", "gga2026_layout_cable"),
    ("T36", "tmoea", "nysted_paper_eq16_cpu_r4_v2"),
    ("L0726", "geoga", "geoga_anholt_structured_declared_proxy_v1"),
    ("T40", "cgpso", "cgpso_complex_large_16"),
    ("Y35", "hgpso", "agpso_aiga_hgpso_landuse_156"),
}


def read_tsv(path: Path) -> list[dict[str, str]]:
    with path.open(encoding="utf-8", newline="") as handle:
        return list(csv.DictReader(handle, delimiter="\t"))


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--formal", action="store_true")
    parser.add_argument("--final", action="store_true")
    args = parser.parse_args()

    core = read_tsv(CORE)
    full = read_tsv(FULL)
    papers = read_tsv(ROOT / "docs/paper_package_completion.tsv")
    observed = {
        (row["corpus_id"], row["algorithm_id"], row["problem_id"])
        for row in core
    }
    if len(core) != 23 or len(observed) != 23 or observed != MANDATORY:
        raise RuntimeError(
            "core target tuples differ from mandatory Q_core:\n"
            + json.dumps(
                {
                    "missing": sorted(MANDATORY - observed),
                    "unexpected": sorted(observed - MANDATORY),
                    "rows": len(core),
                },
                indent=2,
            )
        )
    if any(
        row["role"] != "target"
        or row["implementation_status"] != "executable_baseline"
        for row in core
    ):
        raise RuntimeError("every core row must be an executable target")
    if len({row["pair_id"] for row in core}) != 23:
        raise RuntimeError("duplicate pair_id in core target registry")
    if len({row["corpus_id"] for row in papers}) != 23:
        raise RuntimeError("paper completion authority does not contain 23 papers")
    paper_by_corpus = {row["corpus_id"]: row for row in papers}
    for row in core:
        paper = paper_by_corpus.get(row["corpus_id"])
        if paper is None:
            raise RuntimeError(f"{row['corpus_id']}: paper completion row missing")
        expected = (
            paper["method_semantic_id"],
            paper["paper_native_problem_id"],
            paper["problem_semantic_id"],
            paper["paper_protocol_id"],
        )
        observed_semantics = (
            row["method_semantic_id"],
            row["problem_id"],
            row["problem_semantic_id"],
            row["paper_protocol_id"],
        )
        if observed_semantics != expected:
            raise RuntimeError(
                f"{row['pair_id']}: semantic mismatch "
                f"{observed_semantics!r} != {expected!r}"
            )
        analysis = ROOT / row["analysis_path"]
        if not analysis.is_file() or digest(analysis) != row["analysis_sha256"]:
            raise RuntimeError(f"{row['pair_id']}: analysis asset/hash mismatch")

    optional_missing = sum(
        row["role"] == "comparator"
        and row["implementation_status"] == "planned_missing_native_comparator"
        for row in full
    )
    if optional_missing != 28:
        raise RuntimeError(
            f"expected 28 ignored optional comparators, observed {optional_missing}"
        )

    if args.formal or args.final:
        manifest = ROOT / "formal/contracts/plan003_core_target_campaigns_v1.json"
        if not manifest.is_file():
            raise RuntimeError("Plan-003 formal manifest is absent")
    if args.final:
        bundle = ROOT / "evidence/closure/plan005_final_bundle.json"
        if not bundle.is_file():
            raise RuntimeError("Plan-005 final evidence bundle is absent")
        document = json.loads(bundle.read_text(encoding="utf-8"))
        if (
            document.get("suite_id") != "plan005_target_native_25_v2"
            or document.get("target_count") != 23
            or document.get("ready_cpu_target_count") != 20
            or document.get("resource_deferred_learning_target_count") != 3
            or document.get("ready_cpu_formal_run_count") != 27775
            or document.get("non_target_baselines_in_readiness") != 0
        ):
            raise RuntimeError("Plan-005 final bundle scope drift")

    print(
        "hpc_core_target_scope_pass "
        f"targets={len(core)} missing=0 "
        f"optional_missing_comparators_ignored={optional_missing}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
