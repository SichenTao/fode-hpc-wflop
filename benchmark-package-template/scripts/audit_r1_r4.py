#!/usr/bin/env python3
"""One-command post-code R1-R4 audit for a benchmark package."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import subprocess
import sys
import tempfile
from pathlib import Path

sys.dont_write_bytecode = True

from audit_pre_code import PACKAGE_ROOT, audit as audit_pre_code


FACT_PREFIX = "/* BENCHMARK PACKAGE FACT DECLARATION"


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


def load_json(relative_path: str) -> dict:
    return json.loads((PACKAGE_ROOT / relative_path).read_text(encoding="utf-8"))


def run(command: list[str]) -> None:
    subprocess.run(command, cwd=PACKAGE_ROOT, check=True)


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def audit_campaign_metadata(*, template_mode: bool) -> dict:
    campaign = load_json("metadata/formal_campaign_contract.json")
    boundary = load_json("metadata/claim_boundary_non_pooling.json")
    problem = load_json("metadata/problem_physical_fes_contract.json")
    learned = load_json("metadata/learned_state_training_contract.json")
    fixed = load_json("fixtures/fixed_layout_fixture.json")
    backends = load_json("metadata/backend_capabilities.json")

    require(
        campaign["status"] == "contract_frozen_not_launched",
        "formal campaign contract must remain frozen and unlaunched",
    )
    require(
        campaign["source"]["clean_worktree_required"] is True,
        "formal campaign requires a clean source worktree",
    )
    execution = campaign["execution"]
    require(
        execution["processes_at_once"] == 1
        and execution["gpu_used"] is False
        and execution["cpu_work_partition"],
        "formal execution partition or single-process contract differs",
    )
    require(
        execution["backend"] in backends["backends"]
        and backends["backends"][execution["backend"]]["supported"] is True,
        "formal campaign requests an unsupported backend",
    )
    if template_mode:
        require(
            all(
                key in campaign
                for key in (
                    "profile",
                    "work",
                    "optimization_seeds",
                    "cases",
                    "resume",
                    "non_pooling",
                    "reporting",
                )
            ),
            "formal campaign template structure is incomplete",
        )
        return campaign

    profile = campaign["profile"]
    for field in (
        "profile_id",
        "method_semantics_id",
        "problem_semantics_id",
        "method_evidence_tier",
        "problem_evidence_tier",
        "claim_boundary",
    ):
        require(
            profile[field] == boundary[field],
            f"formal profile {field} differs from claim-boundary contract",
        )
    require(
        profile["problem_semantics_id"] == problem["problem_semantics_id"],
        "formal profile and problem semantic IDs differ",
    )
    work = campaign["work"]
    require(
        work["namespaces_disjoint"] is True
        and work["training_seed_namespace"]
        != work["optimization_seed_namespace"]
        and work["training_seed_namespace"]
        == learned["random_seeds"]["namespace"],
        "formal training and optimization namespaces differ",
    )
    require(
        work["training_physical_fes"]
        == learned["training_work"]["training_physical_fes"]
        and work["inference_physical_fes"]
        == learned["training_work"]["inference_physical_fes"]
        and work["total_physical_fes_equation"]
        == learned["training_work"]["total_physical_fes_equation"],
        "formal training/inference FES ledger differs",
    )
    require(
        work["physical_fes_per_run"]
        == problem["physical_fes"]["budget_per_run"]
        and work["overshoot_forbidden"] is True,
        "formal exact physical-FES budget differs",
    )
    seeds = campaign["optimization_seeds"]
    require(
        len(seeds) == 25
        and len(set(seeds)) == 25
        and all(isinstance(seed, int) for seed in seeds),
        "formal campaign requires 25 unique integer optimization seeds",
    )
    cases = campaign["cases"]
    require(
        cases
        and len({row["case_id"] for row in cases}) == len(cases)
        and all(row["case_semantic_hash"] for row in cases),
        "formal cases require unique IDs and semantic hashes",
    )
    require(
        any(
            row["case_id"] == fixed["case_id"]
            and row["case_semantic_hash"] == fixed["case_semantic_hash"]
            for row in cases
        ),
        "formal cases do not contain the fixed-layout semantic fixture",
    )
    resume = campaign["resume"]
    required_resume_fields = {
        "campaign_id",
        "profile_id",
        "problem_semantics_id",
        "case_semantic_hash",
        "optimization_seed",
        "physical_fes_per_run",
        "binary_sha256",
        "environment_sha256",
        "source_commit",
    }
    require(
        required_resume_fields <= set(resume["result_key_fields"])
        and resume["reuse_only_complete_validated_results"] is True
        and resume["partial_results_never_reused"] is True
        and "temporary" in resume["atomic_commit"]
        and "fsync" in resume["atomic_commit"]
        and "rename" in resume["atomic_commit"],
        "formal atomic resume contract differs",
    )
    require(
        campaign["non_pooling"]["enabled"] is True
        and campaign["non_pooling"]["pooling_key"] == boundary["pooling_key"],
        "formal non-pooling rule differs",
    )
    reporting = campaign["reporting"]
    require(
        reporting["active_objective_type"] in {"scalar", "multiobjective"}
        and reporting["cross_case_pooling"] is False,
        "formal objective type or cross-case pooling rule differs",
    )
    for objective_type in ("scalar", "multiobjective"):
        record = reporting[objective_type]
        require(
            record["metrics"] and record["normalization"],
            f"{objective_type} reporting metrics or normalization missing",
        )
    require(
        reporting["scalar"]["reference"]
        and reporting["multiobjective"]["reference_front"]
        and reporting["multiobjective"]["hypervolume_reference_point"],
        "formal scalar or multiobjective reference contract missing",
    )

    require(
        "REQUIRED_" not in json.dumps(campaign),
        "formal campaign contract has unresolved fields",
    )
    require(
        re.fullmatch(r"[0-9a-f]{40}", campaign["source"]["commit"]) is not None,
        "formal source commit must be a full 40-character hexadecimal commit",
    )
    environment = campaign["environment"]
    for field in (
        "hostname",
        "architecture",
        "operating_system",
        "cpu_model",
        "compiler",
        "build_type",
    ):
        require(environment[field], f"formal environment {field} missing")
    require(
        isinstance(environment["visible_logical_cpus"], int)
        and environment["visible_logical_cpus"] > 0
        and environment["compiler_flags"]
        and environment["libraries"],
        "formal CPU, compiler flags, or libraries are incomplete",
    )
    require(
        all(
            row["name"]
            and row["path"]
            and re.fullmatch(r"[0-9a-f]{64}", row["sha256"])
            for row in environment["libraries"]
        ),
        "formal library identity or SHA256 is incomplete",
    )
    require(
        re.fullmatch(r"[0-9a-f]{64}", campaign["binary"]["sha256"])
        is not None,
        "formal binary SHA256 must be frozen",
    )
    require(
        isinstance(execution["workers"], int) and execution["workers"] > 0,
        "formal worker count must be a positive integer",
    )
    return campaign


def audit_frozen_binary(campaign: dict, build: Path) -> None:
    relative = Path(campaign["binary"]["relative_build_path"])
    require(not relative.is_absolute(), "formal binary path must be relative")
    binary = (build / relative).resolve()
    build_root = build.resolve()
    require(
        binary == build_root or build_root in binary.parents,
        "formal binary path escapes build root",
    )
    require(binary.is_file(), "formal frozen binary is missing after build")
    require(
        sha256_file(binary) == campaign["binary"]["sha256"],
        "formal frozen binary SHA256 differs after canonical build",
    )


def audit(*, template_mode: bool, skip_build: bool) -> None:
    audit_pre_code(template_mode=template_mode)

    lifecycle = load_json("metadata/r0_r4_checklist.json")
    backends = load_json("metadata/backend_capabilities.json")
    boundary = load_json("metadata/claim_boundary_non_pooling.json")
    manifest = load_json("template_manifest.json")
    campaign = audit_campaign_metadata(template_mode=template_mode)

    if not template_mode:
        require(
            all(
                lifecycle["gates"][gate]["status"] == "pass"
                for gate in ("R0", "R1", "R2", "R3", "R4")
            ),
            "R0 through R4 must pass for post-code admission",
        )
        require(
            boundary["formal_admission_status"] == "admitted",
            "formal campaign remains blocked until exact R4 admission",
        )

    cpp_files = sorted((PACKAGE_ROOT / "cpp").rglob("*.cpp"))
    cpp_files += sorted((PACKAGE_ROOT / "cpp").rglob("*.hpp"))
    require(cpp_files, "pure C++ implementation files are missing")
    for path in cpp_files:
        source = path.read_text(encoding="utf-8")
        require(
            source.startswith(FACT_PREFIX),
            f"{path.relative_to(PACKAGE_ROOT)} lacks a leading fact declaration",
        )
        if not template_mode:
            require(
                "REQUIRED_" not in source,
                f"{path.relative_to(PACKAGE_ROOT)} has unresolved fact fields",
            )

    capabilities = backends["backends"]
    require(
        capabilities["cpu"]["supported"] is True,
        "CPU backend must be implemented",
    )
    for backend in ("hybrid", "gpu"):
        require(
            capabilities[backend]["supported"] is True
            or capabilities[backend]["status"] == "fails_closed",
            f"{backend} must be implemented or fail closed",
        )
    require(
        len(manifest["numbered_elements"]) == 16,
        "post-code package no longer has sixteen required elements",
    )

    if skip_build:
        return
    with tempfile.TemporaryDirectory(prefix="wflop-package-audit-") as temporary:
        build = Path(temporary) / "build"
        run(
            [
                "cmake",
                "-S",
                str(PACKAGE_ROOT / "cpp"),
                "-B",
                str(build),
                "-DCMAKE_BUILD_TYPE=Release",
            ]
        )
        run(["cmake", "--build", str(build), "-j2"])
        run(["ctest", "--test-dir", str(build), "--output-on-failure"])
        if not template_mode:
            audit_frozen_binary(campaign, build)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--template-mode",
        action="store_true",
        help="audit the template skeleton while allowing unresolved metadata",
    )
    parser.add_argument(
        "--skip-build",
        action="store_true",
        help="check contracts and fact declarations without compiling",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    audit(template_mode=args.template_mode, skip_build=args.skip_build)
    print(
        "benchmark_package_r1_r4_audit_pass "
        f"mode={'template' if args.template_mode else 'instantiated'} "
        f"build={'skipped' if args.skip_build else 'tested'}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
