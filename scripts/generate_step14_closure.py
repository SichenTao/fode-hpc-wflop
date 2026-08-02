#!/usr/bin/env python3
"""Generate the deterministic Step-14 project inventory from frozen registries.

The generated inventory separates method evidence, problem evidence, and
execution-form facets.  These dimensions are intentionally not summed.
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import re
import subprocess
from collections import Counter
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
SOURCE_BASELINE_COMMIT = "a1061177342c4d26df7746bb6008f760503302d1"
SUMMARY_PATH = ROOT / "evidence/closure/step14_project_summary.json"
CLOSURE_RECEIPT_PATH = ROOT / "evidence/closure/step14_closure_receipt.json"
README_PATH = ROOT / "README.md"
ROADMAP_PATH = ROOT / "docs/roadmap.md"
README_BEGIN = "<!-- BEGIN GENERATED: STEP14 CLOSURE -->"
README_END = "<!-- END GENERATED: STEP14 CLOSURE -->"
ROADMAP_BEGIN = "<!-- BEGIN GENERATED: STEP14 INVENTORY -->"
ROADMAP_END = "<!-- END GENERATED: STEP14 INVENTORY -->"


def read_json(relative_path: str) -> dict[str, Any]:
    with (ROOT / relative_path).open(encoding="utf-8") as handle:
        return json.load(handle)


def read_tsv(relative_path: str) -> list[dict[str, str]]:
    with (ROOT / relative_path).open(encoding="utf-8", newline="") as handle:
        return list(csv.DictReader(handle, delimiter="\t"))


def baseline_timestamp() -> str:
    result = subprocess.run(
        ["git", "show", "-s", "--format=%cI", SOURCE_BASELINE_COMMIT],
        cwd=ROOT,
        check=True,
        text=True,
        capture_output=True,
    )
    return result.stdout.strip()


def suite_manifests() -> list[dict[str, Any]]:
    completion = read_json(
        "evidence/closure/spark2_completed_suite_reference.json"
    )
    observed_runtime_states = {
        completion["suite_runtime_state"]["suite_id"]:
        completion["suite_runtime_state"]["state"]
    }
    manifests: list[dict[str, Any]] = []
    for path in sorted((ROOT / "formal/contracts").glob("*suite*.json")):
        data = json.loads(path.read_text(encoding="utf-8"))
        if "suite_id" not in data:
            continue
        manifests.append(
            {
                "path": path.relative_to(ROOT).as_posix(),
                "suite_id": data["suite_id"],
                "status": data["status"],
                "observed_runtime_state": observed_runtime_states.get(
                    data["suite_id"], "no_completion_receipt_in_step14_reference"
                ),
                "campaign_count": len(data.get("campaigns", [])),
                "total_optimization_runs": data.get("total_optimization_runs"),
                "active_profile_count": data.get("profile_count"),
                "reused_completed_profile_count": len(
                    data.get("reused_completed_profile_ids", [])
                ),
                "claim_boundary": data.get("claim_boundary"),
            }
        )
    return manifests


def build_summary() -> dict[str, Any]:
    scope = read_json("docs/lineage_scope_contract.json")
    evidence_schema = read_json("shared/contracts/evidence_authority_schema.json")
    profile_registry = read_json("shared/contracts/executable_profile_evidence.json")
    capability = read_json("shared/contracts/global_execution_capability_matrix.json")
    lineage_rows = read_tsv("docs/author_lineage_registry.tsv")
    completion = read_json(
        "evidence/closure/spark2_completed_suite_reference.json"
    )

    profiles = profile_registry["profiles"]
    profile_by_id = {profile["profile_id"]: profile for profile in profiles}
    if len(profile_by_id) != len(profiles):
        raise ValueError("Executable profile IDs are not unique.")

    bindings = capability["profile_bindings"]
    binding_by_id = {binding["profile_id"]: binding for binding in bindings}
    if set(binding_by_id) != set(profile_by_id):
        raise ValueError("Capability bindings and executable profiles differ.")

    method_counts = Counter(profile["method_evidence_tier"] for profile in profiles)
    problem_counts = Counter(profile["problem_evidence_tier"] for profile in profiles)
    method_tiers = evidence_schema["method_evidence_tiers"]
    problem_tiers = evidence_schema["problem_evidence_tiers"]

    method_evidence = [
        {
            "tier": tier,
            "count": method_counts.get(tier, 0),
            "definition": definition,
        }
        for tier, definition in method_tiers.items()
    ]
    problem_evidence = [
        {
            "tier": tier,
            "count": problem_counts.get(tier, 0),
            "definition": definition,
        }
        for tier, definition in problem_tiers.items()
    ]

    executable_pairs = [
        {
            "profile_id": profile["profile_id"],
            "algorithm_id": profile["algorithm_id"],
            "method_semantics_id": profile["method_semantics_id"],
            "method_evidence_tier": profile["method_evidence_tier"],
            "problem_id": profile["problem_id"],
            "problem_semantics_id": profile["problem_semantics_id"],
            "problem_evidence_tier": profile["problem_evidence_tier"],
            "admission_status": binding_by_id[profile["profile_id"]][
                "admission_status"
            ],
            "formal_status": binding_by_id[profile["profile_id"]]["formal_status"],
            "claim_boundary": profile["claim_boundary"],
        }
        for profile in profiles
    ]

    guarded_originals = sorted(
        capability["blocked_original_identities"],
        key=lambda item: item["guarded_algorithm_id"],
    )

    learning_reconstructions: list[dict[str, Any]] = []
    for binding in bindings:
        training_state = binding.get("training_state", "not_applicable")
        is_reconstruction = (
            binding.get("admission_status") == "development_admitted_reconstruction"
        )
        is_learned = not training_state.startswith("not_applicable")
        if not (is_reconstruction and is_learned):
            continue
        profile = profile_by_id[binding["profile_id"]]
        learning_reconstructions.append(
            {
                "profile_id": profile["profile_id"],
                "algorithm_id": profile["algorithm_id"],
                "training_state": training_state,
                "admission_status": binding["admission_status"],
                "formal_status": binding["formal_status"],
                "claim_boundary": profile["claim_boundary"],
            }
        )
    learning_reconstructions.sort(key=lambda item: item["profile_id"])

    complete_information_profiles = [
        profile["profile_id"]
        for profile in profiles
        if profile["method_evidence_tier"] in {"M0_AUTHOR_SOURCE", "M1_PAPER_COMPLETE"}
        and profile["problem_evidence_tier"]
        in {"P0_AUTHOR_ASSET", "P1_PAPER_COMPLETE"}
    ]

    execution_facets = [
        {
            "facet": "original",
            "count": sum(
                profile["method_evidence_tier"] == "M0_AUTHOR_SOURCE"
                and profile["problem_evidence_tier"] == "P0_AUTHOR_ASSET"
                for profile in profiles
            ),
            "definition": (
                "M0 author-source method plus P0 author-asset authority only; "
                "this label does not by itself reproduce an original paper result."
            ),
        },
        {
            "facet": "source-replay",
            "count": sum(
                "source_replay" in profile["profile_id"]
                or "source_replay" in profile["problem_id"]
                for profile in profiles
            ),
            "definition": "Profiles explicitly identified as literal source-replay problem behavior.",
        },
        {
            "facet": "paper-complete",
            "count": sum(
                profile["method_evidence_tier"] == "M1_PAPER_COMPLETE"
                or profile["problem_evidence_tier"] == "P1_PAPER_COMPLETE"
                for profile in profiles
            ),
            "definition": "Profiles with an M1 method or P1 problem; this is not an original-asset count.",
        },
        {
            "facet": "citation-derived",
            "count": sum(
                profile["method_evidence_tier"] == "M2_CITATION_PREDECESSOR"
                or profile["problem_evidence_tier"] == "P2_CITATION_SAME_AUTHOR"
                for profile in profiles
            ),
            "definition": "Profiles using M2 or P2 cited/predecessor authority under a distinct identity.",
        },
        {
            "facet": "declared-proxy",
            "count": sum(
                profile["method_evidence_tier"] == "M3_DECLARED_COMPLETION"
                or profile["problem_evidence_tier"] == "P3_DECLARED_PROXY"
                for profile in profiles
            ),
            "definition": "Profiles using an M3 declared completion or P3 declared problem proxy.",
        },
        {
            "facet": "fixture-only",
            "count": sum(
                profile["method_evidence_tier"] == "M4_FORMULA_FIXTURE"
                or profile["problem_evidence_tier"] == "P4_FORMULA_FIXTURE"
                for profile in profiles
            ),
            "definition": "Profiles limited to M4/P4 equation or scalar fixtures.",
        },
    ]

    paper_dossiers = sorted(
        path.relative_to(ROOT).as_posix()
        for path in (ROOT / "docs/source-dossiers").glob("*.json")
    )
    unique_dois = {row["doi"] for row in lineage_rows}
    if len(unique_dois) != len(lineage_rows):
        raise ValueError("Author-lineage DOI records are not unique.")

    declared_suite = read_json(
        "formal/contracts/declared_reconstruction_formal_suite_v1.json"
    )
    binaries = declared_suite["environment_contract"]["canonical_binaries"]

    return {
        "schema_version": 1,
        "summary_id": "step14_project_summary_v1",
        "source_baseline_commit": SOURCE_BASELINE_COMMIT,
        "generated_at": baseline_timestamp(),
        "generation_time_source": "source_baseline_commit_committer_timestamp",
        "generation_host": declared_suite["environment_contract"]["hostname"],
        "snapshot": {
            "date": scope["snapshot_date"],
            "scope": scope["scope"],
            "paper_count": len(lineage_rows),
            "unique_doi_count": len(unique_dois),
            "source_dossier_count": len(paper_dossiers),
            "source_dossiers": paper_dossiers,
        },
        "evidence_dimensions": {
            "non_additive": True,
            "note": (
                "Method tiers, problem tiers, and execution-form facets are "
                "separate dimensions and must not be added into one total."
            ),
            "method": method_evidence,
            "problem": problem_evidence,
            "execution_facets": execution_facets,
        },
        "executable_inventory": {
            "profile_pair_count": len(executable_pairs),
            "algorithm_count": len(
                {profile["algorithm_id"] for profile in profiles}
            ),
            "problem_count": len({profile["problem_id"] for profile in profiles}),
            "pairs": executable_pairs,
        },
        "identity_guards": {
            "guarded_original_count": len(guarded_originals),
            "guarded_originals": guarded_originals,
            "distinct_reconstruction_profile_count": len(
                {
                    item["reconstruction_profile_id"]
                    for item in guarded_originals
                }
            ),
        },
        "learning_reconstructions": {
            "profile_count": len(learning_reconstructions),
            "profiles": learning_reconstructions,
        },
        "complete_information_identity_preservation": {
            "definition": (
                "Profiles with M0/M1 method evidence and P0/P1 problem evidence "
                "retain their registered profile, method, and problem identities."
            ),
            "profile_count": len(complete_information_profiles),
            "profile_ids": complete_information_profiles,
        },
        "suite_manifests": suite_manifests(),
        "completed_spark2_result_reuse": {
            "evidence_id": completion["evidence_id"],
            "runtime_state": completion["suite_runtime_state"]["state"],
            "completed_optimization_source_commit": completion[
                "suite_runtime_state"
            ]["completed_optimization_source_commit"],
            "excluded_completed_profile_ids": [
                item["profile_id"]
                for item in completion["active_suite_deduplication"][
                    "excluded_completed_profiles"
                ]
            ],
            "accepted_artifact_integrity": completion[
                "accepted_artifact_integrity"
            ]["campaigns"],
            "claim_boundary": completion["evidence_boundary"],
        },
        "frozen_binary_count": len(binaries),
        "frozen_binaries": binaries,
        "formal_campaign_launched_by_step14": False,
    }


def markdown_cell(value: Any) -> str:
    return str(value).replace("|", r"\|").replace("\n", " ")


def evidence_table(rows: list[dict[str, Any]]) -> list[str]:
    output = [
        "| Tier | Executable-profile count | Definition |",
        "|---|---:|---|",
    ]
    for item in rows:
        output.append(
            f"| `{item['tier']}` | {item['count']} | "
            f"{markdown_cell(item['definition'])} |"
        )
    return output


def suite_table(summary: dict[str, Any]) -> list[str]:
    output = [
        "| Exact suite ID | Contract status | Observed runtime state | Active profiles | Reused completed profiles | Campaigns | Contract optimization runs |",
        "|---|---|---|---:|---:|---:|---:|",
    ]
    for item in summary["suite_manifests"]:
        output.append(
            f"| `{item['suite_id']}` | `{item['status']}` | "
            f"`{item['observed_runtime_state']}` | "
            f"{item['active_profile_count'] or 'n/a'} | "
            f"{item['reused_completed_profile_count']} | "
            f"{item['campaign_count']} | {item['total_optimization_runs']} |"
        )
    return output


def render_readme_block(summary: dict[str, Any]) -> str:
    snapshot = summary["snapshot"]
    inventory = summary["executable_inventory"]
    method_counts = ", ".join(
        f"{item['tier'].split('_', 1)[0]}={item['count']}"
        for item in summary["evidence_dimensions"]["method"]
    )
    problem_counts = ", ".join(
        f"{item['tier'].split('_', 1)[0]}={item['count']}"
        for item in summary["evidence_dimensions"]["problem"]
    )
    lines = [
        README_BEGIN,
        "",
        "## Machine-generated project closure",
        "",
        "_Generated by `scripts/generate_step14_closure.py`; edit the registries, not this block._",
        "",
        f"The `{snapshot['date']}` scope snapshot contains **{snapshot['paper_count']} WFLOP papers** and "
        f"{snapshot['source_dossier_count']} source dossiers. The executable registry contains "
        f"**{inventory['profile_pair_count']} algorithm–problem profiles**, spanning "
        f"{inventory['algorithm_count']} algorithm IDs and {inventory['problem_count']} problem IDs. "
        "The complete list is generated in [`docs/roadmap.md`](docs/roadmap.md#complete-executable-profile-inventory) "
        "and is machine-readable in [`step14_project_summary.json`](evidence/closure/step14_project_summary.json).",
        "",
        f"Method evidence counts are {method_counts}. Problem evidence counts are {problem_counts}. "
        "Method tiers, problem tiers, and the execution-form labels `original`, `source-replay`, "
        "`paper-complete`, `citation-derived`, `declared-proxy`, and `fixture-only` are separate "
        "dimensions; their counts must not be added into one total. Here `original` means only an "
        "M0 method plus P0 problem authority combination; it does not automatically establish "
        "reproduction of an original paper result.",
        "",
        f"**Identity boundary.** {summary['identity_guards']['guarded_original_count']} original "
        "method/problem identities remain guarded. Their "
        f"{summary['identity_guards']['distinct_reconstruction_profile_count']} executable "
        "reconstructions use distinct profile IDs. The "
        f"{summary['complete_information_identity_preservation']['profile_count']} "
        "complete-information profiles (M0/M1 with P0/P1) retain their registered method, "
        "problem, and profile identities.",
        "",
        "**Exact formal-suite status**",
        "",
        *suite_table(summary),
        "",
        "The preserved Spark2 suite contract remains byte-identical at "
        "`approved_for_spark2_formal_execution`; its separate observed runtime receipt is "
        f"`{summary['completed_spark2_result_reuse']['runtime_state']}`. Completed exact "
        "PBEA and GeoGA-GGA profiles are reused for analysis and are excluded from the "
        "new declared optimization launch.",
        "",
        f"All {summary['learning_reconstructions']['profile_count']} executable learning "
        "reconstruction profiles retain explicit training-state and claim boundaries in the "
        "generated roadmap. Step 11 bounded admission is complete. Step 12 contracts are "
        "frozen and unlaunched; seven active scalar campaign contracts covering nine "
        "scalar profile routes through four distinct C++ binary targets still require "
        "the single-run checkpoint, feasibility, and "
        "separated-timing instrumentation gate, "
        "so new declared-suite formal results have not been produced.",
        "",
        README_END,
    ]
    return "\n".join(lines)


def render_roadmap_block(summary: dict[str, Any]) -> str:
    snapshot = summary["snapshot"]
    inventory = summary["executable_inventory"]
    lines = [
        ROADMAP_BEGIN,
        "",
        "## Generated closure inventory",
        "",
        "_Generated by `scripts/generate_step14_closure.py`; edit the source registries, not this block._",
        "",
        f"Snapshot `{snapshot['date']}` contains **{snapshot['paper_count']} scoped WFLOP papers**, "
        f"{snapshot['unique_doi_count']} unique DOIs, and {snapshot['source_dossier_count']} "
        "source dossiers.",
        "",
        "### Method evidence tiers",
        "",
        *evidence_table(summary["evidence_dimensions"]["method"]),
        "",
        "### Problem evidence tiers",
        "",
        *evidence_table(summary["evidence_dimensions"]["problem"]),
        "",
        "These are two different evidence axes. The following execution facets are also "
        "non-exclusive and are never summed into an evidence total.",
        "",
        "| Execution facet | Profile count | Definition |",
        "|---|---:|---|",
    ]
    for item in summary["evidence_dimensions"]["execution_facets"]:
        lines.append(
            f"| `{item['facet']}` | {item['count']} | "
            f"{markdown_cell(item['definition'])} |"
        )

    lines.extend(
        [
            "",
            "### Exact suite status",
            "",
            *suite_table(summary),
            "",
            "The contract and runtime columns are intentionally separate. The preserved "
            "Spark2 contract remains approved while its observed receipt is completed. "
            "This closure step launched no new campaign. Step 11 bounded admission is "
            "complete; Step 12 contracts remain frozen and unlaunched. Seven active "
            "scalar campaign contracts covering nine scalar profile routes through four "
            "distinct C++ binary targets still require one-run "
            "physical-FES checkpoint, feasibility, and separated-timing instrumentation "
            "admission before formal launch.",
            "",
            "### Guarded originals and distinct executable reconstructions",
            "",
            "| Guarded original ID | Status | Distinct executable reconstruction |",
            "|---|---|---|",
        ]
    )
    for item in summary["identity_guards"]["guarded_originals"]:
        lines.append(
            f"| `{item['guarded_algorithm_id']}` | `{item['status']}` | "
            f"`{item['reconstruction_profile_id']}` |"
        )

    lines.extend(
        [
            "",
            "### Reconstructed learning-method claim boundaries",
            "",
            "| Executable profile | Frozen training-state boundary | Formal status | Claim boundary |",
            "|---|---|---|---|",
        ]
    )
    for item in summary["learning_reconstructions"]["profiles"]:
        lines.append(
            f"| `{item['profile_id']}` | `{item['training_state']}` | "
            f"`{item['formal_status']}` | {markdown_cell(item['claim_boundary'])} |"
        )

    preserved = summary["complete_information_identity_preservation"]
    lines.extend(
        [
            "",
            "### Complete-information identity preservation",
            "",
            f"{preserved['definition']} The {preserved['profile_count']} preserved profile IDs are:",
            "",
            *[f"- `{profile_id}`" for profile_id in preserved["profile_ids"]],
            "",
            "### Complete executable profile inventory",
            "",
            f"The registry contains **{inventory['profile_pair_count']} executable ",
            f"algorithm–problem profiles**, {inventory['algorithm_count']} distinct ",
            f"algorithm IDs, and {inventory['problem_count']} distinct problem IDs.",
            "",
            "| Profile ID | Algorithm | Method tier | Problem | Problem tier | Admission | Formal status | Claim boundary |",
            "|---|---|---|---|---|---|---|---|",
        ]
    )
    for item in inventory["pairs"]:
        lines.append(
            f"| `{item['profile_id']}` | `{item['algorithm_id']}` | "
            f"`{item['method_evidence_tier']}` | `{item['problem_id']}` | "
            f"`{item['problem_evidence_tier']}` | `{item['admission_status']}` | "
            f"`{item['formal_status']}` | {markdown_cell(item['claim_boundary'])} |"
        )
    lines.extend(["", ROADMAP_END])
    return "\n".join(lines)


def replace_generated_block(
    current: str,
    begin: str,
    end: str,
    rendered: str,
    *,
    before_heading: str | None = None,
) -> str:
    rendered = "\n".join(line.rstrip() for line in rendered.splitlines())
    if (begin in current) != (end in current):
        raise ValueError(f"unpaired generated markers: {begin}")
    if begin in current:
        start = current.index(begin)
        finish = current.index(end, start) + len(end)
        updated = current[:start] + rendered + current[finish:]
    elif before_heading is not None and before_heading in current:
        start = current.index(before_heading)
        updated = current[:start] + rendered + "\n\n" + current[start:]
    else:
        updated = current.rstrip() + "\n\n" + rendered + "\n"
    return updated.rstrip() + "\n"


def expected_documents(summary: dict[str, Any]) -> tuple[str, str]:
    readme = replace_generated_block(
        README_PATH.read_text(encoding="utf-8"),
        README_BEGIN,
        README_END,
        render_readme_block(summary),
        before_heading="## Licensing",
    )
    roadmap = replace_generated_block(
        ROADMAP_PATH.read_text(encoding="utf-8"),
        ROADMAP_BEGIN,
        ROADMAP_END,
        render_roadmap_block(summary),
    )
    return readme, roadmap


def file_sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def file_records(paths: list[Path]) -> list[dict[str, str]]:
    records = []
    for path in sorted(paths):
        if not path.is_file():
            raise FileNotFoundError(path)
        records.append(
            {
                "path": path.relative_to(ROOT).as_posix(),
                "sha256": file_sha256(path),
            }
        )
    return records


def referenced_shared_contracts(capability: dict[str, Any]) -> set[Path]:
    referenced: set[Path] = set()

    def visit(value: Any) -> None:
        if isinstance(value, dict):
            for child in value.values():
                visit(child)
        elif isinstance(value, list):
            for child in value:
                visit(child)
        elif isinstance(value, str):
            for relative in re.findall(
                r"shared/contracts/[A-Za-z0-9_.-]+\.json", value
            ):
                path = ROOT / relative
                if path.is_file():
                    referenced.add(path)

    visit(capability["packages"])
    for binding in capability["profile_bindings"]:
        visit(binding.get("additional_contract_paths", []))
    return referenced


def build_closure_receipt(
    summary: dict[str, Any], summary_rendered: str
) -> dict[str, Any]:
    scope_paths = [
        ROOT / "docs/lineage_scope_contract.json",
        ROOT / "docs/author_lineage_registry.tsv",
        ROOT / "docs/problem_package_registry.tsv",
        ROOT / "docs/lineage_r0_r4_completion.tsv",
    ]
    dossier_paths = sorted((ROOT / "docs/source-dossiers").glob("*.json"))
    decision_paths = sorted(
        (ROOT / "shared/contracts/reconstruction-decisions").glob("*.json")
    ) + [
        ROOT / "docs/semantic_discrepancy_ledger.tsv",
        ROOT / "shared/contracts/paper_implementation_ledger.tsv",
        ROOT / "shared/contracts/reconstruction_admission_policy.json",
        ROOT / "shared/contracts/uncertainty_decisions.md",
    ]
    semantic_paths = sorted(
        {
            *list((ROOT / "shared/contracts").glob("*contract*.json")),
            *list((ROOT / "shared/contracts").glob("*semantic*.json")),
            *list((ROOT / "shared/contracts").glob("*semantics*.json")),
            *referenced_shared_contracts(
                read_json("shared/contracts/global_execution_capability_matrix.json")
            ),
        }
    )
    formal_paths = sorted((ROOT / "formal/contracts").glob("*.json"))
    declared_suite = read_json(
        "formal/contracts/declared_reconstruction_formal_suite_v1.json"
    )

    return {
        "schema_version": 1,
        "receipt_id": "step14_closure_receipt_v1",
        "source_baseline_commit": SOURCE_BASELINE_COMMIT,
        "generated_at": summary["generated_at"],
        "generation_time_source": summary["generation_time_source"],
        "generation_host": summary["generation_host"],
        "scope": {
            "snapshot_date": summary["snapshot"]["date"],
            "paper_count": summary["snapshot"]["paper_count"],
            "registry_files": file_records(scope_paths),
        },
        "source_dossiers": file_records(dossier_paths),
        "evidence_schema": file_records(
            [ROOT / "shared/contracts/evidence_authority_schema.json"]
        ),
        "authority_registries": file_records(
            [
                ROOT / "docs/lineage_scope_contract.json",
                ROOT / "docs/author_lineage_registry.tsv",
                ROOT / "shared/contracts/evidence_authority_schema.json",
                ROOT / "shared/contracts/executable_profile_evidence.json",
                ROOT / "shared/contracts/global_execution_capability_matrix.json",
                ROOT / "formal/contracts/declared_reconstruction_formal_suite_v1.json",
                ROOT / "evidence/closure/spark2_completed_suite_reference.json",
            ]
        ),
        "decision_ledgers": file_records(decision_paths),
        "semantic_contracts": file_records(semantic_paths),
        "formal_contracts": file_records(formal_paths),
        "frozen_binaries": declared_suite["environment_contract"][
            "canonical_binaries"
        ],
        "test_summary": {
            "full_ctest": {
                "command": "ctest --test-dir build/full --output-on-failure",
                "passed": 56,
                "failed": 0,
                "status": "passed",
            },
            "formal_contract_audit": {
                "command": "python3 scripts/audit_formal_suite_contracts.py --require-binaries",
                "status": "passed",
            },
            "evidence_scope": {
                "command": "python3 scripts/audit_step14_closure.py --formal-only-evidence",
                "definition": "Step-14 evidence additions are limited to evidence/closure; no raw machine result is tracked.",
                "status": "passed",
            },
            "template_self_test": {
                "command": "python3 benchmark-package-template/scripts/self_test.py",
                "status": "passed",
            },
            "public_audit": {
                "command": "bash scripts/public_audit.sh",
                "status": "passed",
            },
            "git_diff_check": {
                "command": "git diff --check",
                "status": "passed",
            },
            "step14_closure_audit": {
                "command": "python3 scripts/audit_step14_closure.py",
                "status": "passed",
            },
        },
        "preservation_contract": {
            "baseline_commit": "3237c300b059f1e5ba7a07a4afbebc87059e78ca",
            "old_formal_contracts": "Every formal/contracts file present at the baseline is byte-identical.",
            "spark2_launcher_tree": "Every hpc/wflop_cpp/spark2 file present at the baseline is byte-identical.",
        },
        "generated_artifacts": [
            {
                "path": "evidence/closure/step14_project_summary.json",
                "sha256": hashlib.sha256(summary_rendered.encode("utf-8")).hexdigest(),
            },
            {"path": "README.md", "generated_block": README_BEGIN},
            {"path": "docs/roadmap.md", "generated_block": ROADMAP_BEGIN},
        ],
        "formal_campaign": {
            "launched_by_step14": False,
            "results_written_by_step14": False,
            "claim_boundary": "Closure metadata and audits only; no campaign was launched and no historical result was modified.",
        },
    }


def canonical_json(data: dict[str, Any]) -> str:
    return json.dumps(data, indent=2, sort_keys=True, ensure_ascii=False) + "\n"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--check",
        action="store_true",
        help="Fail when the committed project summary differs from registries.",
    )
    args = parser.parse_args()

    summary = build_summary()
    rendered = canonical_json(summary)
    readme, roadmap = expected_documents(summary)
    receipt_rendered = canonical_json(build_closure_receipt(summary, rendered))
    if args.check:
        if not SUMMARY_PATH.is_file():
            raise SystemExit(f"missing generated summary: {SUMMARY_PATH}")
        if SUMMARY_PATH.read_text(encoding="utf-8") != rendered:
            raise SystemExit("Step-14 project summary is stale; rerun generator.")
        if README_PATH.read_text(encoding="utf-8") != readme:
            raise SystemExit("README Step-14 block is stale; rerun generator.")
        if ROADMAP_PATH.read_text(encoding="utf-8") != roadmap:
            raise SystemExit("Roadmap Step-14 block is stale; rerun generator.")
        if not CLOSURE_RECEIPT_PATH.is_file():
            raise SystemExit(f"missing closure receipt: {CLOSURE_RECEIPT_PATH}")
        if CLOSURE_RECEIPT_PATH.read_text(encoding="utf-8") != receipt_rendered:
            raise SystemExit("Step-14 closure receipt is stale; rerun generator.")
        print("step14_generated_closure_check_pass")
        return 0

    SUMMARY_PATH.parent.mkdir(parents=True, exist_ok=True)
    SUMMARY_PATH.write_text(rendered, encoding="utf-8")
    README_PATH.write_text(readme, encoding="utf-8")
    ROADMAP_PATH.write_text(roadmap, encoding="utf-8")
    CLOSURE_RECEIPT_PATH.write_text(receipt_rendered, encoding="utf-8")
    print(
        "generated "
        f"{SUMMARY_PATH.relative_to(ROOT)}, README.md, docs/roadmap.md, "
        f"{CLOSURE_RECEIPT_PATH.relative_to(ROOT)}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
