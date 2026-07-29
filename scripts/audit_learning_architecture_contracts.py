#!/usr/bin/env python3
"""Fail-closed audit of the three Plan-004 learning architecture contracts."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Any

from audit_learning_pdf_consumption import audit as audit_pdf_consumption


ROOT = Path(__file__).resolve().parents[1]
CONTRACTS = {
    "Y36": (
        "shared/contracts/plan004_taae_transformer_architecture.json",
        "plan004_taae_transformer_architecture_v1",
        "taae_transformer_declared_reconstruction_v1",
        "TaaeTransformer",
    ),
    "T45": (
        "shared/contracts/plan004_alga_attention_architecture.json",
        "plan004_alga_attention_architecture_v1",
        "alga_attention_declared_reconstruction_v1",
        "AlgaAttention",
    ),
    "T42": (
        "shared/contracts/plan004_rlpso_ppo_architecture.json",
        "plan004_rlpso_ppo_architecture_v1",
        "rlpso_paper_corrected_training_reconstruction_v1",
        "RlpsoActorCritic",
    ),
}


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


def load(path: str) -> dict[str, Any]:
    return json.loads((ROOT / path).read_text(encoding="utf-8"))


def recursively_require_evidence(value: Any, location: str) -> None:
    """Every terminal contract object must identify an evidence source."""
    if isinstance(value, dict):
        terminal = any(
            key in value
            for key in (
                "shape",
                "weight_shape",
                "layer_shapes",
                "value",
                "formula",
                "type",
                "effect",
            )
        )
        if terminal:
            require(
                bool(value.get("evidence")),
                f"{location}: terminal field has no evidence",
            )
        for key, child in value.items():
            recursively_require_evidence(child, f"{location}.{key}")
    elif isinstance(value, list):
        for index, child in enumerate(value):
            recursively_require_evidence(child, f"{location}[{index}]")


def audit_taae(data: dict[str, Any]) -> None:
    tensors = data["tensor_contract"]
    architecture = data["architecture"]
    training = data["loss_and_training"]
    require(tensors["token_input"]["shape"] == ["batch", 15], "Y36 token shape")
    require(tensors["encoder_token_embedding"]["shape"] == [400, 64], "Y36 encoder embedding")
    require(tensors["decoder_logits"]["shape"] == ["batch", 15, 400], "Y36 logits")
    require(architecture["encoder_layers"]["value"] == 6, "Y36 encoder layers")
    require(architecture["decoder_layers"]["value"] == 6, "Y36 decoder layers")
    require(architecture["attention_heads"]["value"] == 4, "Y36 heads")
    require(architecture["feed_forward"]["shape"] == [64, 256, 64], "Y36 FFN")
    require(architecture["decoder_self_attention_mask"]["type"] == "strict_future_causal_mask", "Y36 causal mask")
    require(training["regression"]["weight"] == 30.0, "Y36 regression weight")
    require(training["pretraining"]["layouts"] == 100000, "Y36 layouts")
    require(training["pretraining"]["epochs"] == 500, "Y36 epochs")
    require(
        data["evolution_bridge"]["optimizer_consumer"].startswith(
            "plan004_learning_target_hpc --method taae"
        ),
        "Y36 bridge",
    )
    source = (ROOT / "hpc/taae_cpp/src/model.cpp").read_text(encoding="utf-8")
    for token in ("struct Attention", "struct EncoderLayer", "struct DecoderLayer", "causal", "metric_alignment", "xavier_uniform"):
        require(token in source, f"Y36 direct reference missing {token}")


def audit_alga(data: dict[str, Any]) -> None:
    tensors = data["tensor_contract"]
    architecture = data["architecture"]
    training = data["loss_and_training"]
    require(tensors["query_projection"]["shape"] == [8, "turbine_count"], "T45 query")
    require(tensors["attention_scores"]["shape"] == [8, 30, 30], "T45 scores")
    require(tensors["prediction"]["shape"] == [30], "T45 prediction")
    require(architecture["attention_heads"]["value"] == 8, "T45 heads")
    require(architecture["network_depth"]["forbidden"] == "generic_hidden_mlp", "T45 MLP guard")
    require(training["optimizer"]["type"] == "vanilla_full_batch_gradient_descent", "T45 optimizer")
    require(training["optimizer"]["learning_rate"] == 0.001, "T45 learning rate")
    require(training["optimizer"]["updates_per_generation"] == 1, "T45 update count")
    require(data["evolution_bridge"]["elite_count"]["value"] == 6, "T45 elites")
    source = (ROOT / data["evidence_status"]["reference_implementation"]["path"]).read_text(encoding="utf-8")
    for token in ("kAttentionHeads = 8", "struct AttentionModel", "normalized_fitness_targets", "train_one_full_batch_step", "model.query", "model.key", "model.value"):
        require(token in source, f"T45 direct reference missing {token}")


def audit_rlpso(data: dict[str, Any]) -> None:
    tensors = data["tensor_contract"]
    training = data["loss_and_training"]
    require(tensors["state"]["shape"] == ["batch", 2], "T42 state")
    require(tensors["actor"]["layer_shapes"] == [[256, 2], [64, 256], [4, 64]], "T42 actor")
    require(tensors["critic"]["layer_shapes"] == [[256, 2], [64, 256], [1, 64]], "T42 critic")
    require(tensors["critic"]["activations"][-1] == "ReLU", "T42 critic final ReLU")
    effects = [entry["effect"] for entry in data["action_semantics"]]
    require(
        effects == [
            "W_alpha += 0.001",
            "W_beta -= 0.001",
            "W_alpha -= 0.001",
            "W_beta += 0.001",
        ],
        "T42 action map",
    )
    require(training["discounted_return"]["gamma"] == 0.99, "T42 gamma")
    require(training["actor_objective"]["clip_epsilon"] == 0.2, "T42 clip")
    require(training["optimizer"]["update_epochs"] == 80, "T42 epochs")
    require(training["rollout"]["update_interval"] == 500, "T42 interval")
    require(data["swarm_bridge"]["policy_lifecycle"].startswith("one seeded persistent"), "T42 lifecycle")
    sources = "\n".join(
        (ROOT / path).read_text(encoding="utf-8")
        for path in data["evidence_status"]["reference_implementation"]["paths"]
    )
    for token in ("first_hidden_width = 256", "second_hidden_width = 64", "update_epochs = 80", "discounted_returns", "clipped_actor_objective", "rollout.clear"):
        require(token in sources, f"T42 direct reference missing {token}")


def audit_compatibility_guard() -> None:
    contract = load("shared/contracts/hpc_learning_libtorch_backend_contract.json")
    require(contract["contract_id"] == "backend_compatibility_only_v1", "compatibility contract id")
    require(contract["target_h5_admissible"] is False, "compatibility H5 must be false")
    source = (ROOT / "hpc/torch_training/src/main.cpp").read_text(encoding="utf-8")
    require("struct TargetModelImpl" in source, "compatibility probe unexpectedly absent")
    require("generic MLPs" in source, "generic probe boundary not explicit")
    for target_id in (
        "taae_transformer_declared_reconstruction_v1",
        "alga_attention_declared_reconstruction_v1",
        "rlpso_paper_corrected_training_reconstruction_v1",
    ):
        require(target_id not in source, f"generic probe impersonates {target_id}")
    for token in (
        "backend_compatibility_only_v1_taae_shape_probe",
        "backend_compatibility_only_v1_alga_shape_probe",
        "backend_compatibility_only_v1_rlpso_shape_probe",
    ):
        require(token in source, f"generic probe missing demoted id {token}")


def audit_optimized_libtorch(
    loaded: dict[str, dict[str, Any]]
) -> None:
    header = (
        ROOT
        / "hpc/learning_libtorch/include/wflop_learning/models.hpp"
    ).read_text(encoding="utf-8")
    source = (
        ROOT / "hpc/learning_libtorch/src/models.cpp"
    ).read_text(encoding="utf-8")
    executable = (
        ROOT / "hpc/learning_libtorch/src/main.cpp"
    ).read_text(encoding="utf-8")
    backend_test = (
        ROOT / "scripts/test_plan004_learning_backends.py"
    ).read_text(encoding="utf-8")
    require("TargetModelImpl" not in source, "optimized source is generic MLP")
    expected = {
        "Y36": (
            "TaaeTransformer",
            (
                "TaaeTransformerImpl",
                "TransformerEncoderLayer",
                "TransformerDecoderLayer",
                "taae_loss",
                "counter_keyed_xavier_uniform_",
                "taae_optimization_transition",
            ),
        ),
        "T45": (
            "AlgaAttention",
            (
                "AlgaAttentionImpl",
                "query_weight",
                "key_weight",
                "value_weight",
                "torch::softmax(scores, 2)",
                "alga_optimization_transition",
            ),
        ),
        "T42": (
            "RlpsoActorCritic",
            (
                "RlpsoActorCriticImpl",
                "torch::nn::Linear(2, 256)",
                "torch::nn::Linear(256, 64)",
                "torch::nn::Linear(64, 4)",
                "kRlpsoUpdateEpochs = 80",
                "kRlpsoUpdateInterval = 500",
                "rlpso_discounted_normalized_returns",
                "rlpso_ppo_loss",
                "cumsum(-1)",
                "rlpso_optimization_transition",
            ),
        ),
    }
    combined = header + "\n" + source
    for corpus, (module, tokens) in expected.items():
        implementation = loaded[corpus]["evidence_status"][
            "optimized_implementation"
        ]
        require(implementation["kind"] == "typed_libtorch_module", f"{corpus}: implementation kind")
        require(implementation["module"] == module, f"{corpus}: module")
        require(
            implementation["step2_receipt"]
            == "evidence/development/plan004_learning_libtorch_step2_20260730.json",
            f"{corpus}: Step2 receipt",
        )
        for token in tokens:
            require(token in combined, f"{corpus}: optimized token {token}")
    require("torch::optim::SGD optimizer" in executable, "ALGA SGD absent")
    require(
        executable.count("torch::optim::Adam optimizer") == 2,
        "TAAE/RLPSO Adam ownership mismatch",
    )
    for token in (
        "method_semantic_id",
        "problem_semantic_id",
        "model_config",
        "training_work",
        "learned_state_fnv1a64",
        "optimizer.save",
        "optimizer.load",
        "transfer_bounded_queue",
        "pin_memory",
        "torch::cuda::synchronize",
    ):
        require(token in source, f"artifact field absent: {token}")
    for token in ("cpu", "cuda", "hybrid"):
        require(token in backend_test, f"backend matrix missing {token}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--scope", choices=("core",), required=True)
    parser.parse_args()
    audit_pdf_consumption()
    loaded: dict[str, dict[str, Any]] = {}
    for corpus, (path, contract_id, method_id, module) in CONTRACTS.items():
        data = load(path)
        require(data["corpus_id"] == corpus, f"{corpus}: corpus id")
        require(data["contract_id"] == contract_id, f"{corpus}: contract id")
        require(data["method_semantic_id"] == method_id, f"{corpus}: semantic id")
        require(data["optimized_module"] == module, f"{corpus}: module")
        require(data["backend_contract"]["generic_backend_compatibility_is_not_target_evidence"] is True, f"{corpus}: compatibility boundary")
        recursively_require_evidence(data["tensor_contract"], f"{corpus}.tensor_contract")
        loaded[corpus] = data
    audit_taae(loaded["Y36"])
    audit_alga(loaded["T45"])
    audit_rlpso(loaded["T42"])
    audit_compatibility_guard()
    audit_optimized_libtorch(loaded)
    print(
        "learning_architecture_contract_audit_pass "
        "contracts=3 exact_modules=3 typed_libtorch_modules=3 "
        "artifact_bridges=3 corrected_pdf_receipts=2 "
        "generic_target_h5_admissible=no"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
