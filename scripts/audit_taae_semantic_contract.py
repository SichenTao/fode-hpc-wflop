#!/usr/bin/env python3
"""
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: TAAE formula, full semantic-hash, and discrete-evidence audit
DOI: 10.1109/JAS.2026.126233
Fixture scope: independently recomputes Eq. 9 and cross-checks all six hashes, hash coverage, and binary-grid evidence
Problem evidence tier: P4_FORMULA_FIXTURE over P3_DECLARED_PROXY
Problem semantic ID: taae_zhangbei_structured_declared_proxy_v1
Controlling contracts: shared/contracts/taae_formula_fixture_contract.json and shared/contracts/taae_zhangbei_structured_declared_proxy_contract.json
Claim boundary: contract-consistency audit only; no original Zhangbei, TAAE method, performance, or reported-result claim
END WFLOP IMPLEMENTATION FACT DECLARATION
"""

from __future__ import annotations

import csv
import json
import math
import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "hpc/wflop_cpp/src/problems/taae_zhangbei_structured_proxy.cpp"
TEST = ROOT / "hpc/wflop_cpp/tests/taae_problem_test.cpp"
CASES = ROOT / "shared/contracts/taae_zhangbei_structured_declared_proxy_cases.json"
CONTRACT = ROOT / "shared/contracts/taae_zhangbei_structured_declared_proxy_contract.json"
FORMULA = ROOT / "shared/contracts/taae_formula_fixture_contract.json"
PROBLEMS = ROOT / "docs/problem_package_registry.tsv"
HASH_VERSION = "taae_proxy_full_problem_semantics_hash_v2"
SEMANTIC_CONSTANTS = (
    "kTerrainBaseM",
    "kTerrainWaveAmplitudeM",
    "kTerrainTrendAmplitudeM",
    "kTerrainGridSpan",
    "kTerrainTrendDivisor",
    "kCellCenterOffset",
    "kHubHeightM",
    "kRotorRadiusM",
    "kThrustCoefficient",
    "kTurbulenceIntensity",
    "kWindShearExponent",
    "kMinimumShearBase",
    "kTerrainReferenceM",
    "kWakeVerticalCoefficient",
    "kWakeVerticalCtExponent",
    "kWakeVerticalTiExponent",
    "kWakeHorizontalCoefficient",
    "kWakeHorizontalCtExponent",
    "kWakeHorizontalTiExponent",
    "kGaussianRadiusDivisor",
    "kPowerCutInMps",
    "kPowerRatedStartMps",
    "kPowerCutOutMps",
    "kPowerCubicCoefficient",
    "kPowerRatedKw",
    "kAcousticBandsHz",
    "kAWeightingDb",
    "kAirAbsorptionDbPerM",
    "kInflowReferenceDb",
    "kTrailingReferenceDb",
    "kReferenceWindSpeed",
    "kInflowVelocityDbExponent",
    "kTrailingVelocityDbExponent",
    "kMonitorHeightM",
    "kMinimumAcousticDistanceM",
    "kTurbineCostScale",
    "kLandCostPerSquareMetre",
    "kTurbineCostExponent",
    "kTurbineCostFixedFraction",
    "kTurbineCostVariableFraction",
    "kEffectiveAreaBlend",
)


def main() -> int:
    source = SOURCE.read_text(encoding="utf-8")
    test = TEST.read_text(encoding="utf-8")
    cases = json.loads(CASES.read_text(encoding="utf-8"))
    contract = json.loads(CONTRACT.read_text(encoding="utf-8"))
    formula = json.loads(FORMULA.read_text(encoding="utf-8"))

    hash_body = source[source.index("structured_proxy_semantic_hash") :]
    for constant in SEMANTIC_CONSTANTS:
        if constant not in hash_body:
            raise RuntimeError(
                f"full problem-semantic hash omits {constant}"
            )
    for rule in (
        "multiwake=ambient_target_shear_once_minus_",
        "noise=probability_weighted_monitor_mean_",
        "constraint=max(0,cost-budget)/budget",
        "identity_for(data.case_id).budget",
    ):
        if rule not in hash_body:
            raise RuntimeError(f"full problem-semantic hash omits rule {rule}")

    source_hashes = re.findall(r'"(fnv1a64:[0-9a-f]{16})"', source)
    test_hashes = re.findall(r'"(fnv1a64:[0-9a-f]{16})"', test)
    case_hashes = list(
        cases["full_problem_semantic_hash"]["case_hashes"].values()
    )
    contract_hashes = list(contract["case_semantic_hashes"].values())
    if not (
        len(source_hashes) == 6
        and source_hashes == test_hashes == case_hashes == contract_hashes
    ):
        raise RuntimeError("six full problem-semantic hash ledgers disagree")
    if (
        cases["full_problem_semantic_hash"]["version_tag"] != HASH_VERSION
        or contract["semantic_hash_contract"]["version_tag"] != HASH_VERSION
        or HASH_VERSION not in source
    ):
        raise RuntimeError("semantic hash version tags disagree")
    if (
        "14695981039346656037ULL" not in hash_body
        or "1099511628211ULL" not in hash_body
    ):
        raise RuntimeError("hash implementation is not standard FNV-1a 64")

    expected_eq9 = 10.0 * math.log10(
        (
            3.0 * 1.0 * 1.225**2 * 0.1 * 38.5
            * 1.0 * 40.0**4
        )
        / (100.0**2 * 343.0**2)
    ) - 8.6
    observed_eq9 = formula["covered_oracles"][
        "equation_9_declared_scalar_spl_db"
    ]
    if not math.isclose(
        observed_eq9,
        expected_eq9,
        rel_tol=1.0e-14,
        abs_tol=1.0e-14,
    ):
        raise RuntimeError("Eq. 9 scalar oracle transcription mismatch")

    with PROBLEMS.open(encoding="utf-8", newline="") as stream:
        rows = list(csv.DictReader(stream, delimiter="\t"))
    y36 = next(row for row in rows if row["corpus_id"] == "Y36")
    if y36["decision_space"] != "discrete_grid_indices_with_terrain_elevation":
        raise RuntimeError("Y36 decision space is not the paper binary grid")

    print(
        "taae_semantic_contract_audit_pass "
        "hashes=6 eq9=pass decision_space=discrete"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
