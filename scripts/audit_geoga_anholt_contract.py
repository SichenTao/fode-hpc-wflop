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
    "shared/contracts/geoga_reconstruction_execution_contract.json":
        "3356d9ea850b8623a8786a69428ff3df286db6ac46eec6e32555b20fa6071b70",
    "scripts/test_geoga_cpp.py":
        "f4c2717ad2f03286a7d3360450438b7187123c0dae22bc6613c97ff643c74846",
}
HISTORICAL_GEOGA_FUNCTION_SHA256 = (
    "d49a8caa42affdf45b912c27e8eb1aaf"
    "5857fc3a87a7fee3d08f121af09540ca"
)
HISTORICAL_GEOGA_PROVENANCE_ROW = (
    "geoga\tGeoGA\tpaper_derived_declared_problem_proxy\t"
    "Zhang et al. 2025 geometric mutation operators; unavailable Anholt assets "
    "are replaced only by a separately declared GGA-asset proxy\t"
    "10.1109/cbd69312.2025.00059\tgeoga_declared_reconstruction_v1"
)


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

    shared_source = (
        ROOT / "hpc/gga_cpp/src/main.cpp"
    ).read_text(encoding="utf-8")
    geoga_function = shared_source[
        shared_source.index("RunResult optimize_geoga("):
        shared_source.index("RunResult optimize_tmoea(")
    ]
    geoga_function_hash = hashlib.sha256(
        geoga_function.encode("utf-8")
    ).hexdigest()
    if geoga_function_hash != HISTORICAL_GEOGA_FUNCTION_SHA256:
        raise RuntimeError("historical GeoGA optimize function changed")

    provenance_rows = (
        ROOT / "shared/contracts/algorithm_provenance.tsv"
    ).read_text(encoding="utf-8").splitlines()
    geoga_rows = [row for row in provenance_rows if row.startswith("geoga\t")]
    if geoga_rows != [HISTORICAL_GEOGA_PROVENANCE_ROW]:
        raise RuntimeError(
            "historical GeoGA algorithm-provenance row changed or duplicated"
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
        f"old_guard_files={len(OLD_GUARD_HASHES)} "
        "shared_source_guard=exact_geoga_function "
        "shared_registry_guard=exact_geoga_row"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
