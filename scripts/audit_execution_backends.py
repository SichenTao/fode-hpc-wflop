#!/usr/bin/env python3
"""Audit the global Gao-Tao executable-profile and backend capability matrix."""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import re
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
MATRIX = ROOT / "shared/contracts/global_execution_capability_matrix.json"
REGISTRY = ROOT / "shared/contracts/executable_profile_evidence.json"
SCOPE = ROOT / "docs/lineage_scope_contract.json"
LINEAGE = ROOT / "docs/author_lineage_registry.tsv"
COMPLETION = ROOT / "docs/lineage_r0_r4_completion.tsv"
REPRODUCIBILITY = (
    ROOT / "shared/contracts/remaining_heterogeneous_reproducibility.json"
)
MODES = {"cpu", "auto", "hybrid", "gpu"}


def load_json(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


def load_tsv(path: Path) -> list[dict[str, str]]:
    with path.open(encoding="utf-8", newline="") as handle:
        return list(csv.DictReader(handle, delimiter="\t"))


def fail(message: str) -> None:
    raise RuntimeError(message)


def normalize_doi(value: str) -> str:
    return value.strip().lower()


def cmake_text() -> str:
    return "\n".join(
        path.read_text(encoding="utf-8")
        for path in sorted(ROOT.rglob("CMakeLists.txt"))
    )


def remove_option(arguments: list[str], selector: str) -> list[str]:
    result: list[str] = []
    index = 0
    while index < len(arguments):
        if arguments[index] == selector:
            index += 2
        else:
            result.append(arguments[index])
            index += 1
    return result


def run_command(command: list[str], expected_success: bool, label: str) -> str:
    completed = subprocess.run(
        command,
        cwd=ROOT,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        timeout=180,
        check=False,
    )
    succeeded = completed.returncode == 0
    if succeeded != expected_success:
        fail(
            f"{label}: expected success={expected_success}, "
            f"returncode={completed.returncode}, "
            f"stdout={completed.stdout[-1200:]!r}, "
            f"stderr={completed.stderr[-1200:]!r}"
        )
    return completed.stdout + completed.stderr


def audit_runtime(
    matrix: dict,
    build_dir: Path,
) -> tuple[int, int, int]:
    positive = 0
    negative = 0
    auto = 0
    help_by_binary: dict[Path, str] = {}
    for package_id, package in matrix["packages"].items():
        binary = build_dir / package["binary_relative_path"]
        if not binary.is_file():
            fail(f"{package_id}: missing built binary {binary}")
        if binary not in help_by_binary:
            help_by_binary[binary] = run_command(
                [str(binary), "--help"], True, f"{package_id} help"
            )
        help_text = help_by_binary[binary]
        for flag in sorted(
            set(re.findall(r"--[a-z0-9-]+", package["cli_template"]))
        ):
            if flag not in help_text:
                fail(f"{package_id}: CLI template flag {flag} absent from --help")

        for asset in package["asset_requirements"]:
            if not (ROOT / asset).exists():
                fail(
                    f"{package_id}: required trusted asset was not staged: {asset}"
                )

        smoke = [str(binary), *package["smoke_arguments"]]
        run_command(smoke, True, f"{package_id} bounded CPU CLI")
        positive += 1

        backend = package["backend_cli"]
        modes = package["execution_modes"]
        selector = backend["selector"]
        for mode in ("hybrid", "gpu"):
            declaration = modes[mode]
            token = backend[mode]
            if declaration["status"] == "fails_closed":
                if selector is None or token is None:
                    fail(f"{package_id}: {mode} fail-closed lacks a CLI token")
                base = remove_option(package["smoke_arguments"], selector)
                failure = run_command(
                    [str(binary), *base, selector, token],
                    False,
                    f"{package_id} {mode} fail-closed",
                ).lower()
                if "unavailable" not in failure or "hidden" not in failure:
                    fail(
                        f"{package_id}: {mode} failure lacks unavailable/"
                        "no-hidden-fallback evidence"
                    )
                negative += 1
                for alias in backend.get(
                    "compatibility_aliases", {}
                ).get(mode, []):
                    alias_failure = run_command(
                        [str(binary), *base, selector, alias],
                        False,
                        f"{package_id} {mode} alias {alias} fail-closed",
                    ).lower()
                    if (
                        "unavailable" not in alias_failure
                        or "hidden" not in alias_failure
                    ):
                        fail(
                            f"{package_id}: {alias} failure lacks unavailable/"
                            "no-hidden-fallback evidence"
                        )
                    negative += 1
            else:
                fail(f"{package_id}: unsupported {mode} status is ambiguous")

        if modes["auto"]["supported"]:
            if (
                modes["auto"]["status"] != "resolves_to_cpu"
                or selector is None
                or backend["auto"] is None
            ):
                fail(f"{package_id}: supported auto lacks recorded CPU resolution")
            base = remove_option(package["smoke_arguments"], selector)
            output = run_command(
                [str(binary), *base, selector, backend["auto"]],
                True,
                f"{package_id} auto-to-CPU",
            )
            if '"resolved_execution_mode":"cpu"' not in output.replace(" ", ""):
                fail(f"{package_id}: auto run did not record CPU resolution")
            auto += 1
        elif modes["auto"]["status"] == "fails_closed":
            if selector is None or backend["auto"] is None:
                fail(f"{package_id}: auto fail-closed lacks a CLI token")
            base = remove_option(package["smoke_arguments"], selector)
            failure = run_command(
                [str(binary), *base, selector, backend["auto"]],
                False,
                f"{package_id} auto fail-closed",
            ).lower()
            if "unavailable" not in failure or "hidden" not in failure:
                fail(
                    f"{package_id}: auto failure lacks unavailable/"
                    "no-hidden-fallback evidence"
                )
            negative += 1
        else:
            fail(f"{package_id}: unsupported auto status is ambiguous")
    return positive, negative, auto


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--build-dir",
        type=Path,
        help="Run real bounded CLI and backend checks against this build tree.",
    )
    arguments = parser.parse_args()

    matrix = load_json(MATRIX)
    registry = load_json(REGISTRY)
    scope = load_json(SCOPE)
    reproducibility = load_json(REPRODUCIBILITY)
    lineage = load_tsv(LINEAGE)
    completion = load_tsv(COMPLETION)
    if matrix["baseline_commit"] != (
        "8ec2181672a406abae01cd1e4242403044ba83f2"
    ):
        fail("Step 10 matrix baseline differs from the approved Step 9 commit")

    scoped_dois = {normalize_doi(value) for value in scope["required_dois"]}
    lineage_by_id = {row["corpus_id"]: row for row in lineage}
    completion_by_id = {row["corpus_id"]: row for row in completion}
    if len(lineage_by_id) != 23 or len(completion_by_id) != 23:
        fail("global matrix requires exactly the 23 scoped lineage records")
    if set(lineage_by_id) != set(completion_by_id):
        fail("lineage and completion corpus IDs differ")
    if {normalize_doi(row["doi"]) for row in lineage} != scoped_dois:
        fail("lineage DOI coverage differs from the frozen 23-paper scope")

    profiles = registry["profiles"]
    registry_by_id = {row["profile_id"]: row for row in profiles}
    if len(registry_by_id) != len(profiles) or len(profiles) < 40:
        fail("executable profile registry is duplicated or below 40 profiles")
    bindings = matrix["profile_bindings"]
    binding_by_id = {row["profile_id"]: row for row in bindings}
    if len(binding_by_id) != len(bindings):
        fail("duplicate profile binding")
    if set(binding_by_id) != set(registry_by_id):
        fail(
            "matrix/profile registry mismatch: "
            f"missing={sorted(set(registry_by_id) - set(binding_by_id))} "
            f"extra={sorted(set(binding_by_id) - set(registry_by_id))}"
        )

    covered_corpus = {
        row["corpus_id"] for row in bindings if row["corpus_id"] is not None
    }
    if covered_corpus != set(lineage_by_id):
        fail(
            "matrix does not cover all 23 scoped papers: "
            f"missing={sorted(set(lineage_by_id) - covered_corpus)}"
        )

    packages = matrix["packages"]
    cmake = cmake_text()
    for package_id, package in packages.items():
        required = {
            "binary_target",
            "binary_relative_path",
            "cli_source",
            "cli_template",
            "smoke_arguments",
            "backend_cli",
            "contract_paths",
            "oracle_or_test_paths",
            "ctest_names",
            "asset_requirements",
            "execution_modes",
        }
        missing = required.difference(package)
        if missing:
            fail(f"{package_id}: missing package fields {sorted(missing)}")
        if not re.search(
            rf"add_executable\s*\(\s*{re.escape(package['binary_target'])}\b",
            cmake,
        ):
            fail(f"{package_id}: CMake target does not exist")
        if not (ROOT / package["cli_source"]).is_file():
            fail(f"{package_id}: CLI source does not exist")
        if not package["cli_template"].strip() or not package["smoke_arguments"]:
            fail(f"{package_id}: CLI mapping is empty")
        for relative in (
            package["contract_paths"] + package["oracle_or_test_paths"]
        ):
            if not (ROOT / relative).is_file():
                fail(f"{package_id}: mapped evidence file does not exist: {relative}")
        for test in package["ctest_names"]:
            if not re.search(
                rf"(?:NAME|add_test\s*\(\s*)\s*{re.escape(test)}\b",
                cmake,
            ):
                fail(f"{package_id}: CTest {test} is not registered")
        modes = package["execution_modes"]
        if set(modes) != MODES:
            fail(f"{package_id}: backend declaration is incomplete")
        if not modes["cpu"]["supported"]:
            fail(f"{package_id}: admitted package lacks a real CPU backend")
        for mode, declaration in modes.items():
            if (
                not isinstance(declaration.get("supported"), bool)
                or not declaration.get("status")
            ):
                fail(f"{package_id}: malformed {mode} declaration")
            if mode in {"hybrid", "gpu"} and declaration["supported"]:
                fail(f"{package_id}: unimplemented accelerator mode was admitted")
            if mode != "cpu" and not declaration["supported"]:
                if declaration["status"] != "fails_closed":
                    fail(f"{package_id}: unsupported {mode} is not fail-closed")
                if (
                    package["backend_cli"]["selector"] is None
                    or package["backend_cli"][mode] is None
                ):
                    fail(f"{package_id}: unsupported {mode} lacks an interface")
        aliases = package["backend_cli"].get("compatibility_aliases", {})
        for mode, tokens in aliases.items():
            if mode not in MODES or not tokens:
                fail(f"{package_id}: malformed backend compatibility alias")
        if "preserved_semantic_slice" in package:
            preserved = package["preserved_semantic_slice"]
            source = (ROOT / preserved["source"]).read_text(encoding="utf-8")
            semantic_slice = source[
                source.index(preserved["start_token"]):
                source.index(preserved["end_token"])
            ]
            observed = hashlib.sha256(
                semantic_slice.encode("utf-8")
            ).hexdigest()
            if observed != preserved["sha256"]:
                fail(f"{package_id}: preserved semantic slice changed")

    for profile_id, binding in binding_by_id.items():
        if binding["package_id"] not in packages:
            fail(f"{profile_id}: unknown package")
        if binding["corpus_id"] is not None:
            corpus = binding["corpus_id"]
            if corpus not in lineage_by_id:
                fail(f"{profile_id}: unknown scoped corpus {corpus}")
            if not binding["formal_status"].strip():
                fail(f"{profile_id}: empty formal status")
        if not binding["training_state"].strip():
            fail(f"{profile_id}: empty training-state declaration")
        if not binding["admission_status"].startswith(
            ("development_admitted", "historical_development_admission")
        ):
            fail(f"{profile_id}: non-admitted profile is mislabeled in registry")
        profile = registry_by_id[profile_id]
        if not profile["method_semantics_id"] or not profile["problem_semantics_id"]:
            fail(f"{profile_id}: semantic identity is incomplete")
        for key in (
            "additional_contract_paths",
            "additional_oracle_or_test_paths",
        ):
            for relative in binding.get(key, []):
                if not (ROOT / relative).is_file():
                    fail(f"{profile_id}: additional evidence is missing: {relative}")

    blocked = matrix["blocked_original_identities"]
    blocked_ids = {row["guarded_algorithm_id"] for row in blocked}
    required_guards = {"taae", "alga", "rlpso", "rlfode"}
    if not required_guards.issubset(blocked_ids):
        fail("original learned-method guards are incomplete")
    if len(blocked_ids) != len(blocked):
        fail("duplicate guarded original identity")
    for row in blocked:
        if not row["status"].startswith("blocked"):
            fail(f"{row['guarded_algorithm_id']}: original status is not blocked")
        if row["reconstruction_profile_id"] not in binding_by_id:
            fail(f"{row['guarded_algorithm_id']}: reconstruction binding is missing")
        completion_row = completion_by_id[row["corpus_id"]]
        original_state = completion_row["original_reproduction_status"]
        boundary = completion_row["claim_boundary"].lower()
        heterogeneous_original = (
            reproducibility.get("packages", {})
            .get(row["corpus_id"], {})
            .get("original_problem_status", "")
        )
        if (
            "blocked" not in original_state.lower()
            and "blocked" not in boundary
            and "blocked" not in heterogeneous_original.lower()
        ):
            fail(
                f"{row['guarded_algorithm_id']}: completion row lost original blocker"
            )

    positive = negative = auto = 0
    if arguments.build_dir is not None:
        build_dir = arguments.build_dir.resolve()
        positive, negative, auto = audit_runtime(matrix, build_dir)
    print(
        "execution_backend_audit_pass "
        f"papers={len(scoped_dois)} profiles={len(profiles)} "
        f"packages={len(packages)} blocked_originals={len(blocked)} "
        f"runtime_cpu={positive} runtime_fail_closed={negative} "
        f"runtime_auto={auto}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
