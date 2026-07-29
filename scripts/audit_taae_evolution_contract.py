#!/usr/bin/env python3
"""
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: TAAE end-to-end declared-reconstruction contract audit
Paper title: Transformer Autoencoder-Assisted Evolutionary Framework for Constrained Multiobjective 3D Wind Farm Layout Optimization
DOI: 10.1109/JAS.2026.126233
Public author method source/checkpoint: unavailable as recorded in docs/source-dossiers/Y36.json
Missing choices completed here: method identity, SPEA2 density, ranking ties, latent bounds, pre-repair decoded filtering, post-repair guards, no-feasible front output, partial FES, checkpoint gate, and CPU-only execution
Reconstruction status: bounded executable M3 engineering reconstruction on the declared P3 problem proxy
Method evidence tier: M3_DECLARED_COMPLETION
Method semantic ID: taae_transformer_evolution_declared_reconstruction_v1
Kernel semantic ID: taae_transformer_declared_reconstruction_v1
Problem semantic ID: taae_zhangbei_structured_declared_proxy_v1
Controlling contract: shared/contracts/taae_transformer_evolution_declared_reconstruction_contract.json
Claim boundary: bounded contract audit only; original taae remains blocked, paper-scale state requires an immutable checkpoint, and no Zhangbei, reported-front, formal, performance, or GPU claim is made
END WFLOP IMPLEMENTATION FACT DECLARATION
"""

from __future__ import annotations

import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
CONTRACT_PATH = (
    ROOT
    / "shared/contracts/taae_transformer_evolution_declared_reconstruction_contract.json"
)
DECISIONS_PATH = ROOT / "shared/contracts/reconstruction-decisions/Y36.json"
CPU_R4_RECEIPT_PATH = (
    ROOT / "evidence/development/taae_cpu_r4_spark_20260729.json"
)
METHOD_ID = "taae_transformer_evolution_declared_reconstruction_v1"
KERNEL_ID = "taae_transformer_declared_reconstruction_v1"
PROBLEM_ID = "taae_zhangbei_structured_declared_proxy_v1"
FILES = (
    ROOT / "hpc/taae_cpp/include/taae/evolution.hpp",
    ROOT / "hpc/taae_cpp/src/evolution.cpp",
    ROOT / "hpc/taae_cpp/src/main.cpp",
    ROOT / "hpc/taae_cpp/tests/evolution_test.cpp",
    Path(__file__).resolve(),
)


def main() -> int:
    contract = json.loads(CONTRACT_PATH.read_text(encoding="utf-8"))
    decisions = json.loads(DECISIONS_PATH.read_text(encoding="utf-8"))
    cpu_r4 = json.loads(CPU_R4_RECEIPT_PATH.read_text(encoding="utf-8"))
    assert contract["method_semantic_id"] == METHOD_ID
    assert contract["kernel_semantic_id"] == KERNEL_ID
    assert contract["blocked_original_algorithm_id"] == "taae"
    assert contract["problem_semantic_id"] == PROBLEM_ID
    paper = contract["paper_visible_semantics"]
    assert paper["population_size"] == 100
    assert paper["initial_physical_fes"] == 100
    assert paper["maximum_physical_fes"] == 10000
    assert paper["fine_tune_epochs_per_generation"] == 10
    assert paper["latent_differential_weight"] == 0.3
    assert paper["polynomial_mutation_distribution_index"] == 20.0
    completions = contract["declared_identity_critical_completions"]
    for field in (
        "relative_fitness",
        "spea2_density_k",
        "tournament",
        "latent_bounds",
        "polynomial_mutation_bounds",
        "decoded_filter_order",
        "proposal_traversal",
        "gaussian_covariance",
        "nearest_cell_tie",
        "repair_fallback",
        "duplicate_refill",
        "partial_generation",
        "no_feasible_front",
        "pretraining_checkpoint",
    ):
        assert completions[field]
    assert contract["physical_work"]["training_physical_fes"] == 0
    assert contract["parallel_contract"]["gpu"] == "unsupported_fail_closed"
    assert contract["parallel_contract"]["hybrid"] == "unsupported_fail_closed"
    for field in (
        "cpu",
        "cli_default_workers",
        "ordered_state",
        "speculative_decode",
        "worker_equivalence",
        "participation_receipt",
        "proposal_work_receipt",
        "rejected_optimizer_parallelism",
    ):
        assert contract["parallel_contract"][field]
    assert contract["training_profiles"]["bounded_smoke"] == {
        "profile_id": "taae_evolution_bounded_smoke_v1",
        "pretraining_layouts": 64,
        "pretraining_epochs": 2,
        "pretraining_batch_size": 16,
        "fine_tune_epochs_per_generation": 10,
        "fine_tune_batch_size": 64,
        "paper_scale_claim": False,
    }
    assert contract["training_profiles"]["paper_scale"][
        "fail_closed_without_checkpoint_and_sha256"
    ] is True
    assert (
        "training-state profile ID and actual model architecture"
        in contract["required_outputs"]
    )
    assert (
        "front feasibility label and minimum normalized constraint violation"
        in contract["required_outputs"]
    )
    assert (
        "per-stage timing and actual executor-participation receipts"
        in contract["required_outputs"]
    )
    assert (
        "proposal and speculative-decode work receipt"
        in contract["required_outputs"]
    )
    assert contract["repair_contract"]["ordering"].startswith(
        "integer decode, raw decoded multiset"
    )
    assert contract["declared_identity_critical_completions"][
        "cdp_rule"
    ].startswith("one feasible solution beats one infeasible solution")
    assert METHOD_ID in decisions["profiles"]
    assert "paper_scale_checkpoint_blocked" in decisions["completion_status"]

    source = (ROOT / "hpc/taae_cpp/src/evolution.cpp").read_text(
        encoding="utf-8"
    )
    for token in (
        "kPaperPopulationSize = 100",
        "kPaperMaximumFes = 10000",
        "kDifferentialWeight = 0.3",
        "kPolynomialDistributionIndex = 20.0",
        "kGaussianAttemptCap = 64",
        "kProposalMultiplierCap = 10",
        "kRefillMultiplierCap = 10",
        "canonical_solution_key",
        "duplicate_raw_before_repair",
        "parent_identical_before_repair",
        "duplicate_after_repair",
        "least_violation_infeasible",
        "proposal_index % population.size();",
        "value.source_index = population.size() + offspring.size();",
        "TrainingStateProfile::paper_scale_checkpoint",
        "training_state_profile_id",
        "model_architecture",
        "training_physical_fes = 0",
        "const std::size_t speculative_width",
        "post_latent_rng_state",
        "speculative_decode_discards",
        "repair_rng_invalidations",
        "requested_workers",
        "resolved_workers",
        "participant_activations",
        "peak_region_participants",
    ):
        assert token in source, f"source missing {token}"
    main_source = (ROOT / "hpc/taae_cpp/src/main.cpp").read_text(
        encoding="utf-8"
    )
    assert "int workers = 0;" in main_source
    assert "default: all visible CPUs" in main_source
    test_source = (
        ROOT / "hpc/taae_cpp/tests/evolution_test.cpp"
    ).read_text(encoding="utf-8")
    for token in (
        "executor_receipt_fixture",
        "check_parallel_stage",
        "proposal algorithm-work mismatch",
    ):
        assert token in test_source, f"test missing {token}"
    assert "speculative_midrepair=trajectory_rng_layout_exact" in source
    assert any(
        decision["field"] == "taae_cpu_r4_exact_execution"
        for decision in decisions["decisions"]
    )
    assert cpu_r4["approved_source_base_commit"] == (
        "9036e00e46a9ddcd86c2a899bcb2a95214d45d40"
    )
    assert cpu_r4["scope"]["backend"] == "pure_cpp_cpu"
    assert cpu_r4["controlled_approved_checkpoint_200_fes"][
        "exact_scientific_outputs"
    ]["match_approved_pre_r4_and_workers_1_20"] is True
    assert cpu_r4["canonical_seed1_default_workers_300_fes"][
        "requested_workers"
    ] == 0
    assert cpu_r4["canonical_seed1_default_workers_300_fes"][
        "resolved_workers"
    ] == 20
    assert cpu_r4["validation"]["full_ctest"] == "39_of_39_pass"
    assert "predates the approved 9036" in cpu_r4[
        "invalid_historical_denominator"
    ]["reason"]
    filter_start = source.index("DecodedProposalResult filter_and_repair_decoded(")
    filter_end = source.index("\ndouble evaluate_population(", filter_start)
    filter_source = source[filter_start:filter_end]
    assert filter_source.index("canonical_solution_key(decoded)") < (
        filter_source.index("repair_layout(decoded")
    )
    assert filter_source.index("parent_keys.contains(raw_key)") < (
        filter_source.index("repair_layout(decoded")
    )
    cdp_start = source.index("bool cdp_dominates(")
    cdp_end = source.index(
        "\nstd::vector<std::vector<std::size_t>> assign_nondomination(",
        cdp_start,
    )
    cdp_source = source[cdp_start:cdp_end]
    assert "if (!left_feasible)" in cdp_source
    assert "return lhs < rhs;" in cdp_source
    assert cdp_source.index("return lhs < rhs;") < (
        cdp_source.index("return pareto_dominates(left, right);")
    )
    cmake = (ROOT / "hpc/taae_cpp/CMakeLists.txt").read_text(
        encoding="utf-8"
    )
    assert "add_executable(taae_evolution_hpc" in cmake
    assert "taae_evolution_test" in cmake

    for path in FILES:
        text = path.read_text(encoding="utf-8")
        declaration_end = text.index(
            "END WFLOP IMPLEMENTATION FACT DECLARATION"
        )
        declaration = text[:declaration_end]
        for required in (
            "DOI: 10.1109/JAS.2026.126233",
            "Public author method source/checkpoint: unavailable",
            "Missing choices completed here:",
            "Reconstruction status:",
            "Method evidence tier: M3_DECLARED_COMPLETION",
            f"Method semantic ID: {METHOD_ID}",
            f"Kernel semantic ID: {KERNEL_ID}",
            f"Problem semantic ID: {PROBLEM_ID}",
            "Controlling contract:",
            "Claim boundary:",
            "original taae remains blocked",
        ):
            assert required in declaration, f"{path}: missing {required}"
    print(
        "taae_evolution_contract_audit_pass "
        "population=100 max_fes=10000 fine_tune_epochs=10 "
        "method=distinct_M3 original_taae=blocked paper_scale=blocked"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
