#!/usr/bin/env python3
"""Audit Step 11 bounded profile, repeatability, timing, and quality receipts."""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import re
from pathlib import Path

from historical_binary_receipts import (
    verify_historical_binary,
    verify_historical_source,
)


ROOT = Path(__file__).resolve().parents[1]
SHA256 = re.compile(r"^[0-9a-f]{64}$")
EXPECTED_SCHEMAS = {
    "scalar_expected_power": [
        {"name": "best_expected_power", "unit": "kW", "direction": "maximize"}
    ],
    "scalar_aep": [
        {"name": "best_aep", "unit": "kWh/year", "direction": "maximize"}
    ],
    "structured_aep_capacity": [
        {"name": "best_aep", "unit": "kWh/year", "direction": "maximize"},
        {
            "name": "capacity_factor",
            "unit": "dimensionless",
            "direction": "maximize",
        },
    ],
    "taae_two_objective_front": [
        {
            "name": "reciprocal_expected_power",
            "unit": "1/kW",
            "direction": "minimize",
        },
        {
            "name": "average_a_weighted_noise",
            "unit": "dBA",
            "direction": "minimize",
        },
        {
            "name": "normalized_constraint_violation",
            "unit": "dimensionless",
            "direction": "minimize",
        },
    ],
    "tmoea_two_objective_front": [
        {"name": "aep", "unit": "kWh/year", "direction": "maximize"},
        {
            "name": "cable_cost",
            "unit": "contract_currency",
            "direction": "minimize",
        },
    ],
    "pbea_three_objective_front": [
        {"name": "inverse_power", "unit": "1/kW", "direction": "minimize"},
        {
            "name": "land_area",
            "unit": "grid_area_units",
            "direction": "minimize",
        },
        {
            "name": "total_cost",
            "unit": "contract_cost_units",
            "direction": "minimize",
        },
    ],
}
REQUIRED_METRIC_FIELDS = {
    "structured_aep_capacity": {
        "best_aep_kwh_per_year",
        "capacity_factor",
    },
    "taae_two_objective_front": {
        "min_reciprocal_expected_power_per_kw",
        "max_reciprocal_expected_power_per_kw",
        "min_average_a_weighted_noise_dba",
        "max_average_a_weighted_noise_dba",
        "max_normalized_constraint_violation",
    },
    "tmoea_two_objective_front": {
        "min_aep_kwh_per_year",
        "max_aep_kwh_per_year",
        "min_cable_cost",
        "max_cable_cost",
    },
    "pbea_three_objective_front": {
        "minimum_inverse_power_per_kw",
        "minimum_land_area_grid_units",
        "minimum_total_cost_contract_units",
        "maximum_inverse_power_per_kw",
        "maximum_land_area_grid_units",
        "maximum_total_cost_contract_units",
    },
}


def load(path: str) -> dict:
    return json.loads((ROOT / path).read_text(encoding="utf-8"))


def digest(path: str) -> str:
    return hashlib.sha256((ROOT / path).read_bytes()).hexdigest()


def fail(message: str) -> None:
    raise RuntimeError(message)

def finite(value: object) -> bool:
    return (
        isinstance(value, (int, float))
        and not isinstance(value, bool)
        and math.isfinite(value)
    )


PERFORMANCE_ONLY_FIELDS = {
    "requested_workers",
    "observed_workers",
    "resolved_workers",
    "timing_seconds",
    "total_wall_seconds",
    "evaluator_wall_seconds",
    "stage_receipts",
    "wall_seconds",
}


def scientific_payload(value: object) -> object:
    if isinstance(value, dict):
        return {
            key: scientific_payload(item)
            for key, item in value.items()
            if key not in PERFORMANCE_ONLY_FIELDS
        }
    if isinstance(value, list):
        return [scientific_payload(item) for item in value]
    return value


def scientific_digest(document: dict) -> str:
    encoded = json.dumps(
        scientific_payload(document),
        sort_keys=True,
        separators=(",", ":"),
        allow_nan=False,
    ).encode("utf-8")
    return hashlib.sha256(encoded).hexdigest()


def taae_front_metrics(raw: dict) -> dict[str, float]:
    front = raw["front"]
    return {
        "min_reciprocal_expected_power_per_kw": min(
            row["reciprocal_expected_power_per_kw"] for row in front
        ),
        "max_reciprocal_expected_power_per_kw": max(
            row["reciprocal_expected_power_per_kw"] for row in front
        ),
        "min_average_a_weighted_noise_dba": min(
            row["average_a_weighted_noise_dba"] for row in front
        ),
        "max_average_a_weighted_noise_dba": max(
            row["average_a_weighted_noise_dba"] for row in front
        ),
        "max_normalized_constraint_violation": max(
            row["normalized_constraint_violation"] for row in front
        ),
    }


def require_sha256(value: object, label: str) -> None:
    if not isinstance(value, str) or not SHA256.fullmatch(value):
        fail(f"{label}: invalid SHA-256")


def validate_quality_item(
    profile_id: str,
    schema: str,
    item: dict,
) -> None:
    values = item.get("metric_values")
    if schema in {"scalar_expected_power", "scalar_aep"}:
        if not finite(values):
            fail(f"{profile_id}: scalar metric is absent or non-finite")
    else:
        if not isinstance(values, dict):
            fail(f"{profile_id}: multi-field metric masquerades as scalar/hash")
        required = REQUIRED_METRIC_FIELDS[schema]
        if set(values) != required or not all(finite(v) for v in values.values()):
            fail(f"{profile_id}: metric fields differ from declared schema")
    if schema == "structured_aep_capacity":
        if not 0.0 <= values["capacity_factor"] <= 1.0:
            fail(f"{profile_id}: invalid capacity factor")
        if not item.get("layout_hash"):
            fail(f"{profile_id}: layout hash missing")
    elif schema == "taae_two_objective_front":
        if (
            item.get("front_count", 0) <= 0
            or not item.get("front_hash", "").startswith("fnv1a64:")
        ):
            fail(f"{profile_id}: TAAE front receipt missing")
        require_sha256(
            item.get("science_sha256"),
            f"{profile_id} seed {item.get('s')} science",
        )
    elif schema == "tmoea_two_objective_front":
        if (
            item.get("front_count", 0) <= 0
            or not item.get("front_hash", "").startswith("fnv1a64:")
        ):
            fail(f"{profile_id}: T-MOEA front receipt missing")
    elif schema == "pbea_three_objective_front":
        if item.get("front_count", 0) <= 0:
            fail(f"{profile_id}: PBEA front count missing")
        require_sha256(
            item.get("front_artifact_sha256"),
            f"{profile_id} seed {item.get('s')} front artifact",
        )
    if schema != "taae_two_objective_front":
        require_sha256(item.get("h"), f"{profile_id} seed {item.get('s')} science")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--scope", choices=("core",))
    parser.add_argument("--strict", action="store_true")
    arguments = parser.parse_args()
    if arguments.scope is not None or arguments.strict:
        if arguments.scope != "core" or not arguments.strict:
            parser.error("Plan-005 performance audit requires --scope core --strict")
        from audit_plan005_h6_receipts import audit

        result = audit(
            raw_path=(
                ROOT
                / "evidence/performance/"
                "plan005_h6_performance_first_raw_observations_20260730.jsonl"
            ),
            summary_path=(
                ROOT
                / "evidence/performance/"
                "plan005_h6_performance_first_summary_20260730.json"
            ),
        )
        print(
            "performance_receipt_audit_pass "
            f"scope=core pairs={result['pairs']} "
            f"observations={result['observations']} mode=strict"
        )
        return 0
    matrix = load("shared/contracts/global_execution_capability_matrix.json")
    registry = load("shared/contracts/executable_profile_evidence.json")
    contract = load("shared/contracts/step11_bounded_development_receipts.json")
    primary = load("evidence/development/step11_profile_runs_spark_20260729.json")
    rlpso = load("evidence/development/rlpso_bounded_step11_spark_20260729.json")

    expected = {
        row["profile_id"]
        for row in matrix["profile_bindings"]
        if "reconstruction" in row["admission_status"]
    }
    registry_by_id = {row["profile_id"]: row for row in registry["profiles"]}
    observed = {row["profile_id"] for row in primary["profiles"]}
    observed.update(
        f"{method}__rpso2024_source_problem_ws1_ws4"
        for method in rlpso["profiles"]
    )
    if expected != observed or len(expected) != 15:
        fail("Step 11 bounded receipt coverage differs from matrix-derived scope")

    for receipt in contract["bounded_run_receipts"]:
        if digest(receipt["path"]) != receipt["sha256"]:
            fail(f"bounded receipt hash changed: {receipt['path']}")
    spec_profiles: set[str] = set()
    for spec in contract["execution_spec_groups"]:
        spec_profiles.update(spec["profiles"])
        for key in (
            "binary_target", "binary_path", "binary_sha256",
            "command_template", "case_id", "asset_path", "asset_sha256",
            "budget", "fes_denominator",
        ):
            if not spec.get(key):
                fail(f"execution spec lacks {key}")
        require_sha256(spec["binary_sha256"], f"{spec['binary_target']} binary")
        require_sha256(spec["asset_sha256"], f"{spec['case_id']} asset")
        verify_historical_binary(
            spec["binary_target"],
            spec["binary_path"],
            spec["binary_sha256"],
        )
        if digest(spec["asset_path"]) != spec["asset_sha256"]:
            fail(f"execution asset hash changed: {spec['asset_path']}")
        command = spec["command_template"]
        if (
            "--seed" not in command
            or "--workers" not in command
            or not any(
                flag in command
                for flag in ("--compute-backend cpu", "--backend cpu",
                             "--execution-mode cpu")
            )
        ):
            fail(f"{spec['binary_target']}: command is not seeded CPU work")
        populations = spec.get("initial_population_by_profile")
        if populations is None:
            population = spec.get("population_or_initial_population")
            if not isinstance(population, int) or population <= 0:
                fail(f"{spec['case_id']}: initial population is not numeric")
            populations = {profile_id: population for profile_id in spec["profiles"]}
        if set(populations) != set(spec["profiles"]):
            fail(f"{spec['case_id']}: population map coverage differs")
        quality_budget = spec.get("quality_budget", spec["budget"])
        if not isinstance(quality_budget, int):
            fail(f"{spec['case_id']}: quality budget is not numeric")
        for profile_id, population in populations.items():
            if not isinstance(population, int) or quality_budget <= population:
                fail(f"{profile_id}: workload completes no algorithm transition")
    if spec_profiles != expected:
        fail("execution specs do not cover the exact derived profile scope")

    seeds = primary["seeds"]
    if seeds != [2026072901, 2026072902, 2026072903]:
        fail("quality seed set drifted")
    schemas = primary["quality_metric_schemas"]
    if schemas != EXPECTED_SCHEMAS:
        fail("quality schema names, units, or directions drifted")
    if set(primary["profile_quality_schema"]) != {
        row["profile_id"] for row in primary["profiles"]
    }:
        fail("typed quality schemas do not cover all primary profiles")
    for row in primary["profiles"]:
        profile = registry_by_id[row["profile_id"]]
        if (
            row["method_semantics_id"] != profile["method_semantics_id"]
            or row["problem_semantics_id"] != profile["problem_semantics_id"]
        ):
            fail(f"{row['profile_id']}: semantic ID drift")
        repeat = row["repeat"]
        if repeat["workers"] != [1, 20, 20] or len(repeat["raw"]) != 3:
            fail(f"{row['profile_id']}: missing 1/20/20 repeatability")
        require_sha256(repeat["science"], f"{row['profile_id']} repeat science")
        if len(set(repeat["raw"])) != 3:
            fail(f"{row['profile_id']}: output hashes are incomplete")
        for raw_hash in repeat["raw"]:
            require_sha256(raw_hash, f"{row['profile_id']} raw output")
        raw_paths = repeat.get("raw_paths")
        if raw_paths is not None:
            if len(raw_paths) != 3:
                fail(f"{row['profile_id']}: raw repeat path coverage differs")
            raw_documents = [load(path) for path in raw_paths]
            if [
                digest(path) for path in raw_paths
            ] != repeat["raw"]:
                fail(f"{row['profile_id']}: raw repeat hash changed")
            science = [scientific_digest(item) for item in raw_documents]
            if len(set(science)) != 1 or science[0] != repeat["science"]:
                fail(f"{row['profile_id']}: 1/20/20 scientific output differs")
        for timing in (repeat["t1"], repeat["t20"]):
            if len(timing) != 4 or timing[0] is None or timing[1] is None:
                fail(f"{row['profile_id']}: timing components are incomplete")
            if any(value is not None and not finite(value) for value in timing):
                fail(f"{row['profile_id']}: timing is non-finite")
            if not math.isclose(
                timing[3],
                sum(value for value in timing[:3] if value is not None),
                rel_tol=1.0e-9,
                abs_tol=1.0e-12,
            ):
                fail(f"{row['profile_id']}: timing decomposition is inconsistent")
        if [item["s"] for item in row["quality"]] != seeds:
            fail(f"{row['profile_id']}: three-seed quality receipt is absent")
        schema = primary["profile_quality_schema"][row["profile_id"]]
        if schema not in schemas:
            fail(f"{row['profile_id']}: unknown quality schema")
        for item in row["quality"]:
            validate_quality_item(row["profile_id"], schema, item)
            if "raw_path" in item:
                if digest(item["raw_path"]) != item["raw_sha256"]:
                    fail(f"{row['profile_id']}: raw quality hash changed")
                raw = load(item["raw_path"])
                expected_hash = item.get("science_sha256", item.get("h"))
                if scientific_digest(raw) != expected_hash:
                    fail(f"{row['profile_id']}: raw quality science changed")
                if raw["seed"] != item["s"]:
                    fail(f"{row['profile_id']}: raw quality seed drifted")
                if schema == "scalar_expected_power":
                    value = (
                        raw["best"]["expected_power_kw"]
                        if "best" in raw
                        else raw["best_expected_power_kw"]
                    )
                    if value != item["metric_values"]:
                        fail(f"{row['profile_id']}: scalar aggregate drifted")
                elif schema == "taae_two_objective_front":
                    if (
                        raw["generations"] != item["completed_generations"]
                        or len(raw["front"]) != item["front_count"]
                        or raw["front_hash"] != item["front_hash"]
                        or taae_front_metrics(raw) != item["metric_values"]
                    ):
                        fail(f"{row['profile_id']}: TAAE aggregate drifted")
        if row["profile_id"].startswith("taae_"):
            if row.get("quality_physical_fes", 0) < 200:
                fail("TAAE quality workload did not pass initialization")
            if any(item["completed_generations"] < 1 for item in row["quality"]):
                fail("TAAE quality receipt completed no latent generation")
        elif row["physical_fes"] <= 0:
            fail(f"{row['profile_id']}: physical FES is absent")

    if rlpso["scope"]["quality_seed_set_predetermined"] != seeds:
        fail("RLPSO seed set differs")
    for method, receipt in rlpso["profiles"].items():
        if receipt["repeatability"]["workers"] != [1, 20, 20]:
            fail(f"{method}: missing 1/20/20 repeatability")
        if len(receipt["quality_results"]) != 3:
            fail(f"{method}: missing three-seed quality")
        if len(receipt["repeatability"]["raw_stdout_sha256"]) != 3:
            fail(f"{method}: raw output hashes missing")
        require_sha256(
            receipt["repeatability"]["canonical_scientific_output_sha256"],
            f"{method} repeat science",
        )
        for raw_hash in receipt["repeatability"]["raw_stdout_sha256"]:
            require_sha256(raw_hash, f"{method} raw output")
        raw_paths = receipt["repeatability"].get("raw_paths", [])
        if (
            len(raw_paths) != 3
            or [digest(path) for path in raw_paths]
                != receipt["repeatability"]["raw_stdout_sha256"]
        ):
            fail(f"{method}: raw repeatability evidence changed")
        raw_documents = [load(path) for path in raw_paths]
        science = [scientific_digest(item) for item in raw_documents]
        if (
            len(set(science)) != 1
            or science[0] != receipt["repeatability"][
                "canonical_scientific_output_sha256"
            ]
        ):
            fail(f"{method}: 1/20/20 scientific output differs")
        for quality in receipt["quality_results"]:
            if not finite(quality.get("best_expected_power_kw")):
                fail(f"{method}: non-finite quality metric")
            require_sha256(quality.get("science_sha256"), f"{method} quality")
            if digest(quality["raw_path"]) != quality["raw_sha256"]:
                fail(f"{method}: raw quality hash changed")
            raw = load(quality["raw_path"])
            if (
                scientific_digest(raw) != quality["science_sha256"]
                or raw["best_expected_power_kw"]
                    != quality["best_expected_power_kw"]
            ):
                fail(f"{method}: raw quality aggregate drifted")

    for group in contract["coverage_groups"]:
        if digest(group["evidence_path"]) != group["evidence_sha256"]:
            fail(f"accepted evidence hash changed: {group['evidence_path']}")
        verify_historical_source(
            group["oracle_path"],
            group["oracle_sha256"],
            contract["baseline_commit"],
        )
    print(
        "performance_receipt_audit_pass "
        f"profiles={len(expected)} quality_seeds={len(seeds)} "
        "worker_pair=1_vs_20 repeat_at_20=true mode=cpu"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
