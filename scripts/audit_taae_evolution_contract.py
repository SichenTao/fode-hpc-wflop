#!/usr/bin/env python3
"""
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: TAAE end-to-end declared-reconstruction contract audit
Paper title: Transformer Autoencoder-Assisted Evolutionary Framework for Constrained Multiobjective 3D Wind Farm Layout Optimization
DOI: 10.1109/JAS.2026.126233
Public author method source/checkpoint: unavailable as recorded in docs/source-dossiers/Y36.json
Missing choices completed here: method identity, SPEA2 density, ranking ties, latent bounds, repair/refill caps, partial FES, checkpoint gate, and CPU-only execution
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
        "gaussian_covariance",
        "nearest_cell_tie",
        "repair_fallback",
        "duplicate_refill",
        "partial_generation",
        "pretraining_checkpoint",
    ):
        assert completions[field]
    assert contract["physical_work"]["training_physical_fes"] == 0
    assert contract["parallel_contract"]["gpu"] == "unsupported_fail_closed"
    assert contract["parallel_contract"]["hybrid"] == "unsupported_fail_closed"
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
        "TrainingStateProfile::paper_scale_checkpoint",
        "training_state_profile_id",
        "model_architecture",
        "training_physical_fes = 0",
    ):
        assert token in source, f"source missing {token}"
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
