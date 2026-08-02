#!/usr/bin/env python3
"""Audit Plan-004 H5 state and reject backend self-agreement as equivalence."""

from __future__ import annotations

import argparse
import csv
import json
import math
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
REGISTRY = ROOT / "docs/hpc_core_target_pairs.tsv"
LEARNING = {"Y36", "T42", "T45"}
REOPENED = "reopened_reference_equivalence_missing"


def validation_path(analysis_path: str) -> Path:
    analysis = ROOT / analysis_path
    return analysis.with_name(
        analysis.name.replace("_hpc_analysis.json", "_hpc_validation.json")
    )


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


def audit_reopened(rows: list[dict[str, str]]) -> None:
    supersession = json.loads(
        (
            ROOT
            / "evidence/development/plan004_plan003_neural_h5_supersession_20260730.json"
        ).read_text(encoding="utf-8")
    )
    require(
        supersession["supersedes_commit"] == "25db016",
        "supersession must identify the invalid commit",
    )
    require(
        set(supersession["affected_corpus_ids"]) == LEARNING,
        "supersession corpus set mismatch",
    )
    for row in rows:
        data = json.loads(validation_path(row["analysis_path"]).read_text(encoding="utf-8"))
        h5 = data["H5_bounded_equivalence"]
        if row["corpus_id"] in LEARNING:
            require(h5["status"] == REOPENED, f"{row['pair_id']}: H5 not reopened")
            require(data["overall_status"] == REOPENED, f"{row['pair_id']}: overall not reopened")
            require(h5["candidate_backends"] == [], f"{row['pair_id']}: invalid candidates retained")
            require(
                h5["superseded_plan003_claim"] == "accepted_h5",
                f"{row['pair_id']}: missing superseded claim",
            )
            require(
                "self-agreement" in h5["comparison_scope"],
                f"{row['pair_id']}: invalid comparison not described",
            )


def audit_historical_reopened_regression(
    rows: list[dict[str, str]],
) -> None:
    supersession = json.loads(
        (
            ROOT
            / "evidence/development/"
            "plan004_plan003_neural_h5_supersession_20260730.json"
        ).read_text(encoding="utf-8")
    )
    compatibility = json.loads(
        (
            ROOT
            / "shared/contracts/"
            "hpc_learning_libtorch_backend_contract.json"
        ).read_text(encoding="utf-8")
    )
    require(
        supersession["supersedes_commit"] == "25db016",
        "historical supersession commit differs",
    )
    require(
        set(supersession["affected_corpus_ids"]) == LEARNING,
        "historical reopened set differs",
    )
    require(
        supersession["corrected_status"] == REOPENED
        and supersession["prior_claim"] == "accepted_h5",
        "historical reopened transition differs",
    )
    require(
        supersession["historical_raw_results_changed"] is False,
        "historical raw results were not preserved",
    )
    require(
        compatibility["contract_id"] == "backend_compatibility_only_v1"
        and compatibility["target_h5_admissible"] is False,
        "generic compatibility probe became target-H5 admissible",
    )
    require(
        {"target H5", "target H6", "formal backend selection"}
        <= set(supersession["forbidden_uses"]),
        "historical forbidden-use boundary differs",
    )
    current_learning = 0
    for row in rows:
        if row["corpus_id"] not in LEARNING:
            continue
        data = json.loads(
            validation_path(row["analysis_path"]).read_text(encoding="utf-8")
        )
        h5 = data["H5_bounded_equivalence"]
        require(
            h5["status"] == "accepted_h5"
            and h5["independent_reference"]["does_not_call_candidate"]
            is True,
            f"{row['pair_id']}: current closure lost independent H5",
        )
        current_learning += 1
    require(current_learning == 3, "current learning target count differs")


def audit_strict(rows: list[dict[str, str]]) -> None:
    accepted = 0
    for row in rows:
        data = json.loads(validation_path(row["analysis_path"]).read_text(encoding="utf-8"))
        h5 = data["H5_bounded_equivalence"]
        require(h5["status"] == "accepted_h5", f"{row['pair_id']}: H5 not accepted")
        independent = h5.get("independent_reference")
        require(isinstance(independent, dict), f"{row['pair_id']}: independent reference absent")
        require(independent.get("does_not_call_candidate") is True, f"{row['pair_id']}: reference independence")
        require(bool(independent.get("implementation_or_oracle")), f"{row['pair_id']}: reference identity")
        comparison = h5.get("numerical_comparison")
        require(isinstance(comparison, dict), f"{row['pair_id']}: numerical comparison absent")
        require(comparison.get("passed") is True, f"{row['pair_id']}: numerical comparison failed")
        maximum_error = comparison.get("maximum_absolute_error")
        require(
            not isinstance(maximum_error, bool)
            and isinstance(maximum_error, (int, float))
            and math.isfinite(maximum_error)
            and maximum_error >= 0.0,
            f"{row['pair_id']}: maximum error invalid",
        )
        require(
            "backend self-agreement alone is not accepted"
            in h5.get("comparison_scope", ""),
            f"{row['pair_id']}: self-agreement boundary absent",
        )
        if row["corpus_id"] in LEARNING:
            require(
                h5.get("runtime_test")
                == "plan004_learning_full_optimizer_artifacts",
                f"{row['pair_id']}: real full optimizer evidence absent",
            )
            require(
                isinstance(
                    comparison.get("absolute_tolerance_per_tensor"),
                    (int, float),
                )
                and isinstance(
                    comparison.get("observed_result", {}).get(
                        "tolerances"
                    ),
                    dict,
                ),
                f"{row['pair_id']}: per-tensor tolerances absent",
            )
            for field in (
                "forward_tensors",
                "losses",
                "all_named_parameter_gradients",
                "one_optimizer_step",
                "artifact_reload",
                "artifact_driven_transition",
                "physical_fes",
                "random_event_ownership",
                "terminal_partial_work",
            ):
                require(
                    h5.get("coverage", {}).get(field) == "passed",
                    f"{row['pair_id']}: learning H5 missing {field}",
                )
        accepted += 1
    print(f"hpc_equivalence_audit_pass pairs={accepted} independent_references={accepted} mode=strict")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--scope", choices=("core",), required=True)
    modes = parser.add_mutually_exclusive_group(required=True)
    modes.add_argument("--expect-reopened-learning", action="store_true")
    modes.add_argument(
        "--historical-reopened-regression", action="store_true"
    )
    modes.add_argument("--strict", action="store_true")
    args = parser.parse_args()
    with REGISTRY.open(encoding="utf-8", newline="") as handle:
        rows = list(csv.DictReader(handle, delimiter="\t"))
    require(len(rows) == 23, f"expected 23 core rows, found {len(rows)}")
    if args.expect_reopened_learning:
        audit_reopened(rows)
        print(
            "hpc_equivalence_audit_pass pairs=23 "
            "learning_reopened=3 generic_target_h5_admissible=no mode=reopened"
        )
    elif args.historical_reopened_regression:
        audit_historical_reopened_regression(rows)
        print(
            "hpc_equivalence_audit_pass pairs=23 "
            "historical_learning_reopened=3 "
            "generic_target_h5_admissible=no "
            "current_learning_h5=accepted mode=historical-regression"
        )
    else:
        audit_strict(rows)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
