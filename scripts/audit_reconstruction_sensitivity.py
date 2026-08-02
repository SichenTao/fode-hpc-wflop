#!/usr/bin/env python3
"""Audit complete, non-post-hoc reconstruction sensitivity obligations."""

from __future__ import annotations

import glob
import hashlib
import json
from pathlib import Path

from historical_binary_receipts import verify_historical_binary


ROOT = Path(__file__).resolve().parents[1]
ALLOWED = {
    "executed",
    "accepted_existing_evidence",
    "scientifically_inadmissible_with_reason",
}


def load(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


def fail(message: str) -> None:
    raise RuntimeError(message)

def digest(path: str) -> str:
    return hashlib.sha256((ROOT / path).read_bytes()).hexdigest()


def main() -> int:
    receipt = load(
        ROOT / "shared/contracts/step11_reconstruction_sensitivity_receipts.json"
    )
    decisions: dict[tuple[str, str], dict] = {}
    for name in glob.glob(
        str(ROOT / "shared/contracts/reconstruction-decisions/*.json")
    ):
        ledger = load(Path(name))
        for decision in ledger["decisions"]:
            if decision["classification"] == "quality_sensitive":
                decisions[(ledger["corpus_id"], decision["field"])] = decision
    obligations = {
        (row["corpus_id"], row["field"]): row
        for row in receipt["obligations"]
    }
    if len(obligations) != len(receipt["obligations"]):
        fail("duplicate sensitivity obligation")
    if set(obligations) != set(decisions):
        fail("sensitivity receipt differs from ledger-derived obligations")

    for corpus, evidence in receipt["evidence_by_corpus"].items():
        observed = hashlib.sha256((ROOT / evidence["path"]).read_bytes()).hexdigest()
        if observed != evidence["sha256"]:
            fail(f"{corpus}: immutable sensitivity evidence hash changed")

    executed = accepted = inadmissible = 0
    for key, decision in decisions.items():
        row = obligations[key]
        status = row["selected_baseline_status"]
        if status not in ALLOWED or not row["selected_baseline_reason"].strip():
            fail(f"{key}: unexplained baseline disposition")
        rejected = decision["rejected_alternatives"]
        choices = row.get("rejected_choices", [])
        alternatives = row["admissible_alternatives"]
        if len(choices) != len(rejected):
            fail(f"{key}: rejected textual choices are not fully classified")
        for index, (choice, text_value) in enumerate(zip(choices, rejected)):
            if choice.get("index") != index or choice.get("text") != text_value:
                fail(f"{key}: rejected choice {index} does not match ledger text")
            if (
                choice.get("disposition") not in {"executed", "scientific_stop"}
                or not choice.get("reason", "").strip()
            ):
                fail(f"{key}: rejected choice {index} lacks a typed reason")
            if choice["disposition"] == "executed":
                if (
                    not choice.get("semantic_id")
                    or not choice.get("result_locator")
                ):
                    fail(f"{key}: executed choice {index} lacks evidence mapping")
                if not any(
                    alternative["status"] == "executed"
                    and alternative.get("result_locator")
                        == choice["result_locator"]
                    for alternative in alternatives
                ):
                    fail(
                        f"{key}: executed choice {index} is not mapped "
                        "to a registered executed alternative"
                    )
            elif (
                choice.get("semantic_id") is not None
                or choice.get("result_locator") is not None
            ):
                fail(f"{key}: scientific stop {index} masquerades as executed")
            else:
                inadmissible += 1
        if (
            not alternatives
            and status != "executed"
            and not row["result"].startswith("SCIENTIFIC_STOP_")
        ):
            fail(f"{key}: quality-sensitive field has no executed alternative")
        for alternative in alternatives:
            if alternative["status"] not in ALLOWED:
                fail(f"{key}: invalid alternative disposition")
            if alternative["status"] == "executed":
                executed += 1
                if not alternative.get("result_locator"):
                    fail(f"{key}: executed alternative lacks results")
            elif alternative["status"] == "accepted_existing_evidence":
                accepted += 1
            else:
                inadmissible += 1
        if status == "executed":
            executed += 1
        elif status == "accepted_existing_evidence":
            accepted += 1
        else:
            inadmissible += 1

    fqfode = load(
        ROOT
        / "shared/contracts/fqfode_seeded_training_reconstruction_contract.json"
    )
    recommended = {
        row["recommended_sensitivity_profile"]
        for row in fqfode["paper_internal_conflicts"]
    }
    registered = {
        alternative["id"]
        for row in receipt["obligations"]
        if row["corpus_id"] == "S04"
        for alternative in row["admissible_alternatives"]
    }
    if not recommended <= registered:
        fail("S04 recommended sensitivity profiles are not all registered")

    fqfode_receipt = load(
        ROOT
        / "evidence/development/fqfode_sensitivity_step11_spark_20260729.json"
    )
    if (
        fqfode_receipt["case_id"] != "WS2tn50"
        or fqfode_receipt["physical_fes_per_run"] != 24000
        or fqfode_receipt["predetermined_seeds"]
            != receipt["predetermined_seed_set"]
        or fqfode_receipt["compute_backend"] != "cpu"
    ):
        fail("S04 sensitivity workload or seed contract drifted")
    verify_historical_binary(
        "wflop_cpp_hpc",
        fqfode_receipt["binary_path"],
        fqfode_receipt["binary_sha256"],
    )
    artifact = fqfode_receipt["independent_stage_artifact"]
    if (
        digest(artifact["path"]) != artifact["sha256"]
        or artifact["training_physical_fes"] != 158800
    ):
        fail("S04 independent-stage artifact is stale or incomplete")
    expected_profiles = {
        "baseline": "fqfode_seeded_training_declared_reconstruction_v1",
        "multiplicative-action":
            "fqfode_seeded_training_multiplicative_sensitivity_v1",
        "fes-normalized-stage":
            "fqfode_seeded_training_fes_normalized_stage_sensitivity_v1",
        "wrap-after-generation-200":
            "fqfode_generation200_wrap_stage_sensitivity_v1",
        "independent-stage-pretraining":
            "fqfode_independent_stage_pretraining_sensitivity_v1",
    }
    observed_profiles = {
        profile["profile_id"]: profile for profile in fqfode_receipt["profiles"]
    }
    if set(observed_profiles) != set(expected_profiles):
        fail("S04 sensitivity profile registry coverage drifted")
    for profile_id, semantics_id in expected_profiles.items():
        profile = observed_profiles[profile_id]
        if profile["effective_semantics_id"] != semantics_id:
            fail(f"S04 {profile_id}: effective semantics drifted")
        runs = profile["runs"]
        if [run["seed"] for run in runs] != receipt["predetermined_seed_set"]:
            fail(f"S04 {profile_id}: seed set drifted")
        for run in runs:
            if digest(run["path"]) != run["sha256"]:
                fail(f"S04 {profile_id}: raw run hash changed")
            raw = load(ROOT / run["path"])
            if (
                raw["case_id"] != "WS2tn50"
                or raw["physical_fes"] != 24000
                or raw["generations"] <= 200
                or raw["policy_interactions"] != raw["generations"]
                or sum(raw["policy_stage_interactions"])
                    != raw["policy_interactions"]
                or sum(raw["policy_stage_updates"]) != raw["policy_updates"]
                or any(value <= 0 for value in raw["policy_stage_interactions"])
            ):
                fail(f"S04 {profile_id}: work or four-stage receipt invalid")
            for field in (
                "best_expected_power_kw", "generations",
                "policy_interactions", "policy_updates",
                "policy_stage_interactions", "policy_stage_updates",
                "learned_state_hash",
            ):
                aggregate_field = {
                    "policy_stage_interactions": "stage_interactions",
                    "policy_stage_updates": "stage_updates",
                }.get(field, field)
                if raw[field] != run[aggregate_field]:
                    fail(f"S04 {profile_id}: aggregate/raw mismatch for {field}")
            if raw["effective_semantics_id"] != semantics_id:
                fail(f"S04 {profile_id}: raw semantic ID mismatch")
        if profile_id == "baseline" and profile["method_id"] != (
            "FQFODE_SEEDED_TRAINING_DECLARED_RECONSTRUCTION_V1"
        ):
            fail("S04 baseline method_id changed")

    sensitivity = load(
        ROOT
        / "evidence/development/"
          "step11_quality_sensitivity_profiles_spark_20260729.json"
    )
    if (
        sensitivity["predetermined_seeds"] != receipt["predetermined_seed_set"]
        or sensitivity["compute_backend"] != "cpu"
        or sensitivity["workers"] != 20
        or "never pooled" not in sensitivity["claim_boundary"]
        or "observed quality did not select" not in (
            sensitivity["selection_policy"]
        )
    ):
        fail("T43/T45/Y36 sensitivity boundary drifted")
    sensitivity_targets = {
        "wflop": "wflop_cpp_hpc",
        "ppga": "ppga_nantong_hpc",
        "taae": "taae_evolution_hpc",
    }
    for name, binary in sensitivity["binaries"].items():
        verify_historical_binary(
            sensitivity_targets[name],
            binary["path"],
            binary["sha256"],
        )

    expected_family_profiles = {
        "T45": {
            "width1": (
                "alga_attention_declared_reconstruction_v1",
                "alga_guishan_planar_wind_fode_evaluator_transfer_v1",
                1000,
            ),
            "width2": (
                "alga_attention_width2_sensitivity_v1",
                "alga_guishan_planar_wind_fode_evaluator_transfer_v1",
                1000,
            ),
        },
        "T43": {
            "rss": (
                "ppga_nantong_structured_3d_declared_reconstruction_v2",
                "ppga_nantong_structured_3d_declared_proxy_v1",
                60,
            ),
            "multiplicative": (
                "ppga_nantong_structured_3d_declared_reconstruction_v2",
                "ppga_nantong_structured_3d_multiplicative_wake_sensitivity_v1",
                60,
            ),
        },
        "Y36": {
            "baseline": (
                "taae_transformer_evolution_declared_reconstruction_v1",
                "taae_zhangbei_structured_declared_proxy_v1",
                200,
            ),
            "regression-half": (
                "taae_transformer_evolution_regression15_sensitivity_v1",
                "taae_zhangbei_structured_declared_proxy_v1",
                200,
            ),
            "multiplicative-wake": (
                "taae_transformer_evolution_declared_reconstruction_v1",
                "taae_zhangbei_structured_multiplicative_wake_sensitivity_v1",
                200,
            ),
        },
    }
    observed_semantics = set(expected_profiles.values())
    for corpus, expected in expected_family_profiles.items():
        family = sensitivity["families"][corpus]
        if (
            corpus in {"T43", "Y36"}
            and "no cross-semantic pooling or ranking"
                not in family["problem_comparison_rule"]
        ):
            fail(f"{corpus}: cross-semantic comparison boundary is absent")
        profiles = {
            profile["profile_id"]: profile
            for profile in family["profiles"]
        }
        if set(profiles) != set(expected):
            fail(f"{corpus}: sensitivity profile coverage drifted")
        for profile_id, (method_id, problem_id, fes) in expected.items():
            profile = profiles[profile_id]
            observed_semantics.add(method_id)
            observed_semantics.add(problem_id)
            if (
                profile["method_semantics_id"] != method_id
                or profile["problem_semantics_id"] != problem_id
            ):
                fail(f"{corpus}/{profile_id}: semantic identity drifted")
            runs = profile["runs"]
            if [run["seed"] for run in runs] != receipt["predetermined_seed_set"]:
                fail(f"{corpus}/{profile_id}: seed set drifted")
            for run in runs:
                if digest(run["path"]) != run["sha256"]:
                    fail(f"{corpus}/{profile_id}: raw evidence hash changed")
                raw = load(ROOT / run["path"])
                raw_method = raw.get(
                    "effective_semantics_id",
                    raw.get("method_semantic_id"),
                )
                raw_problem = raw.get(
                    "problem_semantics_id",
                    raw.get("problem_semantic_id"),
                )
                if (
                    raw["seed"] != run["seed"]
                    or raw["physical_fes"] != fes
                    or raw_method != method_id
                    or raw_problem != problem_id
                ):
                    fail(f"{corpus}/{profile_id}: raw/aggregate drifted")

    t45 = {
        row["profile_id"]: row
        for row in sensitivity["families"]["T45"]["profiles"]
    }
    if t45["width1"]["method_id"] != (
        "ALGA_ATTENTION_DECLARED_RECONSTRUCTION_V1"
    ):
        fail("T45 baseline method ID changed")
    if any(
        left["learned_state_hash"] == right["learned_state_hash"]
        for left, right in zip(
            t45["width1"]["runs"],
            t45["width2"]["runs"],
        )
    ):
        fail("T45 width sensitivity did not alter learned state")

    t43 = {
        row["profile_id"]: row
        for row in sensitivity["families"]["T43"]["profiles"]
    }
    if (
        t43["rss"]["problem_semantic_hash"] != "ee06013d8778fd7e"
        or t43["multiplicative"]["problem_semantic_hash"]
            != "4ec1944aac0f61d3"
        or all(
            left["best_expected_power_kw"] == right["best_expected_power_kw"]
            for left, right in zip(
                t43["rss"]["runs"],
                t43["multiplicative"]["runs"],
            )
        )
    ):
        fail("T43 wake sensitivity lacks distinct physics/hash")

    y36 = {
        row["profile_id"]: row
        for row in sensitivity["families"]["Y36"]["profiles"]
    }
    if (
        y36["baseline"]["problem_semantic_hash"]
            == y36["multiplicative-wake"]["problem_semantic_hash"]
        or y36["baseline"]["fine_tune_loss_weights"]["regression"] != 30
        or y36["regression-half"]["fine_tune_loss_weights"]["regression"] != 15
        or any(
            left["model_hash"] == right["model_hash"]
            for left, right in zip(
                y36["baseline"]["runs"],
                y36["regression-half"]["runs"],
            )
        )
        or all(
            left["front_hash"] == right["front_hash"]
            for left, right in zip(
                y36["baseline"]["runs"],
                y36["regression-half"]["runs"],
            )
        )
        or all(
            left["front_hash"] == right["front_hash"]
            for left, right in zip(
                y36["baseline"]["runs"],
                y36["multiplicative-wake"]["runs"],
            )
        )
    ):
        fail("Y36 loss/wake sensitivity lacks distinct execution semantics")

    rlpso = load(
        ROOT / "evidence/development/rlpso_bounded_step11_spark_20260729.json"
    )
    observed_semantics.update(rlpso["profiles"])
    for row in receipt["obligations"]:
        for choice in row["rejected_choices"]:
            if (
                choice["disposition"] == "executed"
                and choice["semantic_id"] not in observed_semantics
            ):
                fail(
                    f"{row['corpus_id']}/{row['field']}: executed choice "
                    "semantic ID is not present in evidence"
                )

    print(
        "reconstruction_sensitivity_audit_pass "
        f"obligations={len(decisions)} executed={executed} "
        f"accepted={accepted} inadmissible={inadmissible}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
