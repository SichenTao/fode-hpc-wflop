#!/usr/bin/env python3
"""Audit the isolated GeoGA Anholt-structured P3 contract and old-ID guard."""

from __future__ import annotations

import hashlib
import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
PROBLEM_ID = "geoga_anholt_structured_declared_proxy_v1"
METHOD_ID = "geoga_declared_reconstruction_v1"
PROFILE_ID = "geoga_anholt_structured_p3_execution_v1"
OLD_GUARD_HASHES = {
    "hpc/gga_cpp/src/main.cpp":
        "1d639544b21e3aec593980f94e1d2dca24b21a2538e20fb8e47aebf9f8739405",
    "shared/contracts/geoga_reconstruction_execution_contract.json":
        "3356d9ea850b8623a8786a69428ff3df286db6ac46eec6e32555b20fa6071b70",
    "scripts/test_geoga_cpp.py":
        "f4c2717ad2f03286a7d3360450438b7187123c0dae22bc6613c97ff643c74846",
    "shared/contracts/algorithm_provenance.tsv":
        "25033d22281248010c6b03a02ed3dc7b4095a27048eddec5db31d7d59ce5c545",
}


def load(relative: str) -> dict:
    return json.loads((ROOT / relative).read_text(encoding="utf-8"))


def main() -> int:
    case = load(
        "shared/contracts/geoga_anholt_structured_declared_proxy_case.json"
    )
    problem = load(
        "shared/contracts/geoga_anholt_structured_declared_proxy_contract.json"
    )
    execution = load(
        "shared/contracts/geoga_anholt_structured_execution_contract.json"
    )
    oracle = load(
        "shared/contracts/geoga_anholt_structured_declared_proxy_oracle.json"
    )
    provenance = load(
        "shared/contracts/geoga_anholt_structured_provenance.json"
    )
    decision = load("shared/contracts/reconstruction-decisions/L0726.json")

    if case["problem_semantic_id"] != PROBLEM_ID:
        raise RuntimeError("case problem semantic ID differs")
    if problem["problem_semantic_id"] != PROBLEM_ID:
        raise RuntimeError("problem contract semantic ID differs")
    if execution["problem_semantic_id"] != PROBLEM_ID:
        raise RuntimeError("execution problem semantic ID differs")
    if execution["method_semantic_id"] != METHOD_ID:
        raise RuntimeError("admitted GeoGA method semantic ID is not reused")
    if execution["study_id"] != PROFILE_ID:
        raise RuntimeError("execution profile ID differs")
    if oracle["problem_semantic_id"] != PROBLEM_ID:
        raise RuntimeError("oracle problem semantic ID differs")
    if provenance["profile_id"] != PROFILE_ID:
        raise RuntimeError("provenance profile ID differs")
    if PROBLEM_ID not in decision["profiles"] or PROFILE_ID not in decision["profiles"]:
        raise RuntimeError("reconstruction decision ledger lacks new profiles")

    if case["turbine_count"] != 111 or case["target_candidate_count"] != 180:
        raise RuntimeError("paper-visible GeoGA counts differ")
    turbine = case["turbine"]
    poisson = case["poisson_sampling"]
    if poisson["minimum_spacing_m"] != 5 * turbine["rotor_diameter_m"]:
        raise RuntimeError("GeoGA spacing is not 5D")
    if len(case["boundary"]["boundary_vertices_m"]) < 3:
        raise RuntimeError("GeoGA polygon is incomplete")
    if len(case["wind_bins"]) != 12:
        raise RuntimeError("GeoGA wind-bin count differs")
    if abs(sum(row["probability"] for row in case["wind_bins"]) - 1.0) > 1e-12:
        raise RuntimeError("GeoGA wind probabilities do not sum to one")
    if case["actual_layout_comparison"]["status"] != "blocked":
        raise RuntimeError("actual-layout comparison boundary was weakened")
    if oracle["status"] != "frozen":
        raise RuntimeError("GeoGA oracle is not frozen")
    if oracle["problem_semantic_hash"] != "42a7899a17237389":
        raise RuntimeError("GeoGA full problem hash differs")
    if execution["physical_fes"]["denominator_scope"] != "selected case only":
        raise RuntimeError("GeoGA physical-FES denominator is ambiguous")

    for relative, expected in OLD_GUARD_HASHES.items():
        actual = hashlib.sha256((ROOT / relative).read_bytes()).hexdigest()
        if actual != expected:
            raise RuntimeError(
                f"historical GeoGA semantic asset changed: {relative}"
            )

    implementation_files = sorted(
        (ROOT / "hpc/geoga_cpp").glob("**/*.cpp")
    ) + sorted((ROOT / "hpc/geoga_cpp").glob("**/*.hpp"))
    if not implementation_files:
        raise RuntimeError("GeoGA standalone implementation is absent")
    for path in implementation_files:
        text = path.read_text(encoding="utf-8")
        if "WFLOP IMPLEMENTATION FACT DECLARATION" not in text:
            raise RuntimeError(f"missing fact declaration: {path}")
        if "END WFLOP IMPLEMENTATION FACT DECLARATION" not in text:
            raise RuntimeError(f"unterminated fact declaration: {path}")

    print(
        "geoga_anholt_contract_audit_pass "
        f"problem={PROBLEM_ID} method={METHOD_ID} "
        f"old_guard_files={len(OLD_GUARD_HASHES)}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
