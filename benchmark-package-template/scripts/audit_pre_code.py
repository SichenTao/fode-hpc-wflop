#!/usr/bin/env python3
"""Fail-closed pre-code audit for one copied benchmark package."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
from typing import Any


PACKAGE_ROOT = Path(__file__).resolve().parents[1]
M_TIERS = {
    "M0_AUTHOR_SOURCE",
    "M1_PAPER_COMPLETE",
    "M2_CITATION_PREDECESSOR",
    "M3_DECLARED_COMPLETION",
    "M4_FORMULA_FIXTURE",
}
P_TIERS = {
    "P0_AUTHOR_ASSET",
    "P1_PAPER_COMPLETE",
    "P2_CITATION_SAME_AUTHOR",
    "P3_DECLARED_PROXY",
    "P4_FORMULA_FIXTURE",
}


def load_json(relative_path: str) -> dict[str, Any]:
    return json.loads((PACKAGE_ROOT / relative_path).read_text(encoding="utf-8"))


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def safe_package_file(relative_path: str) -> Path:
    candidate_path = Path(relative_path)
    require(
        not candidate_path.is_absolute(),
        f"package path must be relative: {relative_path}",
    )
    candidate = (PACKAGE_ROOT / candidate_path).resolve()
    root = PACKAGE_ROOT.resolve()
    require(
        candidate == root or root in candidate.parents,
        f"package path escapes package root: {relative_path}",
    )
    require(candidate.is_file(), f"package file missing: {relative_path}")
    return candidate


def find_placeholders(value: Any, path: str = "$") -> list[str]:
    found: list[str] = []
    if isinstance(value, dict):
        for key, item in value.items():
            found.extend(find_placeholders(item, f"{path}.{key}"))
    elif isinstance(value, list):
        for index, item in enumerate(value):
            found.extend(find_placeholders(item, f"{path}[{index}]"))
    elif isinstance(value, str):
        if "REQUIRED_" in value or value == "template_unresolved":
            found.append(path)
    return found


def audit(*, template_mode: bool) -> None:
    manifest = load_json("template_manifest.json")
    require(
        manifest["numbered_element_count"] == 16,
        "template must freeze exactly sixteen numbered elements",
    )
    numbers = [row["number"] for row in manifest["numbered_elements"]]
    require(numbers == list(range(1, 17)), "element numbers must be 1 through 16")
    for element in manifest["numbered_elements"]:
        for relative_path in element["paths"]:
            require(
                (PACKAGE_ROOT / relative_path).is_file(),
                f"required package artifact missing: {relative_path}",
            )

    identity = load_json("metadata/paper_source_identity.json")
    search = load_json("metadata/source_search_evidence.json")
    search_log = load_json("metadata/source_search_log.json")
    tiers = load_json("schemas/evidence_tiers.schema.json")
    lifecycle = load_json("metadata/r0_r4_checklist.json")
    decisions = load_json("metadata/uncertainty_decision_ledger.json")
    learned = load_json("metadata/learned_state_training_contract.json")
    problem = load_json("metadata/problem_physical_fes_contract.json")
    random_events = load_json(
        "metadata/deterministic_random_event_contract.json"
    )
    backends = load_json("metadata/backend_capabilities.json")
    boundary = load_json("metadata/claim_boundary_non_pooling.json")
    fixed = load_json("fixtures/fixed_layout_fixture.json")
    oracle = load_json("fixtures/scalar_oracle.json")

    require(
        set(tiers["method_evidence_tiers"]) == M_TIERS,
        "method evidence-tier schema differs",
    )
    require(
        set(tiers["problem_evidence_tiers"]) == P_TIERS,
        "problem evidence-tier schema differs",
    )
    require(
        [row["rank"] for row in search["permission_ladder"]]
        == [1, 2, 3, 4, 5],
        "authority search must preserve all five permission-ladder ranks",
    )
    require(
        {row["authority_rank"] for row in search_log["entries"]}
        == {1, 2, 3, 4, 5},
        "source-search log must cover every authority rank",
    )
    require(
        search["reconstruction_permission"]["missing_fields_only"] is True,
        "reconstruction must be limited to fields absent after source search",
    )
    require(
        decisions["silent_defaults_forbidden"] is True,
        "silent reconstruction defaults must remain forbidden",
    )
    for decision in decisions["decisions"]:
        require(
            decision["sensitivity_test_required"] is True,
            "every inferred decision requires a sensitivity obligation",
        )

    require(
        learned["random_seeds"]["disjoint_from_optimization_namespace"] is True,
        "training and optimization random namespaces must be disjoint",
    )
    require(
        problem["physical_fes"]["overshoot"] == "forbidden"
        and problem["physical_fes"]["initialization_included"] is True,
        "physical-FES contract must include initialization and forbid overshoot",
    )
    require(
        random_events["schedule_independence"] is True,
        "random-event contract must be schedule independent",
    )
    require(
        backends["backends"]["cpu"]["supported"] is True,
        "pure C++ CPU capability is required",
    )
    for backend in ("hybrid", "gpu"):
        record = backends["backends"][backend]
        require(
            record["supported"] is True or record["status"] == "fails_closed",
            f"{backend} must be admitted or fail closed",
        )
    require(
        boundary["pooling_key"]
        == [
            "method_semantics_id",
            "problem_semantics_id",
            "case_semantic_hash",
            "objective_signature",
            "physical_fes_contract_id",
        ],
        "non-pooling key differs",
    )
    require(
        fixed["fixture_id"] == oracle["fixture_id"],
        "scalar oracle and fixed-layout fixture identities differ",
    )

    if template_mode:
        return

    audited_documents = {
        "identity": identity,
        "search": search,
        "search_log": search_log,
        "lifecycle": lifecycle,
        "decisions": decisions,
        "learned": learned,
        "problem": problem,
        "random_events": random_events,
        "backends": backends,
        "boundary": boundary,
        "fixed": fixed,
        "oracle": oracle,
    }
    unresolved = {
        name: find_placeholders(document)
        for name, document in audited_documents.items()
    }
    unresolved = {name: paths for name, paths in unresolved.items() if paths}
    require(not unresolved, f"unresolved pre-code fields: {unresolved}")
    require(
        identity["identity_status"] == "frozen",
        "paper/source identity must be frozen before coding",
    )
    require(
        all(
            row["status"] in {"searched_found", "searched_not_found"}
            and row["evidence"]
            for row in search["permission_ladder"]
        ),
        "all five authority ranks require completed status and evidence",
    )
    negative = search["bounded_negative_evidence"]
    query_log = safe_package_file(negative["query_log_path"])
    require(
        search_log["log_id"] == negative["query_log_id"],
        "source-search evidence and query-log IDs differ",
    )
    require(
        sha256_file(query_log) == negative["query_log_sha256"],
        "source-search query-log SHA256 differs",
    )
    for ladder_row in search["permission_ladder"]:
        rank_entries = [
            row
            for row in search_log["entries"]
            if row["authority_rank"] == ladder_row["rank"]
        ]
        require(rank_entries, "source-search rank has no logged query")
        require(
            all(
                row["status"] in {"searched_found", "searched_not_found"}
                and row["query"]
                and row["url"]
                and row["searched_at_utc"]
                and row["immutable_receipt"]["kind"]
                and row["immutable_receipt"]["value"]
                for row in rank_entries
            ),
            "source-search log entry lacks status, query, URL, date, or receipt",
        )
        require(
            any(
                row["status"] == ladder_row["status"]
                and row["url"] in ladder_row["evidence"]
                for row in rank_entries
            ),
            "source-search evidence row is not backed by its logged query",
        )
    unavailable = search["bounded_negative_evidence"]["unavailable_fields"]
    require(
        search["reconstruction_permission"]["allowed"] is bool(unavailable),
        "reconstruction permission must match the bounded missing-field set",
    )
    require(
        lifecycle["gates"]["R0"]["status"] == "pass"
        and lifecycle["gates"]["R1"]["status"] == "pass"
        and lifecycle["gates"]["R2"]["status"] == "pass",
        "R0, R1, and R2 must pass before implementation starts",
    )
    require(
        boundary["method_evidence_tier"] in M_TIERS
        and boundary["problem_evidence_tier"] in P_TIERS,
        "profile evidence tiers are invalid",
    )
    require(
        boundary["method_semantics_id"] != boundary["problem_semantics_id"],
        "method and problem semantic identifiers must remain distinct",
    )
    for asset in problem["assets"]:
        asset_path = safe_package_file(asset["relative_path"])
        require(
            sha256_file(asset_path) == asset["sha256"],
            f"problem asset SHA256 differs: {asset['asset_id']}",
        )


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--template-mode",
        action="store_true",
        help="validate the copy-ready template while allowing REQUIRED placeholders",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    audit(template_mode=args.template_mode)
    print(
        "benchmark_package_pre_code_audit_pass "
        f"mode={'template' if args.template_mode else 'instantiated'}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
