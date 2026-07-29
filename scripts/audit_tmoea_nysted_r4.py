#!/usr/bin/env python3
"""Audit T-MOEA v1 preservation and corrected R4 contract identity."""

from __future__ import annotations

import hashlib
import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
METHOD_V1 = "tmoea_nysted_gga_asset_reconstruction_v1"
METHOD_V2 = "tmoea_nysted_gga_asset_reconstruction_paper_eq16_v2"
PROBLEM_V2 = "tmoea_nysted_paper_wake_gga_router_problem_v1"
PROFILE_V2 = "tmoea_nysted_paper_eq16_cpu_r4_v2"
FROZEN_V1_FILES = {
    "shared/contracts/tmoea_nysted_reconstruction_execution_contract.json":
        "72b3f9b99a476147b9f399bad5a66d0dd0650df394d8feb33c8777859b2180d1",
    "evidence/development/tmoea_nysted_reconstruction_admission_spark_20260729.json":
        "be5ff3ce5f1f98b68ae19e7f6f7f8c31aa5f9ea3703fc66c963a7ca821aca33a",
}
FACT_LABELS = [
    "Paper title:",
    "DOI:",
    "Paper provides:",
    "Public author code URL:",
    "Public author code revision or archive hash:",
    "Public code/assets provide:",
    "Known missing information:",
    "Reconstruction performed here:",
    "Method evidence tier:",
    "Problem evidence tier:",
    "Method semantic ID:",
    "Problem semantic ID:",
    "Controlling contracts:",
    "Claim boundary:",
    "Last evidence audit date:",
]


def load(relative: str) -> dict:
    return json.loads((ROOT / relative).read_text(encoding="utf-8"))


def main() -> int:
    for relative, expected in FROZEN_V1_FILES.items():
        actual = hashlib.sha256((ROOT / relative).read_bytes()).hexdigest()
        if actual != expected:
            raise RuntimeError(f"historical T-MOEA v1 asset changed: {relative}")

    execution = load(
        "shared/contracts/tmoea_nysted_paper_eq16_r4_execution_contract.json"
    )
    problem = load(
        "shared/contracts/tmoea_nysted_paper_wake_gga_router_problem.json"
    )
    oracle = load("shared/contracts/tmoea_nysted_r4_oracle.json")
    receipt = load(
        "evidence/development/"
        "tmoea_nysted_paper_eq16_cpu_r4_spark_20260729.json"
    )
    decision = load("shared/contracts/reconstruction-decisions/T36.json")
    if execution["method_semantic_id"] != METHOD_V2:
        raise RuntimeError("corrected T-MOEA method identity differs")
    if execution["method_evidence_tier"] != "M3_DECLARED_COMPLETION":
        raise RuntimeError("corrected T-MOEA method is not M3")
    if execution["problem_semantic_id"] != PROBLEM_V2:
        raise RuntimeError("corrected T-MOEA problem identity differs")
    if execution["problem_evidence_tier"] != "P2_CITATION_SAME_AUTHOR":
        raise RuntimeError("corrected T-MOEA problem is not P2")
    if execution["study_id"] != PROFILE_V2:
        raise RuntimeError("corrected T-MOEA execution profile differs")
    if execution["historical_v1_guard"]["method_semantic_id"] != METHOD_V1:
        raise RuntimeError("historical T-MOEA v1 guard differs")
    if problem["problem_semantic_id"] != PROBLEM_V2:
        raise RuntimeError("T-MOEA biobjective problem contract differs")
    canonical_hash = hashlib.sha256(
        problem["canonical_semantics_string"].encode("utf-8")
    ).hexdigest()
    if canonical_hash != problem["problem_semantics_sha256"]:
        raise RuntimeError("T-MOEA problem semantic hash differs")
    if oracle["method_semantic_id"] != METHOD_V2:
        raise RuntimeError("T-MOEA oracle method identity differs")
    if oracle["problem_semantic_id"] != PROBLEM_V2:
        raise RuntimeError("T-MOEA oracle problem identity differs")
    if receipt["method_semantic_id"] != METHOD_V2:
        raise RuntimeError("T-MOEA R4 receipt method identity differs")
    if receipt["problem_semantic_id"] != PROBLEM_V2:
        raise RuntimeError("T-MOEA R4 receipt problem identity differs")
    if receipt["science"]["physical_fes"] != 3000:
        raise RuntimeError("T-MOEA R4 receipt does not cover full work")
    if receipt["science"]["workers_1_vs_0_scientific_differences"]:
        raise RuntimeError("T-MOEA R4 receipt records worker differences")
    required_profiles = {METHOD_V1, METHOD_V2, PROBLEM_V2, PROFILE_V2}
    if not required_profiles.issubset(set(decision["profiles"])):
        raise RuntimeError("T36 decision ledger lacks a controlled profile")

    source = (ROOT / "hpc/gga_cpp/src/main.cpp").read_text(encoding="utf-8")
    test_source = (
        ROOT / "hpc/gga_cpp/tests/tmoea_topology_test.cpp"
    ).read_text(encoding="utf-8")
    for path, text in (
        ("hpc/gga_cpp/src/main.cpp", source),
        ("hpc/gga_cpp/tests/tmoea_topology_test.cpp", test_source),
    ):
        if "WFLOP IMPLEMENTATION FACT DECLARATION" not in text:
            raise RuntimeError(f"missing implementation facts: {path}")
        if "END WFLOP IMPLEMENTATION FACT DECLARATION" not in text:
            raise RuntimeError(f"unterminated implementation facts: {path}")
        for label in FACT_LABELS:
            if label not in text:
                raise RuntimeError(f"missing {label} in {path}")

    required_source_tokens = [
        "paper_equation_16",
        "tmoea_replacement_candidate",
        "historical-v1",
        "paper-eq16-v2",
        METHOD_V2,
        PROBLEM_V2,
        "complete_layout_evaluations",
        "parent_equality_resolutions",
        "topology_relocations",
        "nondominated_front_hash",
    ]
    for token in required_source_tokens:
        if token not in source:
            raise RuntimeError(f"T-MOEA implementation lacks token: {token}")
    source_hash = hashlib.sha256(source.encode("utf-8")).hexdigest()
    if source_hash != receipt["artifacts"]["implementation_source_sha256"]:
        raise RuntimeError("T-MOEA R4 receipt source hash differs")
    oracle_hash = hashlib.sha256(
        (ROOT / "scripts/validate_tmoea_nysted_r4.py").read_bytes()
    ).hexdigest()
    if oracle_hash != receipt["artifacts"]["independent_oracle_sha256"]:
        raise RuntimeError("T-MOEA R4 receipt oracle hash differs")

    provenance = (
        ROOT / "shared/contracts/algorithm_provenance.tsv"
    ).read_text(encoding="utf-8")
    if provenance.count(f"\t{METHOD_V1}\n") != 1:
        raise RuntimeError("historical T-MOEA provenance row differs")
    if provenance.count(f"\t{METHOD_V2}\n") != 1:
        raise RuntimeError("corrected T-MOEA provenance row differs")

    profiles = load("shared/contracts/executable_profile_evidence.json")
    matching = [
        row
        for row in profiles["profiles"]
        if row["method_semantics_id"] == METHOD_V2
        and row["problem_semantics_id"] == PROBLEM_V2
    ]
    if len(matching) != 1:
        raise RuntimeError("corrected T-MOEA executable profile differs")
    if matching[0]["method_evidence_tier"] != "M3_DECLARED_COMPLETION":
        raise RuntimeError("corrected T-MOEA profile propagated a non-M3 tier")

    print(
        "tmoea_nysted_r4_contract_audit_pass "
        f"historical={METHOD_V1} corrected={METHOD_V2} "
        f"problem={PROBLEM_V2}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
