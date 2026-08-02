#!/usr/bin/env python3
"""Self-test the copy-ready benchmark package template."""

from __future__ import annotations

import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


PACKAGE_ROOT = Path(__file__).resolve().parents[1]
REPOSITORY_ROOT = PACKAGE_ROOT.parent
PROHIBITED_SUFFIXES = {".pdf", ".mat", ".mex", ".npz"}
EXPECTED_ELEMENT_IDS = [
    "paper_source_identity_dossier",
    "source_search_bounded_negative_evidence",
    "method_problem_evidence_tier_schema",
    "r0_r4_lifecycle_checklist",
    "uncertainty_decision_ledger",
    "learned_state_training_contract",
    "problem_asset_physical_fes_contract",
    "scalar_oracle_fixed_layout_fixture",
    "deterministic_random_event_contract",
    "fact_declared_pure_cpp_registration_skeleton",
    "backend_interface_capability_fail_closed",
    "semantic_sensitivity_scaling_formal_tests",
    "claim_boundary_non_pooling",
    "pre_code_one_command_audit",
    "post_code_r1_r4_one_command_audit",
    "reference_only_worked_example",
]


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def run(
    command: list[str],
    *,
    cwd: Path,
    expect_success: bool,
) -> None:
    completed = subprocess.run(
        command,
        cwd=cwd,
        env={**os.environ, "PYTHONDONTWRITEBYTECODE": "1"},
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
    )
    if expect_success and completed.returncode != 0:
        raise RuntimeError(
            f"command failed: {' '.join(command)}\n{completed.stdout}"
        )
    if not expect_success and completed.returncode == 0:
        raise RuntimeError(
            f"fail-closed command unexpectedly passed: {' '.join(command)}"
        )
    if expect_success:
        print(completed.stdout, end="")


def git_status() -> str:
    completed = subprocess.run(
        ["git", "status", "--porcelain"],
        cwd=REPOSITORY_ROOT,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=True,
    )
    return completed.stdout


def write_json(root: Path, relative_path: str, payload: dict) -> None:
    (root / relative_path).write_text(
        json.dumps(payload, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )


def resolve_string_placeholders(value: object) -> object:
    if isinstance(value, dict):
        return {
            key: resolve_string_placeholders(item)
            for key, item in value.items()
        }
    if isinstance(value, list):
        return [resolve_string_placeholders(item) for item in value]
    if isinstance(value, str):
        return re.sub(
            r"REQUIRED_[A-Z0-9_]+",
            "synthetic_self_test_value",
            value,
        )
    return value


def instantiate_synthetic_package(root: Path) -> None:
    identity = json.loads(
        (root / "metadata/paper_source_identity.json").read_text()
    )
    identity.update(
        {
            "package_id": "synthetic_self_test_package",
            "profile_id": "synthetic_algorithm__synthetic_problem",
            "identity_status": "frozen",
            "claim_boundary": (
                "Synthetic template audit fixture only; no external method, "
                "problem, result, or performance claim."
            ),
        }
    )
    identity["paper"].update(
        {
            "corpus_id": "SYNTHETIC",
            "doi": "10.0000/synthetic-template-self-test",
            "title": "Synthetic benchmark package self-test",
            "authors": ["Template Self Test"],
            "publisher_landing_url": "https://example.invalid/synthetic-paper",
            "pdf_sha256": "not_published_synthetic_fixture",
            "supplement_sha256": "not_published_synthetic_fixture",
        }
    )
    identity["source"].update(
        {
            "author_repository_url": "explicitly_not_found_in_synthetic_search",
            "immutable_revision": "not_applicable",
            "tree_or_archive_sha256": "not_applicable",
            "license_identifier": "synthetic_fixture_no_external_source",
            "source_use_boundary": "metadata and clean-room fixture only",
        }
    )
    write_json(root, "metadata/paper_source_identity.json", identity)

    search = json.loads(
        (root / "metadata/source_search_evidence.json").read_text()
    )
    search_log = json.loads(
        (root / "metadata/source_search_log.json").read_text()
    )
    search["search_id"] = "synthetic_complete_authority_search_v1"
    search["search_date_utc"] = "2026-07-29"
    search_log["log_id"] = "synthetic_complete_query_log_v1"
    for entry in search_log["entries"]:
        rank = entry["authority_rank"]
        entry.update(
            {
                "query_id": f"synthetic_authority_rank_{rank}",
                "query": f"synthetic complete query for authority rank {rank}",
                "url": f"https://example.invalid/authority-rank-{rank}",
                "searched_at_utc": "2026-07-29T00:00:00Z",
                "status": "searched_not_found",
                "immutable_receipt": {
                    "kind": "synthetic_http_receipt",
                    "value": f"synthetic-receipt-rank-{rank}",
                    "sha256": hashlib.sha256(
                        f"synthetic-receipt-rank-{rank}".encode()
                    ).hexdigest(),
                },
            }
        )
    write_json(root, "metadata/source_search_log.json", search_log)
    query_log_sha256 = sha256_file(
        root / "metadata/source_search_log.json"
    )
    for row in search["permission_ladder"]:
        row["status"] = "searched_not_found"
        row["evidence"] = [
            f"https://example.invalid/authority-rank-{row['rank']}"
        ]
    negative = search["bounded_negative_evidence"]
    negative["query_log_path"] = "metadata/source_search_log.json"
    negative["query_log_id"] = "synthetic_complete_query_log_v1"
    negative["query_log_sha256"] = query_log_sha256
    negative["searched_identifiers"] = [
        "10.0000/synthetic-template-self-test",
        "Synthetic benchmark package self-test",
        "synthetic_algorithm",
    ]
    negative["unavailable_fields"] = [
        "author learned state",
        "one state-transition tie break",
    ]
    search["reconstruction_permission"]["allowed"] = True
    write_json(root, "metadata/source_search_evidence.json", search)

    lifecycle = json.loads(
        (root / "metadata/r0_r4_checklist.json").read_text()
    )
    lifecycle["package_id"] = "synthetic_self_test_package"
    for gate in lifecycle["gates"].values():
        gate["status"] = "pass"
    write_json(root, "metadata/r0_r4_checklist.json", lifecycle)

    decisions = json.loads(
        (root / "metadata/uncertainty_decision_ledger.json").read_text()
    )
    decisions["package_id"] = "synthetic_self_test_package"
    decision = decisions["decisions"][0]
    decision.update(
        {
            "decision_id": "synthetic_tie_break_completion_v1",
            "field": "equal-objective layout tie break",
            "completed_transition": "survivor selection",
            "selected_value_or_rule": "lexicographically smaller layout",
            "authority_type": "M3_DECLARED_COMPLETION",
            "immutable_authority": [
                "metadata/source_search_evidence.json"
            ],
            "rejected_alternatives": [
                "random tie break rejected because it changes worker equivalence"
            ],
            "expected_downstream_effect": (
                "only equal-objective survivor identity can change"
            ),
            "sensitivity_profile_id": "synthetic_reverse_tie_sensitivity_v1",
            "adjudication_status": "selected_for_synthetic_self_test",
        }
    )
    write_json(root, "metadata/uncertainty_decision_ledger.json", decisions)

    learned = json.loads(
        (root / "metadata/learned_state_training_contract.json").read_text()
    )
    learned.update(
        {
            "package_id": "synthetic_self_test_package",
            "applies": False,
            "not_applicable_reason": "synthetic algorithm has no learned state",
            "training_data_or_generator_id": "not_applicable",
            "training_data_semantic_id": "not_applicable",
            "initialization": "not_applicable",
            "optimizer": "not_applicable",
            "epochs_or_steps": "not_applicable",
            "learned_state_origin": "not_applicable",
            "claim_boundary": "No learned-state claim.",
        }
    )
    learned["random_seeds"]["namespace"] = (
        "training/synthetic_self_test_package"
    )
    learned["random_seeds"]["values_or_manifest"] = "not_applicable"
    learned["checkpoint"].update(
        {
            "required": False,
            "path": "not_applicable",
            "sha256": "not_applicable",
            "metadata_hash": "not_applicable",
        }
    )
    learned["training_work"].update(
        {
            "compute_unit": "not_applicable",
            "compute_count": 0,
            "training_physical_fes": 0,
            "inference_physical_fes": 31,
            "total_physical_fes_equation": "0 + 31 = 31",
            "offline_training_is_separate_from_inference_budget": False,
            "online_evaluator_calls_count_inside_run_total": False,
        }
    )
    write_json(root, "metadata/learned_state_training_contract.json", learned)

    problem = json.loads(
        (root / "metadata/problem_physical_fes_contract.json").read_text()
    )
    problem.update(
        {
            "package_id": "synthetic_self_test_package",
            "problem_id": "synthetic_problem",
            "problem_semantics_id": "synthetic_scalar_problem_v1",
            "problem_evidence_tier": "P4_FORMULA_FIXTURE",
            "claim_boundary": (
                "Synthetic scalar fixture only; no wind-farm result claim."
            ),
        }
    )
    problem["decision_space"].update(
        {
            "encoding": "four unique unsigned integers",
            "dimension_or_cardinality_rule": "exactly four values",
            "bounds_or_candidate_set": "integers 0 through 255",
        }
    )
    problem["objective"].update(
        {
            "sense": "minimize",
            "quantity": "sum of squared integer values",
            "unit": "dimensionless",
            "aggregation": "single scalar fixture",
        }
    )
    problem["constraints"].update(
        {
            "feasibility_rules": ["four values must be unique"],
            "repair_or_rejection": "reject duplicate values",
            "constraint_violation_normalization": "not_applicable",
        }
    )
    problem["physical_models"].update(
        {
            "wake": "not_applicable",
            "turbine": "not_applicable",
            "wind_resource": "not_applicable",
            "terrain_cable_acoustic_or_other": "not_applicable",
        }
    )
    problem["assets"] = [
        {
            "asset_id": "synthetic_inline_fixture",
            "relative_path": "fixtures/fixed_layout_fixture.json",
            "sha256": "computed_by_instantiated_package_release_gate",
            "license_or_use_boundary": "generated synthetic metadata",
            "redistributable": True,
        }
    ]
    problem["physical_fes"].update(
        {
            "training_evaluator_calls": "not_applicable",
            "budget_per_run": 31,
        }
    )
    write_json(root, "metadata/problem_physical_fes_contract.json", problem)

    random_events = json.loads(
        (
            root / "metadata/deterministic_random_event_contract.json"
        ).read_text()
    )
    random_events["contract_id"] = "synthetic_counter_event_v1"
    random_events["ordered_reductions"] = [
        "lower objective",
        "lexicographically smaller layout",
        "ascending layout serialization",
    ]
    write_json(
        root,
        "metadata/deterministic_random_event_contract.json",
        random_events,
    )

    backends = json.loads(
        (root / "metadata/backend_capabilities.json").read_text()
    )
    backends["capability_id"] = "synthetic_cpu_fail_closed_v1"
    backends["backends"]["cpu"]["work_partition"] = (
        "independent candidate construction and scalar evaluation"
    )
    backends["backends"]["cpu"]["ordered_stages"] = [
        "best-layout reduction"
    ]
    write_json(root, "metadata/backend_capabilities.json", backends)

    boundary = json.loads(
        (root / "metadata/claim_boundary_non_pooling.json").read_text()
    )
    boundary.update(
        {
            "profile_id": "synthetic_algorithm__synthetic_problem",
            "method_semantics_id": "synthetic_deterministic_algorithm_v1",
            "problem_semantics_id": "synthetic_scalar_problem_v1",
            "method_evidence_tier": "M4_FORMULA_FIXTURE",
            "problem_evidence_tier": "P4_FORMULA_FIXTURE",
            "claim_boundary": (
                "Synthetic template fixture validating package gates only."
            ),
            "prohibited_claims": [
                "original algorithm reproduction",
                "wind-farm optimization quality",
                "accelerator speedup",
            ],
            "formal_admission_status": "admitted",
        }
    )
    write_json(root, "metadata/claim_boundary_non_pooling.json", boundary)

    fixed = json.loads(
        (root / "fixtures/fixed_layout_fixture.json").read_text()
    )
    fixed.update(
        {
            "fixture_id": "synthetic_layout_0_1_2_3_v1",
            "problem_semantics_id": "synthetic_scalar_problem_v1",
            "case_id": "synthetic_case",
            "case_semantic_hash": "fnv1a64:synthetic-case-v1",
            "layout_encoding": "four unique unsigned integers",
            "layout": [0, 1, 2, 3],
            "feasible": True,
            "expected_constraint_values": [0.0],
            "source": "generated synthetic self-test fixture",
        }
    )
    write_json(root, "fixtures/fixed_layout_fixture.json", fixed)
    problem["assets"][0]["sha256"] = sha256_file(
        root / "fixtures/fixed_layout_fixture.json"
    )
    write_json(root, "metadata/problem_physical_fes_contract.json", problem)

    oracle = json.loads((root / "fixtures/scalar_oracle.json").read_text())
    oracle.update(
        {
            "oracle_id": "synthetic_sum_of_squares_oracle_v1",
            "fixture_id": "synthetic_layout_0_1_2_3_v1",
            "problem_semantics_id": "synthetic_scalar_problem_v1",
            "oracle_authority": "independent hand calculation 0+1+4+9",
            "oracle_source_sha256": "not_applicable_inline_equation",
        }
    )
    oracle["expected_outputs"] = [
        {
            "name": "sum_of_squares",
            "value": 14.0,
            "unit": "dimensionless",
            "absolute_tolerance": 1.0e-12,
            "relative_tolerance": 1.0e-12,
        }
    ]
    write_json(root, "fixtures/scalar_oracle.json", oracle)

    campaign = json.loads(
        (root / "metadata/formal_campaign_contract.json").read_text()
    )
    campaign.update(
        {
            "campaign_id": "synthetic_formal_campaign_v1",
            "suite_id": "synthetic_formal_suite_v1",
        }
    )
    campaign["source"].update(
        {
            "commit": "a" * 40,
            "clean_worktree_required": True,
        }
    )
    campaign["environment"].update(
        {
            "hostname": "synthetic-host",
            "architecture": "aarch64",
            "operating_system": "synthetic-os",
            "cpu_model": "synthetic-cpu",
            "visible_logical_cpus": 20,
            "compiler": "GNU 11.5.0",
            "compiler_flags": ["-O3", "-DNDEBUG"],
            "libraries": [
                {
                    "name": "synthetic-standard-library",
                    "path": "toolchain-provided",
                    "sha256": "b" * 64,
                }
            ],
        }
    )
    campaign["binary"]["sha256"] = "0" * 64
    campaign["execution"].update(
        {
            "workers": 20,
            "cpu_work_partition": (
                "independent construction and scalar evaluation"
            ),
        }
    )
    campaign["profile"].update(
        {
            "profile_id": boundary["profile_id"],
            "method_semantics_id": boundary["method_semantics_id"],
            "problem_semantics_id": boundary["problem_semantics_id"],
            "method_evidence_tier": boundary["method_evidence_tier"],
            "problem_evidence_tier": boundary["problem_evidence_tier"],
            "claim_boundary": boundary["claim_boundary"],
        }
    )
    campaign["work"].update(
        {
            "training_seed_namespace": learned["random_seeds"]["namespace"],
            "optimization_seed_namespace": (
                "optimization/synthetic_self_test_package"
            ),
            "training_physical_fes": 0,
            "inference_physical_fes": 31,
            "total_physical_fes_equation": "0 + 31 = 31",
            "physical_fes_per_run": 31,
        }
    )
    campaign["optimization_seeds"] = [
        2026073101 + offset for offset in range(25)
    ]
    campaign["cases"] = [
        {
            "case_id": fixed["case_id"],
            "case_semantic_hash": fixed["case_semantic_hash"],
        }
    ]
    campaign["reporting"].update(
        {
            "active_objective_type": "scalar",
            "scalar": {
                "metrics": [
                    "best objective",
                    "median",
                    "interquartile range",
                ],
                "normalization": "raw dimensionless sum-of-squares scale",
                "reference": "synthetic_sum_of_squares_oracle_v1",
            },
            "multiobjective": {
                "metrics": [
                    "hypervolume",
                    "IGD+",
                    "feasible nondominated count",
                ],
                "normalization": (
                    "per-case frozen objective minima and maxima when active"
                ),
                "reference_front": (
                    "per-case frozen empirical reference front when active"
                ),
                "hypervolume_reference_point": (
                    "1.1 in every normalized minimization objective when active"
                ),
            },
        }
    )
    write_json(root, "metadata/formal_campaign_contract.json", campaign)

    payload_roots = [
        root / "metadata",
        root / "fixtures",
        root / "cpp",
    ]
    for payload_root in payload_roots:
        for path in payload_root.rglob("*"):
            if not path.is_file():
                continue
            if path.suffix == ".json":
                payload = json.loads(path.read_text(encoding="utf-8"))
                payload = resolve_string_placeholders(payload)
                path.write_text(
                    json.dumps(payload, indent=2, sort_keys=True) + "\n",
                    encoding="utf-8",
                )
            elif path.suffix in {".cpp", ".hpp"}:
                text = path.read_text(encoding="utf-8")
                text = re.sub(
                    r"REQUIRED_[A-Z0-9_]+",
                    "SYNTHETIC_SELF_TEST_VALUE",
                    text,
                )
                path.write_text(text, encoding="utf-8")

    problem = json.loads(
        (root / "metadata/problem_physical_fes_contract.json").read_text()
    )
    problem["assets"][0]["sha256"] = sha256_file(
        root / "fixtures/fixed_layout_fixture.json"
    )
    write_json(root, "metadata/problem_physical_fes_contract.json", problem)
    search = json.loads(
        (root / "metadata/source_search_evidence.json").read_text()
    )
    search["bounded_negative_evidence"]["query_log_sha256"] = sha256_file(
        root / "metadata/source_search_log.json"
    )
    write_json(root, "metadata/source_search_evidence.json", search)

    for payload_root in payload_roots:
        for path in payload_root.rglob("*"):
            if path.is_file() and path.suffix in {".json", ".cpp", ".hpp"}:
                require(
                    "REQUIRED_" not in path.read_text(encoding="utf-8"),
                    f"synthetic instantiation retains placeholder: {path}",
                )


def freeze_synthetic_binary(root: Path, build: Path) -> None:
    subprocess.run(
        [
            "cmake",
            "-S",
            str(root / "cpp"),
            "-B",
            str(build),
            "-DCMAKE_BUILD_TYPE=Release",
        ],
        cwd=root,
        check=True,
        stdout=subprocess.DEVNULL,
    )
    subprocess.run(
        ["cmake", "--build", str(build), "-j2"],
        cwd=root,
        check=True,
        stdout=subprocess.DEVNULL,
    )
    binary = build / "benchmark_package_runner"
    require(binary.is_file(), "synthetic runner binary was not built")
    campaign_path = root / "metadata/formal_campaign_contract.json"
    campaign = json.loads(campaign_path.read_text(encoding="utf-8"))
    campaign["binary"]["sha256"] = sha256_file(binary)
    write_json(root, "metadata/formal_campaign_contract.json", campaign)


def main() -> int:
    initial_git_status = git_status()
    manifest = json.loads(
        (PACKAGE_ROOT / "template_manifest.json").read_text(encoding="utf-8")
    )
    elements = manifest["numbered_elements"]
    require(
        manifest["numbered_element_count"] == 16 and len(elements) == 16,
        "template element count differs",
    )
    require(
        [row["number"] for row in elements] == list(range(1, 17)),
        "template element numbering differs",
    )
    require(
        [row["id"] for row in elements] == EXPECTED_ELEMENT_IDS,
        "template element identities differ",
    )
    for row in elements:
        for relative_path in row["paths"]:
            require(
                (PACKAGE_ROOT / relative_path).is_file(),
                f"manifest path missing: {relative_path}",
            )

    for path in PACKAGE_ROOT.rglob("*"):
        if path.is_file():
            require(
                path.suffix.lower() not in PROHIBITED_SUFFIXES,
                f"prohibited payload in template: {path.name}",
            )
            if path.suffix == ".json":
                json.loads(path.read_text(encoding="utf-8"))

    worked = json.loads(
        (
            PACKAGE_ROOT / "worked-example/fqfode_reference.json"
        ).read_text(encoding="utf-8")
    )
    require(
        worked["references_only"] is True
        and worked["contains_restricted_data"] is False
        and worked["contains_third_party_source"] is False,
        "worked example must remain metadata-only",
    )
    for reference in worked["project_references"]:
        relative = Path(reference["path"])
        require(not relative.is_absolute(), "worked-example path must be relative")
        target = REPOSITORY_ROOT / relative
        require(target.is_file(), f"worked-example target missing: {relative}")
        require(
            sha256_file(target) == reference["sha256"],
            f"worked-example reference hash differs: {relative}",
        )

    temporary_path: Path | None = None
    with tempfile.TemporaryDirectory(
        prefix="wflop-benchmark-template-self-test-"
    ) as temporary:
        temporary_path = Path(temporary)
        copied = temporary_path / "benchmark-package-template"
        shutil.copytree(
            PACKAGE_ROOT,
            copied,
            ignore=shutil.ignore_patterns("__pycache__", "*.pyc"),
        )
        copied_manifest = json.loads(
            (copied / "template_manifest.json").read_text(encoding="utf-8")
        )
        require(
            copied_manifest == manifest,
            "temporary template manifest differs after copy",
        )
        run(
            [
                sys.executable,
                str(copied / "scripts/audit_pre_code.py"),
                "--template-mode",
            ],
            cwd=copied,
            expect_success=True,
        )
        run(
            [sys.executable, str(copied / "scripts/audit_pre_code.py")],
            cwd=copied,
            expect_success=False,
        )
        run(
            [
                sys.executable,
                str(copied / "scripts/audit_r1_r4.py"),
                "--template-mode",
            ],
            cwd=copied,
            expect_success=True,
        )
        run(
            [
                sys.executable,
                str(copied / "scripts/audit_r1_r4.py"),
                "--skip-build",
            ],
            cwd=copied,
            expect_success=False,
        )
        instantiate_synthetic_package(copied)
        freeze_synthetic_binary(copied, temporary_path / "synthetic-prebuild")
        run(
            [sys.executable, str(copied / "scripts/audit_pre_code.py")],
            cwd=copied,
            expect_success=True,
        )
        run(
            [sys.executable, str(copied / "scripts/audit_r1_r4.py")],
            cwd=copied,
            expect_success=True,
        )
    require(
        temporary_path is not None and not temporary_path.exists(),
        "temporary self-test directory was not cleaned",
    )
    require(
        git_status() == initial_git_status,
        "template self-test changed the project git status",
    )
    print(
        "benchmark_package_template_self_test_pass "
        "elements=16 cpp_tests=4 hybrid_gpu_fail_closed=true "
        "default_audits_fail_closed=true instantiated_audits_pass=true "
        "temp_copy_cleaned=true "
        "git_status_unchanged=true"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
