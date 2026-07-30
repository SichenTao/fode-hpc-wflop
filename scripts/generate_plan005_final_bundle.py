#!/usr/bin/env python3
"""Derive the Plan-005 public closure bundle from immutable raw evidence."""

from __future__ import annotations

import csv
import hashlib
import json
import math
import os
import statistics
from pathlib import Path
from typing import Any

from audit_plan005_campaigns import audit_result
from plan005_formal_common import (
    FINAL_MANIFEST,
    H6_SUMMARY,
    ROOT,
    require,
    result_path,
    sha256,
    validate_manifest,
)


OUTPUT = ROOT / "evidence/closure/plan005_final_bundle.json"
PAPER_MATRIX = ROOT / "docs/paper_package_completion.tsv"
SOURCE_REGISTRY = ROOT / "docs/source_asset_registry.tsv"
GAP_LEDGER = ROOT / "docs/paper_asset_gap_ledger.tsv"
FACT_REGISTRY = ROOT / "docs/target_source_fact_declaration_registry.tsv"
FACT_POLICY = ROOT / "docs/academic_reproduction_and_hpc_acceptance_policy.md"


def read_tsv(path: Path) -> list[dict[str, str]]:
    with path.open(encoding="utf-8", newline="") as handle:
        return list(csv.DictReader(handle, delimiter="\t"))


def by_corpus(rows: list[dict[str, str]]) -> dict[str, dict[str, str]]:
    result: dict[str, dict[str, str]] = {}
    for row in rows:
        corpus_id = row["corpus_id"]
        require(corpus_id not in result, f"duplicate corpus row: {corpus_id}")
        result[corpus_id] = row
    return result


def nested_number(document: dict[str, Any], *path: str) -> float | None:
    value: Any = document
    for key in path:
        if not isinstance(value, dict) or key not in value:
            return None
        value = value[key]
    if (
        isinstance(value, (int, float))
        and not isinstance(value, bool)
        and math.isfinite(float(value))
    ):
        return float(value)
    return None


QUALITY_PATHS: dict[str, tuple[tuple[str, ...], ...]] = {
    "best_expected_power_kw": (
        ("best_expected_power_kw",),
        ("best", "expected_power_kw"),
    ),
    "best_aep_kwh": (
        ("best_aep_kwh",),
        ("best", "aep_kwh"),
    ),
    "best_lcoe": (("best_lcoe",),),
    "best_cable_cost": (("best_cable_cost",),),
    "best_capacity_factor": (
        ("best_capacity_factor",),
        ("best", "capacity_factor"),
    ),
    "conversion_efficiency_percent": (
        ("conversion_efficiency_percent",),
    ),
    "conversion_efficiency_ratio": (
        ("best", "conversion_efficiency"),
    ),
    "minimum_inverse_power": (("minimum_inverse_power",),),
    "minimum_land_area_grid_units": (
        ("minimum_land_area_grid_units",),
    ),
    "minimum_total_cost": (("minimum_total_cost",),),
    "nondominated_count": (("nondominated_count",),),
}


def quality_values(raw: dict[str, Any]) -> dict[str, float]:
    values: dict[str, float] = {}
    for name, candidates in QUALITY_PATHS.items():
        for path in candidates:
            value = nested_number(raw, *path)
            if value is not None:
                values[name] = value
                break
    front = raw.get("front")
    if isinstance(front, list):
        values["front_size"] = float(len(front))
    return values


def stage_seconds(raw: dict[str, Any], name: str) -> float | None:
    direct_key = {
        "end_to_end": "end_to_end_seconds",
        "evaluator": "evaluator_seconds",
        "algorithm": "algorithm_seconds",
    }[name]
    direct_value = nested_number(raw, direct_key)
    if direct_value is not None:
        return direct_value
    timing = raw.get("timing_seconds")
    if isinstance(timing, dict):
        aliases = {
            "end_to_end": ("end_to_end",),
            "evaluator": ("evaluator",),
            "algorithm": ("algorithm",),
        }
        for key in aliases[name]:
            value = nested_number(timing, key)
            if value is not None:
                return value
    if name == "end_to_end":
        for key in ("total_wall_seconds", "wall_seconds"):
            value = nested_number(raw, key)
            if value is not None:
                return value
    stages = raw.get("stage_receipts")
    if not isinstance(stages, dict):
        stages = raw.get("stages")
    if isinstance(stages, dict):
        evaluator = nested_number(stages, "evaluator", "wall_seconds")
        total = sum(
            nested_number(value, "wall_seconds") or 0.0
            for value in stages.values()
            if isinstance(value, dict)
        )
        if name == "evaluator" and evaluator is not None:
            return evaluator
        if name == "algorithm" and total > 0.0:
            return max(0.0, total - (evaluator or 0.0))
        if name == "end_to_end" and total > 0.0:
            return total
    return None


def summary(values: list[float]) -> dict[str, float | int]:
    require(bool(values), "cannot summarize an empty observation set")
    return {
        "count": len(values),
        "minimum": min(values),
        "median": statistics.median(values),
        "mean": statistics.fmean(values),
        "maximum": max(values),
        "population_standard_deviation": statistics.pstdev(values),
    }


def case_summary(
    campaign: dict[str, Any],
    case: dict[str, Any],
    observations: list[dict[str, Any]],
) -> dict[str, Any]:
    require(
        len(observations) == campaign["optimization_seed_count"],
        f"{campaign['pair_id']} {case['case_id']}: seed cardinality drift",
    )
    quality: dict[str, list[float]] = {}
    external_wall: list[float] = []
    effective_cores: list[float] = []
    throughput: list[float] = []
    stages: dict[str, list[float]] = {
        "end_to_end": [],
        "evaluator": [],
        "algorithm": [],
    }
    for document in observations:
        raw = document["raw_result"]
        process = document["process"]
        wall = float(process["external_wall_seconds"])
        cpu = float(process["user_cpu_seconds"]) + float(
            process["system_cpu_seconds"]
        )
        external_wall.append(wall)
        effective_cores.append(cpu / wall)
        throughput.append(document["observed_physical_fes"] / wall)
        for name, value in quality_values(raw).items():
            quality.setdefault(name, []).append(value)
        for name in stages:
            value = stage_seconds(raw, name)
            if value is not None:
                stages[name].append(value)
    require(
        bool(quality),
        f"{campaign['pair_id']} {case['case_id']}: no quality metric mapped",
    )
    require(
        all(len(values) == len(observations) for values in quality.values()),
        f"{campaign['pair_id']} {case['case_id']}: partial quality coverage",
    )
    require(
        all(len(values) == len(observations) for values in stages.values()),
        f"{campaign['pair_id']} {case['case_id']}: partial timing coverage",
    )
    return {
        "case_id": case["case_id"],
        "case_semantic_hash": case["case_semantic_hash"],
        "physical_fes_per_run": case["physical_fes_per_run"],
        "seed_count": len(observations),
        "quality": {
            name: summary(values) for name, values in sorted(quality.items())
        },
        "performance": {
            "external_wall_seconds": summary(external_wall),
            "effective_cpu_cores": summary(effective_cores),
            "physical_fes_per_external_second": summary(throughput),
            "internal_stage_seconds": {
                name: summary(values)
                for name, values in stages.items()
                if values
            },
        },
    }


def write_atomic(path: Path, document: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + ".tmp")
    with temporary.open("w", encoding="utf-8") as handle:
        json.dump(
            document,
            handle,
            indent=2,
            sort_keys=True,
            allow_nan=False,
        )
        handle.write("\n")
        handle.flush()
        os.fsync(handle.fileno())
    temporary.replace(path)


def main() -> int:
    manifest = json.loads(FINAL_MANIFEST.read_text(encoding="utf-8"))
    validate_manifest(manifest, prepared=False)
    h6 = json.loads(H6_SUMMARY.read_text(encoding="utf-8"))
    h6_by_pair = {row["pair_id"]: row for row in h6["targets"]}
    matrix = by_corpus(read_tsv(PAPER_MATRIX))
    sources = by_corpus(read_tsv(SOURCE_REGISTRY))
    facts = by_corpus(read_tsv(FACT_REGISTRY))
    gaps_by_corpus: dict[str, list[dict[str, str]]] = {}
    for row in read_tsv(GAP_LEDGER):
        gaps_by_corpus.setdefault(row["corpus_id"], []).append(row)

    require(
        set(matrix) == set(sources) == set(facts)
        == {campaign["corpus_id"] for campaign in manifest["campaigns"]},
        "paper/source/fact/manifest coverage drift",
    )

    paper_rows: list[dict[str, Any]] = []
    ready_run_count = 0
    for campaign in manifest["campaigns"]:
        corpus_id = campaign["corpus_id"]
        h6_target = h6_by_pair[campaign["pair_id"]]
        case_summaries: list[dict[str, Any]] = []
        formal_run_count = 0
        if campaign["execution_admission"] == "ready_cpu":
            for case in campaign["cases"]:
                observations = []
                for seed in campaign["optimization_seeds"]:
                    path = result_path(campaign, case, seed)
                    audit_result(manifest, campaign, case, seed)
                    require(path.is_file(), f"formal result absent: {path}")
                    document = json.loads(path.read_text(encoding="utf-8"))
                    require(
                        document.get("status") == "validated_complete",
                        f"formal result incomplete: {path}",
                    )
                    observations.append(document)
                case_summaries.append(
                    case_summary(campaign, case, observations)
                )
                formal_run_count += len(observations)
            ready_run_count += formal_run_count
            formal_status = "complete_25_seed"
        else:
            require(
                campaign["execution_admission"]
                == "validated_deferred_full_training",
                f"{campaign['pair_id']}: unknown execution admission",
            )
            formal_status = "validated_deferred_full_training"

        source = sources[corpus_id]
        fact = facts[corpus_id]
        paper = matrix[corpus_id]
        require(
            source["doi"].lower() == paper["doi"].lower()
            == fact["doi"].lower(),
            f"{corpus_id}: DOI registry drift",
        )
        require(
            campaign["provenance"]["doi"].lower()
            == paper["doi"].lower(),
            f"{corpus_id}: manifest DOI drift",
        )
        require(
            campaign["algorithm_id"] == paper["target_algorithm"].lower()
            or campaign["method_semantic_id"]
            == paper["method_semantic_id"],
            f"{corpus_id}: algorithm identity drift",
        )
        paper_rows.append(
            {
                "corpus_id": corpus_id,
                "doi": paper["doi"],
                "target_algorithm": paper["target_algorithm"],
                "pair_id": campaign["pair_id"],
                "method_semantic_id": campaign["method_semantic_id"],
                "problem_id": campaign["problem_id"],
                "problem_semantic_id": campaign["problem_semantic_id"],
                "paper_protocol_id": campaign["paper_protocol_id"],
                "objective_mode": campaign["objective_mode"],
                "native_asset": campaign["native_asset"],
                "execution_admission": campaign["execution_admission"],
                "formal_status": formal_status,
                "formal_case_count": campaign["case_count"],
                "formal_seed_count": campaign["optimization_seed_count"],
                "formal_run_count": formal_run_count,
                "selected_backend": campaign["backend"],
                "h6": {
                    "status": h6_target["status"],
                    "selected_workers": h6_target["selected_workers"],
                    "fixed_physical_fes": h6_target["fixed_physical_fes"],
                    "minimum_named_h0_stage_attribution":
                        h6_target["minimum_named_h0_stage_attribution"],
                    "worker_statistics": h6_target["worker_statistics"],
                    "analysis_path":
                        h6_target["dependency_proof"]["analysis_path"],
                    "analysis_sha256":
                        h6_target["dependency_proof"]["analysis_sha256"],
                    "claim_boundary": h6_target["claim_boundary"],
                },
                "source_fact_declaration": {
                    "source_authority": source["source_authority"],
                    "source_url": source["source_url"],
                    "revision_or_sha256": source["revision_or_sha256"],
                    "implementation_use": source["implementation_use"],
                    "fact_files": fact["fact_files"].split(";"),
                    "missing_or_conflict_completions":
                        gaps_by_corpus.get(corpus_id, []),
                },
                "training_admission": campaign["training_admission"],
                "case_summaries": case_summaries,
                "claim_boundary": paper["claim_boundary"],
            }
        )

    require(ready_run_count == 27775, "CPU formal run cardinality drift")
    document = {
        "schema_version": 1,
        "bundle_id": "plan005_target_native_public_closure_v1",
        "status": "cpu_formal_complete_learning_resource_deferred",
        "suite_id": manifest["suite_id"],
        "manifest_logical_path": str(FINAL_MANIFEST.relative_to(ROOT)),
        "manifest_sha256": sha256(FINAL_MANIFEST),
        "h6_summary_logical_path": str(H6_SUMMARY.relative_to(ROOT)),
        "h6_summary_sha256": sha256(H6_SUMMARY),
        "academic_reproduction_policy_logical_path": str(
            FACT_POLICY.relative_to(ROOT)
        ),
        "academic_reproduction_policy_sha256": sha256(FACT_POLICY),
        "target_count": 23,
        "ready_cpu_target_count": 20,
        "resource_deferred_learning_target_count": 3,
        "ready_cpu_formal_run_count": ready_run_count,
        "non_target_baselines_in_readiness": 0,
        "papers": paper_rows,
        "claim_boundary": (
            "Twenty CPU-admissible target-native or explicitly named "
            "reconstruction campaigns are complete at 25 seeds. TAAE, "
            "RLPSO, and ALGA retain accepted CPU H6 and executable LibTorch "
            "C++ paths but remain resource-deferred until paper-scale "
            "training artifacts are produced; generic CUDA compatibility is "
            "not promoted to target GPU H6."
        ),
    }
    write_atomic(OUTPUT, document)
    print(
        "plan005_final_bundle_generation_pass "
        "papers=23 ready_cpu=20 deferred_learning=3 "
        f"formal_runs={ready_run_count} output={OUTPUT.relative_to(ROOT)}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
