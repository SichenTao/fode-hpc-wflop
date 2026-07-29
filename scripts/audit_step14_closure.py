#!/usr/bin/env python3
"""Audit Step-14 generated inventory and closure artifacts."""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import re
import subprocess
import sys
from collections import Counter
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
SUMMARY = ROOT / "evidence/closure/step14_project_summary.json"
RECEIPT = ROOT / "evidence/closure/step14_closure_receipt.json"
SOURCE_BASELINE_COMMIT = "a1061177342c4d26df7746bb6008f760503302d1"
PRESERVATION_BASELINE_COMMIT = "3237c300b059f1e5ba7a07a4afbebc87059e78ca"


def read_json(relative_path: str) -> dict[str, Any]:
    with (ROOT / relative_path).open(encoding="utf-8") as handle:
        return json.load(handle)


def fail(message: str) -> None:
    raise SystemExit(f"step14_closure_audit_failed: {message}")


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def assert_equal(actual: Any, expected: Any, label: str) -> None:
    if actual != expected:
        fail(f"{label}: expected {expected!r}, found {actual!r}")


def audit_summary() -> dict[str, Any]:
    if not SUMMARY.is_file():
        fail("missing evidence/closure/step14_project_summary.json")

    subprocess.run(
        [sys.executable, "scripts/generate_step14_closure.py", "--check"],
        cwd=ROOT,
        check=True,
    )

    summary = json.loads(SUMMARY.read_text(encoding="utf-8"))
    assert_equal(summary["source_baseline_commit"], SOURCE_BASELINE_COMMIT, "baseline")
    assert_equal(summary["formal_campaign_launched_by_step14"], False, "launch state")

    with (ROOT / "docs/author_lineage_registry.tsv").open(
        encoding="utf-8", newline=""
    ) as handle:
        lineage = list(csv.DictReader(handle, delimiter="\t"))
    dossiers = sorted((ROOT / "docs/source-dossiers").glob("*.json"))
    assert_equal(summary["snapshot"]["paper_count"], len(lineage), "paper count")
    assert_equal(
        summary["snapshot"]["unique_doi_count"],
        len({row["doi"] for row in lineage}),
        "unique DOI count",
    )
    assert_equal(
        summary["snapshot"]["source_dossier_count"], len(dossiers), "dossier count"
    )

    schema = read_json("shared/contracts/evidence_authority_schema.json")
    registry = read_json("shared/contracts/executable_profile_evidence.json")
    matrix = read_json("shared/contracts/global_execution_capability_matrix.json")
    profiles = registry["profiles"]
    profile_by_id = {item["profile_id"]: item for item in profiles}
    binding_by_id = {
        item["profile_id"]: item for item in matrix["profile_bindings"]
    }
    assert_equal(set(profile_by_id), set(binding_by_id), "profile-binding identity set")

    method_counts = Counter(item["method_evidence_tier"] for item in profiles)
    problem_counts = Counter(item["problem_evidence_tier"] for item in profiles)
    observed_method = {
        item["tier"]: item["count"]
        for item in summary["evidence_dimensions"]["method"]
    }
    observed_problem = {
        item["tier"]: item["count"]
        for item in summary["evidence_dimensions"]["problem"]
    }
    expected_method = {
        tier: method_counts.get(tier, 0)
        for tier in schema["method_evidence_tiers"]
    }
    expected_problem = {
        tier: problem_counts.get(tier, 0)
        for tier in schema["problem_evidence_tiers"]
    }
    assert_equal(observed_method, expected_method, "method evidence counts")
    assert_equal(observed_problem, expected_problem, "problem evidence counts")
    assert_equal(
        summary["evidence_dimensions"]["non_additive"],
        True,
        "evidence dimensions non-additive",
    )
    expected_facets = {
        "original": sum(
            item["method_evidence_tier"] == "M0_AUTHOR_SOURCE"
            and item["problem_evidence_tier"] == "P0_AUTHOR_ASSET"
            for item in profiles
        ),
        "source-replay": sum(
            "source_replay" in item["profile_id"]
            or "source_replay" in item["problem_id"]
            for item in profiles
        ),
        "paper-complete": sum(
            item["method_evidence_tier"] == "M1_PAPER_COMPLETE"
            or item["problem_evidence_tier"] == "P1_PAPER_COMPLETE"
            for item in profiles
        ),
        "citation-derived": sum(
            item["method_evidence_tier"] == "M2_CITATION_PREDECESSOR"
            or item["problem_evidence_tier"] == "P2_CITATION_SAME_AUTHOR"
            for item in profiles
        ),
        "declared-proxy": sum(
            item["method_evidence_tier"] == "M3_DECLARED_COMPLETION"
            or item["problem_evidence_tier"] == "P3_DECLARED_PROXY"
            for item in profiles
        ),
        "fixture-only": sum(
            item["method_evidence_tier"] == "M4_FORMULA_FIXTURE"
            or item["problem_evidence_tier"] == "P4_FORMULA_FIXTURE"
            for item in profiles
        ),
    }
    observed_facets = {
        item["facet"]: item["count"]
        for item in summary["evidence_dimensions"]["execution_facets"]
    }
    assert_equal(observed_facets, expected_facets, "execution facet counts")

    inventory = summary["executable_inventory"]
    assert_equal(inventory["profile_pair_count"], len(profiles), "profile pair count")
    assert_equal(
        inventory["algorithm_count"],
        len({item["algorithm_id"] for item in profiles}),
        "algorithm count",
    )
    assert_equal(
        inventory["problem_count"],
        len({item["problem_id"] for item in profiles}),
        "problem count",
    )
    assert_equal(
        [item["profile_id"] for item in inventory["pairs"]],
        [item["profile_id"] for item in profiles],
        "profile pair order/list",
    )

    expected_guards = sorted(
        matrix["blocked_original_identities"],
        key=lambda item: item["guarded_algorithm_id"],
    )
    assert_equal(
        summary["identity_guards"]["guarded_originals"],
        expected_guards,
        "guarded original identities",
    )
    for guard in expected_guards:
        reconstruction_id = guard["reconstruction_profile_id"]
        if reconstruction_id not in profile_by_id:
            fail(f"guard reconstruction profile is not executable: {reconstruction_id}")
        if reconstruction_id == guard["guarded_algorithm_id"]:
            fail(f"guarded original was reused as reconstruction: {reconstruction_id}")

    expected_learning = []
    for profile_id, binding in binding_by_id.items():
        if binding.get("admission_status") != "development_admitted_reconstruction":
            continue
        if binding.get("training_state", "not_applicable").startswith("not_applicable"):
            continue
        expected_learning.append(profile_id)
    expected_learning.sort()
    observed_learning = [
        item["profile_id"]
        for item in summary["learning_reconstructions"]["profiles"]
    ]
    assert_equal(observed_learning, expected_learning, "learning reconstruction profiles")
    for item in summary["learning_reconstructions"]["profiles"]:
        if not item["claim_boundary"]:
            fail(f"empty learning claim boundary: {item['profile_id']}")

    expected_complete = [
        item["profile_id"]
        for item in profiles
        if item["method_evidence_tier"]
        in {"M0_AUTHOR_SOURCE", "M1_PAPER_COMPLETE"}
        and item["problem_evidence_tier"]
        in {"P0_AUTHOR_ASSET", "P1_PAPER_COMPLETE"}
    ]
    preservation = summary["complete_information_identity_preservation"]
    assert_equal(
        preservation["profile_ids"],
        expected_complete,
        "complete-information identity list",
    )
    assert_equal(
        preservation["profile_count"],
        len(expected_complete),
        "complete-information identity count",
    )

    suite_files = sorted((ROOT / "formal/contracts").glob("*suite*.json"))
    expected_suites = []
    for path in suite_files:
        data = json.loads(path.read_text(encoding="utf-8"))
        expected_suites.append(
            (path.relative_to(ROOT).as_posix(), data["suite_id"], data["status"])
        )
    observed_suites = [
        (item["path"], item["suite_id"], item["status"])
        for item in summary["suite_manifests"]
    ]
    assert_equal(observed_suites, expected_suites, "suite manifest status list")
    return summary


def expected_file_records(paths: list[Path]) -> list[dict[str, str]]:
    return [
        {"path": path.relative_to(ROOT).as_posix(), "sha256": sha256(path)}
        for path in sorted(paths)
    ]


def audit_receipt(summary: dict[str, Any]) -> dict[str, Any]:
    if not RECEIPT.is_file():
        fail("missing evidence/closure/step14_closure_receipt.json")
    receipt = json.loads(RECEIPT.read_text(encoding="utf-8"))
    assert_equal(receipt["receipt_id"], "step14_closure_receipt_v1", "receipt ID")
    assert_equal(
        receipt["source_baseline_commit"], SOURCE_BASELINE_COMMIT, "receipt baseline"
    )
    for prohibited_key in {"self_hash", "self_reference_commit", "commit"}:
        if prohibited_key in receipt:
            fail(f"receipt contains prohibited self-reference key: {prohibited_key}")

    scope_paths = [
        ROOT / "docs/lineage_scope_contract.json",
        ROOT / "docs/author_lineage_registry.tsv",
        ROOT / "docs/problem_package_registry.tsv",
        ROOT / "docs/lineage_r0_r4_completion.tsv",
    ]
    assert_equal(
        receipt["scope"]["registry_files"],
        expected_file_records(scope_paths),
        "scope registry hashes",
    )
    assert_equal(
        receipt["source_dossiers"],
        expected_file_records(
            list((ROOT / "docs/source-dossiers").glob("*.json"))
        ),
        "source dossier hashes",
    )
    assert_equal(
        receipt["evidence_schema"],
        expected_file_records(
            [ROOT / "shared/contracts/evidence_authority_schema.json"]
        ),
        "evidence schema hash",
    )
    authority_paths = [
        ROOT / "docs/lineage_scope_contract.json",
        ROOT / "docs/author_lineage_registry.tsv",
        ROOT / "shared/contracts/evidence_authority_schema.json",
        ROOT / "shared/contracts/executable_profile_evidence.json",
        ROOT / "shared/contracts/global_execution_capability_matrix.json",
        ROOT / "formal/contracts/declared_reconstruction_formal_suite_v1.json",
    ]
    assert_equal(
        receipt["authority_registries"],
        expected_file_records(authority_paths),
        "authority registry hashes",
    )

    decision_paths = list(
        (ROOT / "shared/contracts/reconstruction-decisions").glob("*.json")
    ) + [
        ROOT / "docs/semantic_discrepancy_ledger.tsv",
        ROOT / "shared/contracts/paper_implementation_ledger.tsv",
        ROOT / "shared/contracts/reconstruction_admission_policy.json",
        ROOT / "shared/contracts/uncertainty_decisions.md",
    ]
    assert_equal(
        receipt["decision_ledgers"],
        expected_file_records(decision_paths),
        "decision ledger hashes",
    )
    semantic_paths: set[Path] = {
        *list((ROOT / "shared/contracts").glob("*contract*.json")),
        *list((ROOT / "shared/contracts").glob("*semantic*.json")),
        *list((ROOT / "shared/contracts").glob("*semantics*.json")),
    }
    matrix = read_json("shared/contracts/global_execution_capability_matrix.json")
    matrix_references: set[Path] = set()

    def visit_contract_references(value: Any) -> None:
        if isinstance(value, dict):
            for child in value.values():
                visit_contract_references(child)
        elif isinstance(value, list):
            for child in value:
                visit_contract_references(child)
        elif isinstance(value, str):
            for relative in re.findall(
                r"shared/contracts/[A-Za-z0-9_.-]+\.json", value
            ):
                path = ROOT / relative
                if path.is_file():
                    matrix_references.add(path)

    visit_contract_references(matrix["packages"])
    for binding in matrix["profile_bindings"]:
        visit_contract_references(binding.get("additional_contract_paths", []))
    semantic_paths.update(matrix_references)
    assert_equal(
        receipt["semantic_contracts"],
        expected_file_records(list(semantic_paths)),
        "semantic contract hashes",
    )
    receipt_semantic_paths = {
        ROOT / item["path"] for item in receipt["semantic_contracts"]
    }
    missing_matrix_references = matrix_references.difference(receipt_semantic_paths)
    if missing_matrix_references:
        fail(
            "semantic receipt omits matrix-referenced shared contracts: "
            + ", ".join(
                path.relative_to(ROOT).as_posix()
                for path in sorted(missing_matrix_references)
            )
        )
    formal_paths = list((ROOT / "formal/contracts").glob("*.json"))
    assert_equal(
        receipt["formal_contracts"],
        expected_file_records(formal_paths),
        "formal contract hashes",
    )

    suite = read_json("formal/contracts/declared_reconstruction_formal_suite_v1.json")
    expected_binaries = suite["environment_contract"]["canonical_binaries"]
    assert_equal(receipt["frozen_binaries"], expected_binaries, "frozen binaries")
    assert_equal(len(expected_binaries), 7, "frozen binary count")
    for name, binary in expected_binaries.items():
        path = ROOT / binary["path"]
        if not path.is_file():
            fail(f"frozen binary is missing: {name} -> {binary['path']}")
        assert_equal(sha256(path), binary["sha256"], f"frozen binary hash {name}")

    expected_summary_hash = sha256(SUMMARY)
    summary_records = [
        item
        for item in receipt["generated_artifacts"]
        if item["path"] == "evidence/closure/step14_project_summary.json"
    ]
    assert_equal(len(summary_records), 1, "generated summary record count")
    assert_equal(
        summary_records[0]["sha256"],
        expected_summary_hash,
        "generated summary hash",
    )
    if any(item.get("path") == RECEIPT.relative_to(ROOT).as_posix() for item in receipt["generated_artifacts"]):
        fail("closure receipt must not hash or list itself as a generated artifact")

    expected_test_summary = {
        "full_ctest": ("passed", 56, 0),
        "formal_contract_audit": ("passed", None, None),
        "evidence_scope": ("passed", None, None),
        "template_self_test": ("passed", None, None),
        "public_audit": ("passed", None, None),
        "git_diff_check": ("passed", None, None),
        "step14_closure_audit": ("passed", None, None),
    }
    for gate, (status, passed, failed) in expected_test_summary.items():
        if gate not in receipt["test_summary"]:
            fail(f"test summary lacks {gate}")
        assert_equal(receipt["test_summary"][gate]["status"], status, f"{gate} status")
        if passed is not None:
            assert_equal(receipt["test_summary"][gate]["passed"], passed, f"{gate} passed")
        if failed is not None:
            assert_equal(receipt["test_summary"][gate]["failed"], failed, f"{gate} failed")

    assert_equal(
        receipt["formal_campaign"]["launched_by_step14"],
        False,
        "formal campaign launch",
    )
    assert_equal(
        receipt["formal_campaign"]["results_written_by_step14"],
        False,
        "formal result write",
    )
    return receipt


def git_paths_at_commit(commit: str, prefix: str) -> list[str]:
    result = subprocess.run(
        ["git", "ls-tree", "-r", "--name-only", commit, "--", prefix],
        cwd=ROOT,
        check=True,
        text=True,
        capture_output=True,
    )
    return [line for line in result.stdout.splitlines() if line]


def bytes_at_commit(commit: str, path: str) -> bytes:
    result = subprocess.run(
        ["git", "show", f"{commit}:{path}"],
        cwd=ROOT,
        check=True,
        capture_output=True,
    )
    return result.stdout


def audit_preservation() -> tuple[int, int]:
    old_formal = git_paths_at_commit(
        PRESERVATION_BASELINE_COMMIT, "formal/contracts"
    )
    old_spark2 = git_paths_at_commit(
        PRESERVATION_BASELINE_COMMIT, "hpc/wflop_cpp/spark2"
    )
    for path in [*old_formal, *old_spark2]:
        current = ROOT / path
        if not current.is_file():
            fail(f"baseline-preserved path is missing: {path}")
        if current.read_bytes() != bytes_at_commit(PRESERVATION_BASELINE_COMMIT, path):
            fail(
                f"baseline-preserved path changed since "
                f"{PRESERVATION_BASELINE_COMMIT}: {path}"
            )

    baseline_registry_bytes = bytes_at_commit(
        PRESERVATION_BASELINE_COMMIT,
        "shared/contracts/executable_profile_evidence.json",
    )
    baseline_registry = json.loads(baseline_registry_bytes.decode("utf-8"))
    current_registry = read_json("shared/contracts/executable_profile_evidence.json")
    current_by_id = {
        item["profile_id"]: item for item in current_registry["profiles"]
    }
    identity_fields = [
        "profile_id",
        "algorithm_id",
        "method_semantics_id",
        "problem_id",
        "problem_semantics_id",
        "method_evidence_tier",
        "problem_evidence_tier",
    ]
    for baseline_profile in baseline_registry["profiles"]:
        if baseline_profile["method_evidence_tier"] not in {
            "M0_AUTHOR_SOURCE",
            "M1_PAPER_COMPLETE",
        }:
            continue
        if baseline_profile["problem_evidence_tier"] not in {
            "P0_AUTHOR_ASSET",
            "P1_PAPER_COMPLETE",
        }:
            continue
        profile_id = baseline_profile["profile_id"]
        if profile_id not in current_by_id:
            fail(f"complete-information baseline profile disappeared: {profile_id}")
        current_profile = current_by_id[profile_id]
        for field in identity_fields:
            assert_equal(
                current_profile[field],
                baseline_profile[field],
                f"complete-information baseline identity {profile_id}.{field}",
            )
    return len(old_formal), len(old_spark2)


def audit_step14_evidence_scope() -> None:
    result = subprocess.run(
        [
            "git",
            "status",
            "--porcelain",
            "--untracked-files=all",
            "--",
            "evidence",
        ],
        cwd=ROOT,
        check=True,
        text=True,
        capture_output=True,
    )
    for line in result.stdout.splitlines():
        path = line[3:]
        if not path.startswith("evidence/closure/"):
            fail(f"Step-14 modified non-closure evidence: {path}")

    result = subprocess.run(
        [
            "git",
            "diff",
            "--name-only",
            SOURCE_BASELINE_COMMIT,
            "--",
            "evidence",
            "formal/evidence",
            "results",
        ],
        cwd=ROOT,
        check=True,
        text=True,
        capture_output=True,
    )
    for path in result.stdout.splitlines():
        if path and not path.startswith("evidence/closure/"):
            fail(f"Historical evidence/result differs from source baseline: {path}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--formal-only-evidence",
        action="store_true",
        help=(
            "Verify that Step-14 touched only closure metadata under evidence/ "
            "and did not change formal evidence, raw results, or historical results."
        ),
    )
    args = parser.parse_args()
    if args.formal_only_evidence:
        audit_step14_evidence_scope()
        print("step14_formal_only_evidence_audit_pass")
        return 0

    summary = audit_summary()
    audit_receipt(summary)
    old_formal_count, old_spark2_count = audit_preservation()
    audit_step14_evidence_scope()
    print(
        "step14_closure_audit_pass "
        f"papers={summary['snapshot']['paper_count']} "
        f"profiles={summary['executable_inventory']['profile_pair_count']} "
        f"guarded={summary['identity_guards']['guarded_original_count']} "
        f"learning={summary['learning_reconstructions']['profile_count']} "
        f"suites={len(summary['suite_manifests'])} "
        f"preserved_formal={old_formal_count} "
        f"preserved_spark2={old_spark2_count}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
