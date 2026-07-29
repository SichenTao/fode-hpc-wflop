#!/usr/bin/env python3
"""
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: TAAE declared-reconstruction Transformer kernel contract audit
Paper title: Transformer Autoencoder-Assisted Evolutionary Framework for Constrained Multiobjective 3D Wind Farm Layout Optimization
DOI: 10.1109/JAS.2026.126233
Public author method source/checkpoint: unavailable as recorded in docs/source-dossiers/Y36.json
Missing choices completed here: FFN width 256, post-norm, zero dropout, Xavier-uniform initialization, mean encoder pooling, separate encoder/decoder embeddings, deterministic metric-pair seed, per-parameter Adam age, and checkpoint format
Reconstruction status: engineering reconstruction with declared completion choices
Method evidence tier: M3_DECLARED_COMPLETION
Method semantic ID: taae_transformer_declared_reconstruction_v1
Controlling contract: shared/contracts/taae_transformer_declared_reconstruction_contract.json
Claim boundary: contract and source-declaration audit only; end-to-end M3 method is not admitted, original taae remains blocked, and no author-result, optimizer, performance, or GPU claim is made
END WFLOP IMPLEMENTATION FACT DECLARATION
"""

from __future__ import annotations

import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
CONTRACT = (
    ROOT
    / "shared/contracts/taae_transformer_declared_reconstruction_contract.json"
)
DECISIONS = ROOT / "shared/contracts/reconstruction-decisions/Y36.json"
SCIENTIFIC_FILES = (
    ROOT / "hpc/taae_cpp/include/taae/model.hpp",
    ROOT / "hpc/taae_cpp/src/model.cpp",
    ROOT / "hpc/taae_cpp/tests/model_test.cpp",
    Path(__file__).resolve(),
)
SEMANTIC_ID = "taae_transformer_declared_reconstruction_v1"
DOI = "10.1109/JAS.2026.126233"


def main() -> int:
    contract = json.loads(CONTRACT.read_text(encoding="utf-8"))
    decisions = json.loads(DECISIONS.read_text(encoding="utf-8"))
    architecture = contract["paper_visible_architecture"]
    assert contract["method_semantic_id"] == SEMANTIC_ID
    assert contract["admission_status"] == (
        "kernel_verified_end_to_end_M3_method_not_admitted"
    )
    assert contract["blocked_original_algorithm_id"] == "taae"
    assert architecture == {
        "encoder_layers": 6,
        "autoregressive_decoder_layers": 6,
        "attention_heads": 4,
        "model_dimension": 64,
        "token_embedding_dimension": 64,
        "ffn_width_declared_completion": 256,
        "latent_dimension": 64,
        "latent_normalization": "L2_before_regression_metric_and_decoder",
        "regression_branch": (
            "hidden_linear_with_bias_then_ReLU_then_scalar_linear_with_bias"
        ),
        "decoder_start": "learned_BOS_embedding",
        "decoder_mask": "causal",
        "decoder_memory": (
            "latent_linear_projection_to_sequence_length_times_model_dimension"
            "_then_reshape"
        ),
        "normalization": "post_norm",
        "dropout": 0.0,
        "initialization": "deterministic_Xavier_uniform",
    }
    assert contract["optimizer"] == {
        "name": "Adam",
        "learning_rate": 0.001,
        "beta1": 0.9,
        "beta2": 0.999,
        "epsilon": 1e-08,
    }
    assert contract["losses"]["combined_fine_tune_weights"] == {
        "lambda_reg": 30.0,
        "lambda_smooth": 1.0,
    }
    assert "exactly batch_size" in contract["losses"]["metric_alignment"]
    assert contract["losses"]["l2_normalization_epsilon"] == 1e-12
    assert contract["losses"]["default_metric_pair_seed_hex"] == (
        "0x5943365041495253"
    )
    assert contract["declared_missing_field_completions"][
        "encoder_sequence_pooling"
    ].startswith("fixed-order arithmetic mean")
    assert contract["declared_missing_field_completions"][
        "embedding_ownership"
    ].startswith("separate encoder and decoder")
    assert contract["paper_scale_pretraining"] == {
        "layouts": 100000,
        "epochs": 500,
        "batch_size": 64,
        "learning_rate": 0.001,
        "embedding_dimension": 64,
        "latent_dimension": 64,
        "batch_order": (
            "deterministic Fisher-Yates shuffle from batch_seed for each epoch"
        ),
    }
    assert contract["checkpoint"]["format"] == "TAAE_KERNEL_CHECKPOINT_V2"
    assert contract["training_work_ledger"]["training_physical_fes"] == 0
    assert contract["verification"]["analytic_training_gradients"] is True
    assert contract["verification"]["finite_differences_used_for_training"] is False
    assert SEMANTIC_ID in decisions["profiles"]
    assert "end_to_end_M3_method_not_admitted" in decisions["completion_status"]
    for path in SCIENTIFIC_FILES:
        text = path.read_text(encoding="utf-8")
        declaration_end = text.index(
            "END WFLOP IMPLEMENTATION FACT DECLARATION"
        )
        declaration = text[:declaration_end]
        for required in (
            f"DOI: {DOI}",
            "Public author method source/checkpoint: unavailable",
            "Missing choices completed here:",
            "Reconstruction status:",
            "Method evidence tier: M3_DECLARED_COMPLETION",
            f"Method semantic ID: {SEMANTIC_ID}",
            "Controlling contract:",
            "Claim boundary:",
            "end-to-end M3 method is not admitted",
            "original taae remains blocked",
        ):
            assert required in declaration, f"{path}: missing {required}"
    print(
        "taae_transformer_kernel_contract_audit_pass "
        "architecture=6+6 heads=4 d_model=64 ffn=256 latent=64 "
        "end_to_end_M3=not_admitted original_taae=blocked"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
