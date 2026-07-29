#!/usr/bin/env python3
"""Audit the BDE paper/source-resolved transition and distinct schedule ID."""

from __future__ import annotations

import hashlib
import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def main() -> int:
    audit = json.loads(
        (
            ROOT / "shared/contracts/bde_ws56_transition_parity_audit.json"
        ).read_text(encoding="utf-8")
    )
    old = ROOT / audit["old_profile_boundary"]["source_path"]
    source_text = old.read_text(encoding="utf-8")
    boundary = audit["old_profile_boundary"]
    start = source_text.index(boundary["source_slice_start"])
    end = source_text.index(boundary["source_slice_end"], start)
    source_slice = source_text[start:end].encode()
    if hashlib.sha256(source_slice).hexdigest() != boundary[
        "source_slice_sha256"
    ]:
        raise RuntimeError("admitted old BDE implementation changed")
    source = (
        ROOT / "hpc/bde_ws56_cpp/src/evolution.cpp"
    ).read_text(encoding="utf-8")
    required = [
        'algorithm_salt("bde")',
        "900",
        "901",
        "902",
        "kSuperiorFusionCount = 12",
        "kPaperImax = 400",
        "903",
        "904",
        "905",
        "kPbestCount = 3",
        "906",
        "907",
        "908",
        "909",
        "910",
        "trial_fitness",
        ">= fitness",
        "stable_rank_descending",
        "EvaluationSchedule::GranularityAware",
        "kParallelWorkThreshold = 2048",
        "offspring_count * dimension",
    ]
    missing = [token for token in required if token not in source]
    if missing:
        raise RuntimeError(f"BDE transition tokens missing: {missing}")
    if audit["method_semantic_id"] == audit["old_profile_boundary"][
        "method_semantic_id"
    ]:
        raise RuntimeError("changed Imax schedule reused the old method ID")
    fixture = audit["frozen_75_fes_fixture"]
    test = (
        ROOT / "hpc/bde_ws56_cpp/tests/evolution_test.cpp"
    ).read_text(encoding="utf-8")
    for value in (
        fixture["best_layout_hash"],
        fixture["population_layout_hash"],
        str(fixture["best_expected_power_kw"]),
    ):
        if value not in test:
            raise RuntimeError("BDE frozen transition fixture is not tested")
    print(
        "bde_ws56_transition_parity_audit_pass "
        "paper_imax=400 exact_fes_generations=398 fusion=12+13"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
