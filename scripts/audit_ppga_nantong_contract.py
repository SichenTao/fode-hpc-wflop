#!/usr/bin/env python3
"""
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: PPGA Nantong-structured P3/M3 contract and fact-declaration audit
Paper title: Advanced 3D Wind Farm Layout Optimization Framework via Power-Law Perturbation-Based Genetic Algorithm
DOI: 10.1109/JAS.2025.125351
Problem evidence tier: P3_DECLARED_PROXY
Method evidence tier: M3_DECLARED_COMPLETION
Problem semantic ID: ppga_nantong_structured_3d_declared_proxy_v1
Method semantic ID: ppga_nantong_structured_3d_declared_reconstruction_v1
Controlling contracts: shared/contracts/ppga_nantong_structured_3d_declared_proxy_contract.json and shared/contracts/ppga_nantong_structured_3d_declared_reconstruction_contract.json
Claim boundary: contract consistency only; original Nantong data, author-exact PPGA transitions, paper results, and GPU or hybrid execution remain blocked
END WFLOP IMPLEMENTATION FACT DECLARATION
"""

from __future__ import annotations

import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
PROBLEM_ID = "ppga_nantong_structured_3d_declared_proxy_v1"
METHOD_ID = "ppga_nantong_structured_3d_declared_reconstruction_v1"
OLD_TRANSFER_ID = "ppga_declared_reconstruction_fode_e0_v1"
FILES = (
    ROOT / "hpc/ppga_cpp/include/ppga/problem.hpp",
    ROOT / "hpc/ppga_cpp/include/ppga/evolution.hpp",
    ROOT / "hpc/ppga_cpp/src/problem.cpp",
    ROOT / "hpc/ppga_cpp/src/evolution.cpp",
    ROOT / "hpc/ppga_cpp/src/main.cpp",
    ROOT / "hpc/ppga_cpp/tests/problem_test.cpp",
    ROOT / "hpc/ppga_cpp/tests/evolution_test.cpp",
    Path(__file__).resolve(),
)


def load(relative: str) -> dict:
    return json.loads((ROOT / relative).read_text(encoding="utf-8"))


def main() -> int:
    problem = load(
        "shared/contracts/ppga_nantong_structured_3d_declared_proxy_contract.json"
    )
    method = load(
        "shared/contracts/ppga_nantong_structured_3d_declared_reconstruction_contract.json"
    )
    cases = load(
        "shared/contracts/ppga_nantong_structured_3d_declared_proxy_cases.json"
    )
    oracle = load(
        "shared/contracts/ppga_nantong_structured_3d_declared_proxy_oracle.json"
    )
    provenance = load(
        "shared/contracts/ppga_nantong_structured_3d_provenance.json"
    )
    decisions = load("shared/contracts/reconstruction-decisions/T43.json")
    receipt = load(
        "evidence/development/ppga_nantong_cpu_r4_spark_20260729.json"
    )

    assert problem["problem_semantic_id"] == PROBLEM_ID
    assert problem["evidence_tier"] == "P3_DECLARED_PROXY"
    assert "distinct Nantong-structured engineering proxy" in (
        problem["claim_boundary"]
    )
    assert method["method_semantic_id"] == METHOD_ID
    assert method["problem_semantic_id"] == PROBLEM_ID
    assert method["preserved_e0_semantic_id"] == OLD_TRANSFER_ID
    assert method["evidence_tier"] == "M3_DECLARED_COMPLETION"
    assert method["paper_preserved"] == {
        "population_size": 30,
        "threshold": 0.0,
        "crossover_probability": 0.8,
        "mutation_probability": 0.1,
        "power_law_exponent": 2.5,
        "power_law_start_generation": 2,
        "independent_runs_reported_by_paper": 30,
    }
    assert method["physical_fes"] == (
        "one complete layout evaluation over the selected case's 16 x 7 "
        "joint wind states counts as one physical FES; WS1, WS2, WS3, "
        "and WS4 are separate benchmark cases"
    )
    assert method["execution"]["default_backend"] == "cpu"
    assert method["execution"]["default_workers"] == "hardware_concurrency"
    assert method["execution"]["gpu_and_hybrid"] == "fail_closed"

    assert cases["problem_semantic_id"] == PROBLEM_ID
    assert cases["grid"] == {
        "rows": 16,
        "cols": 27,
        "cell_width_m": 300.0,
    }
    assert len(cases["wind_directions_deg"]) == 16
    assert len(cases["wind_speeds_mps"]) == 7
    assert len(cases["wind_scenarios"]) == 4
    assert len(cases["cases"]) == 16
    assert {
        case["turbine_count"] for case in cases["cases"]
    } == {20, 30, 40, 50}
    for scenario in cases["wind_scenarios"]:
        assert len(scenario["direction_probabilities"]) == 16
        assert len(scenario["speed_probabilities"]) == 7
        assert abs(sum(scenario["direction_probabilities"]) - 1.0) < 1e-12
        assert abs(sum(scenario["speed_probabilities"]) - 1.0) < 1e-12

    assert oracle["problem_semantic_id"] == PROBLEM_ID
    assert oracle["problem_semantic_hash"] == "ee06013d8778fd7e"
    assert len(oracle["complete_layout_oracle"]["layout_1based"]) == 20
    assert oracle["complete_layout_oracle"]["conversion_efficiency"] > 0.0
    assert provenance["target"]["sha256"] == (
        "92d3eb80a5f6ba3dcf904712f19bd1f81e4432a3d13235b27b74ceda3fece016"
    )
    assert provenance["authority_ladder"][0]["sha256"] == (
        "385658381f3af634dc47ceafef8d03ece5cd6c0084eb0289da4810941b72fbd8"
    )
    assert provenance["original_nantong_reconstruction_status"] == "blocked"
    assert PROBLEM_ID in decisions["profiles"]
    assert METHOD_ID in decisions["profiles"]
    assert OLD_TRANSFER_ID in decisions["profiles"]
    assert len(decisions["decisions"]) == 6
    assert receipt["scope"]["method_semantic_id"] == METHOD_ID
    assert receipt["scope"]["problem_semantic_id"] == PROBLEM_ID
    assert receipt["scope"]["physical_fes_denominator"] == (
        "one complete layout evaluation over this selected case's 16 x 7 "
        "joint wind states; WS1, WS2, WS3, and WS4 are separate benchmark cases"
    )
    assert receipt[
        "scientific_equivalence_workers_1_vs_default_all_visible"
    ]["exact_match"] is True
    assert receipt[
        "scientific_equivalence_workers_1_vs_default_all_visible"
    ]["physical_fes"] == 1500
    assert receipt["canonical_default_all_visible_run"][
        "resolved_workers"
    ] == 20
    assert receipt["canonical_default_all_visible_run"]["stages"][
        "evaluator"
    ]["distinct_participants"] == 20
    assert receipt["validation"][
        "full_configured_ctest_after_complete_build"
    ] == "42_of_42_pass"
    assert len(receipt["bounded_timing"]["runs"]) == 10
    assert len({
        run["result_json_sha256"]
        for run in receipt["bounded_timing"]["runs"]
    }) == 10
    assert receipt["bounded_timing"][
        "scientific_result_sha256_all_ten_runs"
    ] == "b02fe29a31f17e287e389208ac926d93bf593cb8b0c09e56308cdc12c02265a3"
    assert receipt["validation"][
        "old_ppga_e0_transfer_files_unchanged_from_approved_base"
    ] is True

    old_contract = load("shared/contracts/ppga_fode_e0_transfer_execution_contract.json")
    assert old_contract["study_id"] == OLD_TRANSFER_ID
    old_source = (ROOT / "hpc/wflop_cpp/src/algorithms.cpp").read_text(
        encoding="utf-8"
    )
    assert "PPGA_DECLARED_RECONSTRUCTION_FODE_E0_V1" in old_source
    assert METHOD_ID not in old_source

    problem_source = (ROOT / "hpc/ppga_cpp/src/problem.cpp").read_text(
        encoding="utf-8"
    )
    for token in (
        "kAxialInduction = 1.0 / 3.0",
        "kWakeExpansion = 0.05",
        "kGaussianXi = 1.98",
        "kShearExponent = 0.1",
        "kTerrainRule",
        "kPowerCurveRule",
        "kMultipleWakeRule",
        "kObjectiveRule",
        "kCostDiagnosticRule",
        "squared_velocity_deficit",
        "cost_per_expected_power",
    ):
        assert token in problem_source, f"problem source missing {token}"
    method_source = (ROOT / "hpc/ppga_cpp/src/evolution.cpp").read_text(
        encoding="utf-8"
    )
    for token in (
        "kPopulationSize = 30",
        "kEliteCount = 3",
        "kCrossoverProbability = 0.8",
        "kMutationProbability = 0.1",
        "kPowerLawExponent = 2.5",
        "CounterRng",
        "PersistentExecutor",
        "participant_activations",
        "pairwise_layout_distances",
        "config.physical_fes - fes",
    ):
        assert token in method_source, f"method source missing {token}"

    for path in FILES:
        text = path.read_text(encoding="utf-8")
        if path != Path(__file__).resolve():
            assert text.count("WFLOP IMPLEMENTATION FACT DECLARATION") == 2
        declaration_end = text.index(
            "END WFLOP IMPLEMENTATION FACT DECLARATION"
        )
        declaration = text[:declaration_end]
        for token in ("Implementation unit:", "Claim boundary:"):
            assert token in declaration, f"{path}: missing {token}"
        assert (
            PROBLEM_ID in declaration or METHOD_ID in declaration
        ), f"{path}: missing PPGA semantic identity"

    print(
        "ppga_nantong_contract_audit_pass "
        "cases=16 P3=declared_proxy M3=declared_completion "
        "old_E0=preserved original_Nantong=blocked"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
