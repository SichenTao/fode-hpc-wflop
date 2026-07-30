#!/usr/bin/env python3
"""Shared Plan-005 target-native formal-campaign definitions.

This module expands only the 23 target algorithm/problem pairs registered in
``docs/hpc_core_target_pairs.tsv``.  Comparison-only baselines never enter the
manifest or completion count.
"""

from __future__ import annotations

import csv
import hashlib
import json
import re
import shlex
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
REGISTRY = ROOT / "docs/hpc_core_target_pairs.tsv"
SOURCE_REGISTRY = ROOT / "docs/source_asset_registry.tsv"
H6_RAW = (
    ROOT
    / "evidence/performance/"
    "plan005_h6_performance_first_raw_observations_20260730.jsonl"
)
H6_SUMMARY = (
    ROOT
    / "evidence/performance/"
    "plan005_h6_performance_first_summary_20260730.json"
)
PREPARED_MANIFEST = (
    ROOT
    / "formal/plan005/prepared/"
    "plan005_target_native_25_prepared.json"
)
FINAL_MANIFEST = (
    ROOT
    / "formal/plan005/manifests/"
    "plan005_target_native_25_v1.json"
)
RESULT_ROOT = ROOT / "results/plan005_target_native_25_v1"
SEEDS = list(range(2026073101, 2026073126))
LEARNING = {
    "Y36": "taae",
    "T42": "rlpso",
    "T45": "alga",
}
SCALAR_WFLOP = {
    "S01",
    "S02",
    "S03",
    "S04",
    "S05",
    "L0608",
    "T37",
    "T38",
    "T39",
    "T40",
    "T41",
    "T42",
    "T45",
    "T47",
    "Y34",
    "Y35",
}
BINARY_BY_CORPUS = {
    **{corpus: "wflop" for corpus in SCALAR_WFLOP},
    "Y36": "taae",
    "T43": "ppga",
    "T44": "bde",
    "L0726": "geoga",
    "Y06": "gga",
    "T36": "gga",
    "T46": "pbea",
}
OBJECTIVE_MODE = {
    "Y36": "multiobjective",
    "T36": "multiobjective",
    "T46": "multiobjective",
}


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def canonical_sha256(value: Any) -> str:
    return hashlib.sha256(
        json.dumps(
            value,
            sort_keys=True,
            separators=(",", ":"),
            allow_nan=False,
        ).encode("utf-8")
    ).hexdigest()


def relative(path: Path) -> str:
    return str(path.resolve().relative_to(ROOT))


def read_tsv(path: Path) -> list[dict[str, str]]:
    with path.open(encoding="utf-8", newline="") as handle:
        return list(csv.DictReader(handle, delimiter="\t"))


def target_rows() -> list[dict[str, str]]:
    rows = read_tsv(REGISTRY)
    require(len(rows) == 23, f"expected 23 target pairs, found {len(rows)}")
    require(
        all(row["role"] == "target" for row in rows),
        "non-target baseline entered the Plan-005 manifest",
    )
    require(
        len({row["pair_id"] for row in rows}) == 23,
        "duplicate target pair identity",
    )
    return rows


def source_authority() -> dict[str, dict[str, str]]:
    return {row["corpus_id"]: row for row in read_tsv(SOURCE_REGISTRY)}


def h1(row: dict[str, str]) -> dict[str, Any]:
    document = json.loads((ROOT / row["analysis_path"]).read_text())
    return document["H1_work_and_data_movement"]


def physical_fes(row: dict[str, str]) -> int:
    value = h1(row)["actual_values"]["representative"]["FES"]
    require(
        isinstance(value, int) and value > 0,
        f"{row['pair_id']}: invalid paper-native physical FES",
    )
    return value


def representative_case(row: dict[str, str]) -> str:
    value = h1(row)["native_size_provenance"]["selected_native_cases"][
        "representative"
    ]
    require(
        isinstance(value, str) and value,
        f"{row['pair_id']}: representative case absent",
    )
    return value


def case_identifier(value: dict[str, Any]) -> str:
    for field in ("case_id", "id", "name", "case"):
        candidate = value.get(field)
        if isinstance(candidate, str) and candidate:
            return candidate
    raise RuntimeError("case object has no stable identifier")


def case_receipt(
    *,
    case_id: str,
    value: dict[str, Any],
    row: dict[str, str],
    problem_asset: str | None = None,
) -> dict[str, Any]:
    semantic_hash = value.get("case_hash")
    if not isinstance(semantic_hash, str) or len(semantic_hash) != 64:
        semantic_hash = canonical_sha256(value)
    receipt = {
        "case_id": case_id,
        "case_semantic_hash": semantic_hash,
        "physical_fes_per_run": physical_fes(row),
    }
    if problem_asset is not None:
        asset = ROOT / problem_asset
        require(asset.is_file(), f"case asset absent: {problem_asset}")
        receipt["problem_asset"] = problem_asset
        receipt["problem_asset_sha256"] = sha256(asset)
    return receipt


def expand_cases(row: dict[str, str]) -> list[dict[str, Any]]:
    corpus = row["corpus_id"]
    native_path = ROOT / row["native_asset"]
    require(native_path.is_file(), f"{corpus}: native asset absent")
    document = json.loads(native_path.read_text())
    cases: list[dict[str, Any]] = []
    values = document.get("cases") if isinstance(document, dict) else None
    if isinstance(values, list) and values:
        for value in values:
            require(isinstance(value, dict), f"{corpus}: non-object case")
            case_id = case_identifier(value)
            cases.append(case_receipt(case_id=case_id, value=value, row=row))
    elif corpus == "T46":
        for scenario in ("ws1", "ws2"):
            for turbines in range(15, 31):
                case_id = f"{scenario}_tn{turbines}"
                value = {
                    "scenario": scenario,
                    "turbine_count": turbines,
                    "population_size": document["population_size"],
                    "generation_count": document["generation_count"],
                    "problem_semantics_id": document["problem_semantics_id"],
                }
                cases.append(
                    case_receipt(case_id=case_id, value=value, row=row)
                )
    elif corpus == "Y06":
        for case_id in document["case_ids"]:
            asset = (
                ".source-cache/generated/gga_repaired/"
                f"{case_id}.wfp"
            )
            cases.append(
                case_receipt(
                    case_id=case_id,
                    value={
                        "case_id": case_id,
                        "problem_semantics_id": row["problem_semantic_id"],
                    },
                    row=row,
                    problem_asset=asset,
                )
            )
    elif corpus == "T36":
        case_id = representative_case(row)
        asset = f".source-cache/generated/gga_repaired/{case_id}.wfp"
        cases.append(
            case_receipt(
                case_id=case_id,
                value={
                    "case_id": case_id,
                    "problem_semantics_sha256": document[
                        "problem_semantics_sha256"
                    ],
                },
                row=row,
                problem_asset=asset,
            )
        )
    else:
        case_id = document.get("case_id", representative_case(row))
        require(isinstance(case_id, str) and case_id, f"{corpus}: case absent")
        cases.append(
            case_receipt(case_id=case_id, value=document, row=row)
        )
    require(
        len({item["case_id"] for item in cases}) == len(cases),
        f"{corpus}: duplicate native case identifier",
    )
    return cases


def read_h6_header() -> dict[str, Any]:
    require(H6_RAW.is_file(), "H6 raw campaign is absent")
    first = H6_RAW.read_text(encoding="utf-8").splitlines()[0]
    header = json.loads(first)
    require(
        header.get("record_type") == "campaign_header",
        "H6 campaign header is invalid",
    )
    return header


def read_h6_summary() -> dict[str, Any]:
    require(H6_SUMMARY.is_file(), "accepted H6 summary is absent")
    summary = json.loads(H6_SUMMARY.read_text())
    require(
        summary.get("status") == "accepted_h6"
        and summary.get("target_count") == 23
        and summary.get("observation_count") == 805,
        "H6 summary is not accepted and complete",
    )
    return summary


def result_key_fields() -> list[str]:
    return [
        "suite_id",
        "pair_id",
        "method_semantic_id",
        "problem_semantic_id",
        "case_semantic_hash",
        "optimization_seed",
        "physical_fes_per_run",
        "binary_sha256",
        "environment_sha256",
        "source_commit",
    ]


def learning_admission(
    row: dict[str, str],
    header: dict[str, Any],
) -> dict[str, Any]:
    method = LEARNING.get(row["corpus_id"])
    if method is None:
        return {
            "status": "ready_cpu",
            "reason": "no separate paper-scale learned artifact is required",
        }
    bounded = header["learning_artifacts"][method]
    return {
        "status": "validated_deferred_full_training",
        "reason": (
            "bounded CPU artifact and optimizer path are validated; "
            "paper-scale learned-state production remains a separate "
            "resource-gated campaign"
        ),
        "bounded_h6_artifact": bounded,
        "formal_training_seed_namespace": f"training/{row['pair_id']}",
        "optimization_seed_namespace": f"optimization/{row['pair_id']}",
        "namespaces_disjoint": True,
        "resume_requires": [
            "paper-scale artifact path",
            "artifact SHA-256",
            "training command",
            "backend and device receipt",
            "training-work ledger",
        ],
    }


def campaign_record(
    row: dict[str, str],
    *,
    header: dict[str, Any],
    target_h6: dict[str, Any] | None,
    authority: dict[str, dict[str, str]],
) -> dict[str, Any]:
    binary_name = BINARY_BY_CORPUS[row["corpus_id"]]
    binary = header["binaries"][binary_name]
    cases = expand_cases(row)
    admission = learning_admission(row, header)
    selected_workers = (
        target_h6["selected_workers"] if target_h6 is not None else None
    )
    selected_affinity = (
        header["environment"]["worker_affinity_sets"][str(selected_workers)]
        if selected_workers is not None
        else None
    )
    source = authority.get(row["corpus_id"], {})
    record = {
        "campaign_id": f"plan005_{row['corpus_id'].lower()}_native_25_v1",
        "pair_id": row["pair_id"],
        "corpus_id": row["corpus_id"],
        "algorithm_id": row["algorithm_id"],
        "method_semantic_id": row["method_semantic_id"],
        "problem_id": row["problem_id"],
        "problem_semantic_id": row["problem_semantic_id"],
        "paper_protocol_id": row["paper_protocol_id"],
        "native_asset": row["native_asset"],
        "native_asset_sha256": sha256(ROOT / row["native_asset"]),
        "analysis_path": row["analysis_path"],
        "analysis_sha256": sha256(ROOT / row["analysis_path"]),
        "provenance": {
            key: source.get(key, "")
            for key in (
                "doi",
                "source_authority",
                "source_url",
                "revision_or_sha256",
                "license_observation",
                "redistribution_policy",
                "implementation_use",
            )
        },
        "backend": {
            "backend_id": "cpu_hpc_v1",
            "binary_name": binary_name,
            "binary_logical_path": binary["logical_path"],
            "binary_sha256": binary["sha256"],
            "selected_workers": selected_workers,
            "selected_affinity_cpus": selected_affinity,
            "backend_parallelism": 1,
            "selection_status": (
                "accepted_h6" if target_h6 is not None else "waiting_h6"
            ),
        },
        "training_admission": admission,
        "objective_mode": OBJECTIVE_MODE.get(
            row["corpus_id"], "single_objective"
        ),
        "quality_reporting": {
            "cross_problem_pooling": False,
            "mode": (
                "descriptive_empirical_front"
                if row["corpus_id"] in OBJECTIVE_MODE
                else "raw_objective_per_exact_case"
            ),
            "claim_boundary": (
                "target algorithm on its registered paper-native or explicitly "
                "declared proxy problem; no comparison-only baseline claim"
            ),
        },
        "cases": cases,
        "case_count": len(cases),
        "optimization_seeds": SEEDS,
        "optimization_seed_count": len(SEEDS),
        "optimization_run_count": len(cases) * len(SEEDS),
        "result_key_fields": result_key_fields(),
        "resume": {
            "reuse_only_complete_validated_results": True,
            "partial_results_never_reused": True,
            "atomic_commit": (
                "same-filesystem temporary file, flush, fsync, rename"
            ),
        },
        "execution_admission": admission["status"],
    }
    record["command_contract"] = formal_command_contract(record)
    return record


def build_manifest(*, prepared: bool) -> dict[str, Any]:
    rows = target_rows()
    header = read_h6_header()
    summary = None if prepared else read_h6_summary()
    targets = (
        {}
        if summary is None
        else {item["pair_id"]: item for item in summary["targets"]}
    )
    if not prepared:
        require(
            set(targets) == {row["pair_id"] for row in rows},
            "H6 target coverage differs from registry",
        )
    authority = source_authority()
    campaigns = [
        campaign_record(
            row,
            header=header,
            target_h6=targets.get(row["pair_id"]),
            authority=authority,
        )
        for row in rows
    ]
    return {
        "schema_version": 1,
        "suite_id": "plan005_target_native_25_v1",
        "status": (
            "prepared_waiting_h6" if prepared else "frozen_ready"
        ),
        "source_commit": header["source_commit"],
        "h6": {
            "campaign_id": header["campaign_id"],
            "raw_logical_path": relative(H6_RAW),
            "header_sha256": canonical_sha256(header),
            "summary_logical_path": (
                None if prepared else relative(H6_SUMMARY)
            ),
            "summary_sha256": (
                None if prepared else sha256(H6_SUMMARY)
            ),
        },
        "environment_sha256": header["environment"]["sha256"],
        "measurement_order_policy": "formal campaigns are sequential",
        "backend_parallelism": 1,
        "optimization_seeds": SEEDS,
        "target_count": len(campaigns),
        "case_count": sum(item["case_count"] for item in campaigns),
        "optimization_run_count": sum(
            item["optimization_run_count"] for item in campaigns
        ),
        "ready_cpu_target_count": sum(
            item["execution_admission"] == "ready_cpu"
            for item in campaigns
        ),
        "deferred_learning_target_count": sum(
            item["execution_admission"]
            == "validated_deferred_full_training"
            for item in campaigns
        ),
        "non_target_baselines_in_readiness": 0,
        "campaigns": campaigns,
    }


def validate_manifest(document: dict[str, Any], *, prepared: bool) -> None:
    require(document["schema_version"] == 1, "manifest schema drift")
    require(document["target_count"] == 23, "target count drift")
    require(
        document["non_target_baselines_in_readiness"] == 0,
        "comparison baseline entered readiness",
    )
    require(
        document["optimization_seeds"] == SEEDS
        and len(set(SEEDS)) == 25,
        "optimization seed contract drift",
    )
    campaigns = document["campaigns"]
    require(len(campaigns) == 23, "campaign cardinality drift")
    require(
        len({item["pair_id"] for item in campaigns}) == 23,
        "duplicate target campaign",
    )
    require(
        document["case_count"]
        == sum(item["case_count"] for item in campaigns),
        "case count is not derived",
    )
    require(
        document["optimization_run_count"]
        == sum(item["optimization_run_count"] for item in campaigns),
        "optimization run count is not derived",
    )
    for campaign in campaigns:
        require(
            campaign["case_count"] == len(campaign["cases"])
            and campaign["optimization_seed_count"] == 25
            and campaign["optimization_seeds"] == SEEDS
            and campaign["optimization_run_count"]
            == campaign["case_count"] * 25,
            f"{campaign['pair_id']}: run cardinality drift",
        )
        require(
            len({item["case_id"] for item in campaign["cases"]})
            == campaign["case_count"],
            f"{campaign['pair_id']}: duplicate case",
        )
        require(
            all(
                len(item["case_semantic_hash"]) == 64
                and item["physical_fes_per_run"] > 0
                for item in campaign["cases"]
            ),
            f"{campaign['pair_id']}: invalid case receipt",
        )
        backend = campaign["backend"]
        if prepared:
            require(
                backend["selected_workers"] is None
                and backend["selection_status"] == "waiting_h6",
                f"{campaign['pair_id']}: prepared manifest invented H6",
            )
        else:
            require(
                isinstance(backend["selected_workers"], int)
                and backend["selected_workers"] > 0
                and backend["selection_status"] == "accepted_h6"
                and len(backend["selected_affinity_cpus"])
                == backend["selected_workers"],
                f"{campaign['pair_id']}: final H6 backend absent",
            )
    require(
        document["deferred_learning_target_count"] == 3
        and document["ready_cpu_target_count"] == 20,
        "learning-resource admission count drift",
    )


def parse_pbea_case(case_id: str) -> tuple[str, int]:
    match = re.fullmatch(r"(ws[12])_tn(\d+)", case_id)
    require(match is not None, f"invalid PBEA case: {case_id}")
    return match.group(1), int(match.group(2))


def safe_component(value: str) -> str:
    """Return a reversible-enough, traversal-safe result-path component."""

    sanitized = re.sub(r"[^A-Za-z0-9._-]+", "_", value).strip("._")
    require(
        bool(sanitized) and sanitized not in {".", ".."},
        f"unsafe empty path component derived from {value!r}",
    )
    suffix = hashlib.sha256(value.encode("utf-8")).hexdigest()[:12]
    return f"{sanitized[:80]}--{suffix}"


def result_path(
    campaign: dict[str, Any],
    case: dict[str, Any],
    seed: int,
) -> Path:
    return (
        RESULT_ROOT
        / "runs"
        / safe_component(campaign["pair_id"])
        / safe_component(case["case_id"])
        / f"seed-{seed}.json"
    )


def result_key(
    manifest: dict[str, Any],
    campaign: dict[str, Any],
    case: dict[str, Any],
    seed: int,
) -> dict[str, Any]:
    return {
        "suite_id": manifest["suite_id"],
        "pair_id": campaign["pair_id"],
        "method_semantic_id": campaign["method_semantic_id"],
        "problem_semantic_id": campaign["problem_semantic_id"],
        "case_semantic_hash": case["case_semantic_hash"],
        "optimization_seed": seed,
        "physical_fes_per_run": case["physical_fes_per_run"],
        "binary_sha256": campaign["backend"]["binary_sha256"],
        "environment_sha256": manifest["environment_sha256"],
        "source_commit": manifest["source_commit"],
    }


def formal_command_contract(campaign: dict[str, Any]) -> dict[str, Any]:
    """Describe the exact argument mapping without inventing final H6 values."""

    corpus = campaign["corpus_id"]
    contract = {
        "generator": "scripts/plan005_formal_common.py::formal_command",
        "seed_token": "{optimization_seed}",
        "worker_token": "{selected_workers}",
        "case_token": "{case_id}",
        "physical_fes_token": "{physical_fes_per_run}",
        "front_token": (
            "{front_path}" if campaign["objective_mode"] == "multiobjective"
            else None
        ),
    }
    if corpus == "T46":
        contract["physical_fes_mapping"] = (
            "population * (generations + 1), population=100"
        )
    else:
        contract["physical_fes_mapping"] = "--physical-fes"
    return contract


def formal_command(
    campaign: dict[str, Any],
    case: dict[str, Any],
    *,
    seed: int,
    front_path: Path | None,
) -> list[str]:
    """Instantiate one paper-native optimization command from the manifest."""

    backend = campaign["backend"]
    workers = backend["selected_workers"]
    require(
        isinstance(workers, int) and workers > 0,
        f"{campaign['pair_id']}: final H6 worker selection is absent",
    )
    binary = backend["binary_logical_path"]
    corpus = campaign["corpus_id"]
    physical_fes = case["physical_fes_per_run"]
    if corpus in SCALAR_WFLOP:
        command = [
            binary,
            "--algorithm",
            campaign["algorithm_id"],
            "--problem",
            campaign["problem_id"],
            "--cases",
            campaign["native_asset"],
            "--case",
            case["case_id"],
            "--paper-protocol",
            campaign["paper_protocol_id"],
            "--physical-fes",
            str(physical_fes),
            "--seed",
            str(seed),
            "--workers",
            str(workers),
            "--compute-backend",
            "cpu",
        ]
        if corpus == "S04":
            command.extend(
                ["--rlfode-models", "shared/models/fqfode_seeded"]
            )
        if corpus in {"T42", "T45"}:
            artifact = campaign["training_admission"][
                "bounded_h6_artifact"
            ]["artifact_logical_path"]
            command.extend(
                [
                    "--training-artifact",
                    artifact,
                    "--torch-intraop-threads",
                    str(workers),
                    "--torch-interop-threads",
                    "1",
                ]
            )
        return command
    if corpus == "Y36":
        artifact = campaign["training_admission"]["bounded_h6_artifact"][
            "artifact_logical_path"
        ]
        return [
            binary,
            "--cases",
            campaign["native_asset"],
            "--case",
            case["case_id"],
            "--profile",
            "bounded",
            "--physical-fes",
            str(physical_fes),
            "--seed",
            str(seed),
            "--workers",
            str(workers),
            "--backend",
            "cpu",
            "--learning-artifact",
            artifact,
            "--torch-intraop-threads",
            str(workers),
            "--torch-interop-threads",
            "1",
        ]
    if corpus == "T43":
        return [
            binary,
            "--cases",
            campaign["native_asset"],
            "--case",
            case["case_id"],
            "--seed",
            str(seed),
            "--physical-fes",
            str(physical_fes),
            "--workers",
            str(workers),
            "--backend",
            "cpu",
        ]
    if corpus == "T44":
        return [
            binary,
            "--cases",
            campaign["native_asset"],
            "--case",
            case["case_id"],
            "--seed",
            str(seed),
            "--physical-fes",
            str(physical_fes),
            "--workers",
            str(workers),
            "--execution-mode",
            "cpu",
        ]
    if corpus == "L0726":
        return [
            binary,
            "--case",
            campaign["native_asset"],
            "--seed",
            str(seed),
            "--physical-fes",
            str(physical_fes),
            "--workers",
            str(workers),
            "--backend",
            "cpu",
        ]
    if corpus in {"Y06", "T36"}:
        problem_asset = case.get(
            "problem_asset",
            ".source-cache/generated/gga_repaired/Denmark_Nysted.wfp",
        )
        command = [
            binary,
            "--problem",
            problem_asset,
            "--physical-fes",
            str(physical_fes),
            "--workers",
            str(workers),
            "--seed",
            str(seed),
            "--algorithm",
            "gga" if corpus == "Y06" else "tmoea",
            "--execution-mode",
            "cpu",
        ]
        if corpus == "T36":
            command.extend(["--tmoea-profile", "paper-eq16-v2"])
        return command
    if corpus == "T46":
        require(front_path is not None, "PBEA front path is required")
        scenario, turbines = parse_pbea_case(case["case_id"])
        population = 100
        require(
            physical_fes >= population
            and physical_fes % population == 0,
            "PBEA physical FES cannot map to population/generations",
        )
        generations = physical_fes // population - 1
        return [
            binary,
            "--algorithm",
            "moead_p",
            "--scenario",
            scenario,
            "--turbines",
            str(turbines),
            "--population",
            str(population),
            "--generations",
            str(generations),
            "--workers",
            str(workers),
            "--seed",
            str(seed),
            "--execution-mode",
            "cpu",
            "--output-front",
            str(front_path),
        ]
    raise RuntimeError(f"{corpus}: no formal command route")


def shell_display(command: list[str]) -> str:
    return shlex.join(command)
