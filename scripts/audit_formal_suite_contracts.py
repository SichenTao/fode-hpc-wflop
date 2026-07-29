#!/usr/bin/env python3
"""Audit preserved host suites and the declared-reconstruction formal suite."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
LEGACY_SUITES = (
    ROOT / "formal/contracts/waffle_campaign_suite_v1.json",
    ROOT / "formal/contracts/spark2_campaign_suite_v1.json",
)
DECLARED_SUITE = (
    ROOT / "formal/contracts/declared_reconstruction_formal_suite_v1.json"
)
PROFILE_REGISTRY = ROOT / "shared/contracts/executable_profile_evidence.json"
CAPABILITY_MATRIX = (
    ROOT / "shared/contracts/global_execution_capability_matrix.json"
)
SOURCE_COMMIT = "3237c300b059f1e5ba7a07a4afbebc87059e78ca"
MATRIX_ID = "gao_tao_wflop_global_execution_capability_matrix_step10_v1"
SEED_SET_ID = "declared_reconstruction_25_v1"
LEGACY_ROLES = ("common", "bde", "pbea", "offshore")
COMMON_ALGORITHMS = (
    "fode",
    "aga",
    "sugga",
    "ise",
    "agpso",
    "cgpso",
    "lshade",
    "clshade",
    "cede",
    "msshade",
    "bde",
    "hgpso",
    "aiga",
    "ciga",
    "lsde",
    "wfadde",
    "alshade",
    "ppga",
)
ENVIRONMENT_FIELDS = {
    "hostname",
    "architecture",
    "os_release",
    "cpu_model",
    "visible_logical_cpus",
    "compiler_id",
    "compiler_version",
    "cmake_version",
    "build_type",
    "compiler_flags",
    "link_libraries",
    "binary_sha256",
    "source_commit",
    "git_status_porcelain",
}
RESULT_KEY_FIELDS = {
    "campaign_id",
    "profile_id",
    "problem_semantics_id",
    "case_id",
    "optimization_seed",
    "physical_fes_per_run",
    "binary_sha256",
    "environment_sha256",
    "source_commit",
}
EXPECTED_PROFILES = (
    "alga_attention_declared_reconstruction_v1__fode_e0_common",
    "fqfode_seeded_training_declared_reconstruction_v1__fode_e0_common",
    "alga_attention_declared_reconstruction_v1__alga_guishan_planar_transfer",
    "rlpso_compact_policy_declared_reconstruction_v1__rpso2024_source_problem_ws1_ws4",
    "rlpso_paper_corrected_training_reconstruction_v1__rpso2024_source_problem_ws1_ws4",
    "taae_transformer_evolution_declared_reconstruction_v1__taae_zhangbei_structured_declared_proxy_v1",
    "ppga_nantong_structured_3d_declared_reconstruction_v2__ppga_nantong_structured_3d_declared_proxy_v1",
    "bde__bde2025_ws5_paper250_declared_proxy_v1",
    "bde__bde2025_ws6_paper250_declared_proxy_v1",
    "geoga__admitted_gga_problem_asset_proxy",
    "geoga__geoga_anholt_structured_declared_proxy_v1",
    "tmoea__nysted_paper_eq16_cpu_r4_v2",
    "moead__zhang2025_three_objective",
    "morime__zhang2025_three_objective",
    "armoea__zhang2025_three_objective",
)
EXPECTED_CAMPAIGNS = (
    (
        "declared_fode_common_reconstructions_v1",
        2,
        50,
        24000,
        2500,
        60000000,
    ),
    (
        "declared_alga_guishan_reconstruction_v1",
        1,
        1,
        2430,
        25,
        60750,
    ),
    (
        "declared_rlpso_reconstructions_v1",
        2,
        12,
        24000,
        600,
        14400000,
    ),
    ("declared_taae_reconstruction_v1", 1, 6, 10000, 150, 1500000),
    (
        "declared_ppga_nantong_reconstruction_v1",
        1,
        16,
        1500,
        400,
        600000,
    ),
    ("declared_bde_ws5_reconstruction_v1", 1, 6, 10000, 150, 1500000),
    ("declared_bde_ws6_reconstruction_v1", 1, 6, 10000, 150, 1500000),
    (
        "declared_geoga_gga_proxy_reconstruction_v1",
        1,
        8,
        10000,
        200,
        2000000,
    ),
    (
        "declared_geoga_anholt_reconstruction_v1",
        1,
        1,
        10000,
        25,
        250000,
    ),
    (
        "declared_tmoea_eq16_reconstruction_v1",
        1,
        1,
        3000,
        25,
        75000,
    ),
    (
        "declared_pbea_reconstructions_v1",
        3,
        32,
        10100,
        2400,
        24240000,
    ),
)
EXPECTED_ENVIRONMENT = {
    "environment_contract_id": "spark_9b6f_cpu20_gcc11_release_v1",
    "hostname": "spark-9b6f",
    "architecture": "aarch64",
    "operating_system": "Ubuntu 24.04.4 LTS",
    "operating_system_version_id": "24.04",
    "kernel": "Linux 6.14.0-1015-nvidia aarch64",
    "glibc_version": "2.39-0ubuntu8.7",
    "cpu_model": "10x Cortex-X925 plus 10x Cortex-A725",
    "visible_logical_cpus": 20,
    "online_cpu_list": "0-19",
    "threads_per_core": 1,
    "cmake_version": "3.28.3",
    "compiler": "Ubuntu GCC 11.5.0",
    "build_type": "Release",
    "cxx_flags_release": "-O3 -DNDEBUG",
    "openmp_compile_flag": "-fopenmp",
    "openmp_libraries": ["gomp", "pthread"],
    "libgomp_linker_path": "/usr/lib/gcc/aarch64-linux-gnu/11/libgomp.so",
    "libgomp_resolved_path": "/usr/lib/aarch64-linux-gnu/libgomp.so.1.0.0",
    "libgomp_sha256": (
        "33c8bcb7d33228fe5101bfeb201bcaff4563d4ecf09497229b9deba522ebaf3d"
    ),
    "configure_command": (
        "cmake -S . -B build/full -DCMAKE_BUILD_TYPE=Release"
    ),
    "build_command": "cmake --build build/full -j20",
}
EXPECTED_BINARIES = {
    "wflop_cpp_hpc": {
        "path": "build/full/hpc/wflop_cpp/wflop_cpp_hpc",
        "sha256": (
            "eb203f11cf1301f4e92e510c4c3a3cc54a843d365a8f3bb17e0752186715a46a"
        ),
    },
    "taae_evolution_hpc": {
        "path": "build/full/hpc/taae_cpp/taae_evolution_hpc",
        "sha256": (
            "defedb4b806cfda5b90e0ffe64879d02d0b5b32d60f2ee9c1c7ef4ad28d0c228"
        ),
    },
    "ppga_nantong_hpc": {
        "path": "build/full/hpc/ppga_cpp/ppga_nantong_hpc",
        "sha256": (
            "486781c7e4429b518dd5ba552777d4dacf98de58359aa608a7d9071184947401"
        ),
    },
    "bde_ws56_hpc": {
        "path": "build/full/hpc/bde_ws56_cpp/bde_ws56_hpc",
        "sha256": (
            "344c5f6c4c02ab74512bea4f94b86a2b8606f00cae0a9121502e88e668739faf"
        ),
    },
    "gga_cpp_hpc": {
        "path": "build/full/hpc/gga_cpp/gga_cpp_hpc",
        "sha256": (
            "7b10bbff6e63842417509f612854f2b3ae178cb65d3b30743bde9e562a707552"
        ),
    },
    "geoga_anholt_hpc": {
        "path": "build/full/hpc/geoga_cpp/geoga_anholt_hpc",
        "sha256": (
            "e9da8942fb5db7fc7ada54b1c79285ee62828966d68ff6e5a250c2bc03240cb2"
        ),
    },
    "pbea_cpp_hpc": {
        "path": "build/full/hpc/pbea_cpp/pbea_cpp_hpc",
        "sha256": (
            "efad49458661173c41b48ad0e7666ed56a82bec4ac6962d8b2bc4c7442562f35"
        ),
    },
}


def read_json(path: Path) -> dict:
    return json.loads(path.read_text())


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def audit_legacy_suites() -> None:
    """Preserve every pre-Step-12 host-suite assertion."""
    for suite_path in LEGACY_SUITES:
        suite = read_json(suite_path)
        campaigns = suite["campaigns"]
        require(
            tuple(row["role"] for row in campaigns) == LEGACY_ROLES,
            f"{suite_path.name}: campaign role order differs",
        )
        require(
            len({row["campaign_id"] for row in campaigns}) == len(LEGACY_ROLES),
            f"{suite_path.name}: duplicate campaign identity",
        )
        contracts = []
        for row in campaigns:
            contract = read_json(ROOT / row["contract"])
            require(
                contract["campaign_id"] == row["campaign_id"],
                f"{suite_path.name}: campaign/contract identity differs",
            )
            require(
                int(contract["formal_run_count"])
                == int(row["optimization_runs"]),
                f"{row['campaign_id']}: optimization-run count differs",
            )
            require(
                int(contract["formal_complete_layout_evaluations"])
                == int(row["complete_layout_evaluations"]),
                f"{row['campaign_id']}: physical-evaluation count differs",
            )
            contracts.append(contract)
        require(
            tuple(contracts[0]["algorithms"]) == COMMON_ALGORITHMS,
            f"{suite_path.name}: common algorithm set differs",
        )
        require(
            len(contracts[0]["seeds"]) == 25,
            f"{suite_path.name}: common repeats differ",
        )
        require(
            len(contracts[1]["seeds"]) == 25,
            f"{suite_path.name}: BDE repeats differ",
        )
        require(
            int(contracts[2]["repeat_count"]) == 25,
            f"{suite_path.name}: PBEA repeats differ",
        )
        require(
            int(contracts[3]["repeat_count"]) == 25,
            f"{suite_path.name}: offshore repeats differ",
        )
        require(
            sum(row["optimization_runs"] for row in campaigns)
            == int(suite["total_optimization_runs"]),
            f"{suite_path.name}: suite run total differs",
        )
        require(
            sum(row["complete_layout_evaluations"] for row in campaigns)
            == int(suite["total_complete_layout_evaluations"]),
            f"{suite_path.name}: suite physical-evaluation total differs",
        )
        require(
            set(suite["blocked_methods_excluded"])
            == {"alga", "taae", "rlpso", "rlfode"},
            f"{suite_path.name}: blocked method set differs",
        )


def audit_declared_cross_sources() -> None:
    benchmark = read_json(ROOT / "shared/contracts/benchmark_contract.json")
    require(
        benchmark["canonical_semantics_id"] == "fode_wflop_e0_legacy_v1",
        "FODE canonical semantic drift",
    )
    require(int(benchmark["case_count"]) == 50, "FODE case-count drift")

    alga = read_json(
        ROOT / "shared/contracts/alga_guishan_planar_transfer_cases.json"
    )
    require(int(alga["case_count"]) == 1, "ALGA Guishan case-count drift")

    rpso = read_json(
        ROOT / "shared/contracts/rpso_source_problem_execution_contract.json"
    )
    require(
        int(rpso["case_axes"]["case_count"]) == 12,
        "RLPSO source-problem case-count drift",
    )

    taae = read_json(
        ROOT
        / "shared/contracts/taae_zhangbei_structured_declared_proxy_cases.json"
    )
    require(int(taae["case_count"]) == 6, "TAAE case-count drift")
    taae_contract = read_json(
        ROOT / "formal/contracts/declared_taae_reconstruction_v1.json"
    )
    require(
        taae_contract["case_semantic_hashes"]
        == taae["full_problem_semantic_hash"]["case_hashes"],
        "TAAE per-case semantic hashes drift",
    )

    ppga = read_json(
        ROOT
        / "shared/contracts/ppga_nantong_structured_3d_declared_proxy_cases.json"
    )
    require(
        len(ppga["cases"]) == 16,
        "PPGA manifest must remain WS1-WS4 by tn20/30/40/50 = 16 cases",
    )
    ppga_contract = read_json(
        ROOT / "formal/contracts/declared_ppga_nantong_reconstruction_v1.json"
    )
    require(
        int(ppga_contract["case_count"]) == len(ppga["cases"]),
        "PPGA formal case count differs from authoritative manifest",
    )

    bde = read_json(
        ROOT / "shared/contracts/bde_ws56_declared_proxy_cases.json"
    )
    require(
        sum(row["case_id"].startswith("BDEWS5") for row in bde["cases"]) == 6,
        "BDE WS5 case-count drift",
    )
    require(
        sum(row["case_id"].startswith("BDEWS6") for row in bde["cases"]) == 6,
        "BDE WS6 case-count drift",
    )

    pbea_problem = read_json(
        ROOT / "shared/contracts/pbea_problem_semantics.json"
    )
    require(
        2 * len(pbea_problem["decision_space"]["turbine_counts"]) == 32,
        "PBEA WS1/WS2 by turbine-count case axis drift",
    )


def audit_declared_suite(*, require_binaries: bool) -> None:
    suite = read_json(DECLARED_SUITE)
    matrix = read_json(CAPABILITY_MATRIX)
    registry_rows = read_json(PROFILE_REGISTRY)["profiles"]
    registry = {row["profile_id"]: row for row in registry_rows}
    matrix_profiles = {row["profile_id"] for row in matrix["profile_bindings"]}

    require(
        suite["suite_id"] == "declared_reconstruction_formal_suite_v1",
        "declared suite identity differs",
    )
    require(
        suite["status"] == "contract_frozen_not_launched",
        "declared suite must remain frozen and unlaunched",
    )
    require(suite["source_commit"] == SOURCE_COMMIT, "suite source commit drift")
    require(
        suite["capability_matrix"]["matrix_id"] == MATRIX_ID
        and matrix["matrix_id"] == MATRIX_ID,
        "capability matrix identity drift",
    )
    require(
        suite["execution"]
        == {"device_mode": "cpu", "workers": 20, "processes_at_once": 1},
        "suite execution must remain CPU workers=20 processes_at_once=1",
    )
    environment = suite["environment_contract"]
    for field, expected in EXPECTED_ENVIRONMENT.items():
        require(
            environment[field] == expected,
            f"suite environment field {field} differs",
        )
    require(
        environment["canonical_binaries"] == EXPECTED_BINARIES,
        "suite canonical binary paths or SHA256 values differ",
    )
    require(
        environment["launch_preconditions"]
        == {
            "clean_worktree": True,
            "rebuild_from_source_commit": SOURCE_COMMIT,
            "binary_sha256_recomputed_after_build": True,
            "environment_receipt_revalidated": True,
        },
        "suite launch preconditions differ",
    )
    for binary in EXPECTED_BINARIES.values():
        binary_path = ROOT / binary["path"]
        if binary_path.is_file():
            require(
                sha256_file(binary_path) == binary["sha256"],
                f"{binary_path}: frozen binary SHA256 differs",
            )
        elif require_binaries:
            raise RuntimeError(f"{binary_path}: frozen binary missing")
    seeds = suite["optimization_seed_sets"][SEED_SET_ID]
    require(
        len(seeds) == 25 and len(set(seeds)) == 25,
        "declared optimization seeds must be 25 fixed unique values",
    )
    require(
        tuple(suite["included_profile_ids"]) == EXPECTED_PROFILES,
        "declared profile order or membership drift",
    )
    require(
        set(EXPECTED_PROFILES) <= matrix_profiles,
        "declared profile missing from capability matrix",
    )
    require(
        int(suite["profile_count"]) == len(EXPECTED_PROFILES),
        "suite profile count differs",
    )
    require(
        int(suite["campaign_count"]) == len(EXPECTED_CAMPAIGNS),
        "suite campaign count differs",
    )
    require(
        "training_work_and_training_fes_are_excluded_from_optimization_fes"
        not in suite["scientific_invariants"],
        "suite contains the obsolete training-FES exclusion invariant",
    )
    rlpso_accounting = suite["training_work_accounting"][
        "rlpso_total_physical_fes_equation"
    ]
    require(
        int(
            rlpso_accounting[
                "training_physical_fes_plus_inference_physical_fes"
            ]
        )
        == 24000
        and rlpso_accounting[
            "online_training_evaluator_calls_count_inside_total"
        ]
        is True,
        "suite does not freeze the RLPSO online training FES equation",
    )

    rows = suite["campaigns"]
    require(
        tuple(row["campaign_id"] for row in rows)
        == tuple(item[0] for item in EXPECTED_CAMPAIGNS),
        "declared campaign order or membership drift",
    )
    require(
        suite["campaign_contracts"]
        == [row["contract"] for row in rows],
        "suite contract list differs from campaign rows",
    )

    observed_profiles: list[str] = []
    contracts = []
    for row, expected in zip(rows, EXPECTED_CAMPAIGNS):
        (
            campaign_id,
            profile_count,
            case_count,
            physical_fes,
            run_count,
            layout_evaluations,
        ) = expected
        contract_path = ROOT / row["contract"]
        require(contract_path.is_file(), f"{campaign_id}: contract missing")
        contract = read_json(contract_path)
        contracts.append(contract)

        require(
            contract["campaign_id"] == campaign_id
            and contract["suite_id"] == suite["suite_id"],
            f"{campaign_id}: identity differs",
        )
        require(
            contract["status"] == "contract_frozen_not_launched",
            f"{campaign_id}: contract must remain frozen and unlaunched",
        )
        require(
            contract["source_commit"] == SOURCE_COMMIT
            and contract["capability_matrix_id"] == MATRIX_ID,
            f"{campaign_id}: source or matrix identity differs",
        )
        require(
            len(contract["profile_ids"]) == profile_count,
            f"{campaign_id}: profile count differs",
        )
        require(
            int(contract["case_count"]) == case_count,
            f"{campaign_id}: case count differs",
        )
        require(
            int(contract["physical_fes_per_run"]) == physical_fes,
            f"{campaign_id}: physical FES differs",
        )
        require(
            int(contract["formal_run_count"]) == run_count
            == profile_count * case_count * len(seeds),
            f"{campaign_id}: run-count equation differs",
        )
        require(
            int(contract["formal_complete_layout_evaluations"])
            == layout_evaluations
            == run_count * physical_fes,
            f"{campaign_id}: physical-evaluation equation differs",
        )
        require(
            all(
                int(row[key]) == value
                for key, value in (
                    ("profile_count", profile_count),
                    ("case_count", case_count),
                    ("physical_fes_per_run", physical_fes),
                    ("optimization_runs", run_count),
                    ("complete_layout_evaluations", layout_evaluations),
                )
            ),
            f"{campaign_id}: suite row differs from contract",
        )
        require(
            contract["optimization_seed_set_id"] == SEED_SET_ID
            and int(contract["optimization_seed_count"]) == len(seeds),
            f"{campaign_id}: seed set differs",
        )

        execution = contract["execution"]
        require(
            execution["device_mode"] == "cpu"
            and int(execution["workers"]) == 20
            and int(execution["processes_at_once"]) == 1
            and execution["gpu_used"] is False,
            f"{campaign_id}: CPU20 execution contract differs",
        )
        require(
            execution["binary_source_commit"] == SOURCE_COMMIT
            and execution["clean_worktree_required"] is True
            and execution["environment_receipt_required"] is True,
            f"{campaign_id}: binary/source/environment fail-closed rule differs",
        )
        require(
            execution["environment_contract_id"]
            == EXPECTED_ENVIRONMENT["environment_contract_id"]
            and execution["binary_target"] in EXPECTED_BINARIES
            and execution["binary_relative_path"]
            == EXPECTED_BINARIES[execution["binary_target"]]["path"],
            f"{campaign_id}: suite environment or canonical binary differs",
        )
        require(
            ENVIRONMENT_FIELDS <= set(execution["environment_receipt_fields"]),
            f"{campaign_id}: environment receipt is incomplete",
        )
        require(
            bool(execution["cpu_work_partition"]),
            f"{campaign_id}: CPU work partition is missing",
        )

        records = contract["profile_records"]
        require(
            [record["profile_id"] for record in records]
            == contract["profile_ids"],
            f"{campaign_id}: profile records differ from profile IDs",
        )
        for record in records:
            profile_id = record["profile_id"]
            require(
                profile_id in registry,
                f"{campaign_id}: {profile_id} missing from profile registry",
            )
            canonical = registry[profile_id]
            for field in (
                "method_semantics_id",
                "problem_semantics_id",
                "method_evidence_tier",
                "problem_evidence_tier",
                "claim_boundary",
            ):
                require(
                    record[field] == canonical[field],
                    f"{campaign_id}: {profile_id} {field} differs from registry",
                )
            require(
                record["problem_semantics_id"]
                == contract["semantic_group"]["problem_semantics_id"],
                f"{campaign_id}: distinct problem semantics pooled",
            )
            observed_profiles.append(profile_id)

        training = contract["training_work_separation"]
        require(
            training["optimization_seed_namespace"]
            != training["training_seed_namespace"]
            and training["namespaces_must_be_disjoint"] is True
            and training["training_work_and_training_fes_recorded_separately"]
            is True,
            f"{campaign_id}: training and optimization namespaces/ledgers differ",
        )
        if campaign_id == "declared_rlpso_reconstructions_v1":
            require(
                training["training_fes_included_in_optimization_fes"] is True,
                "RLPSO online training FES must be included in total 24000",
            )
            for training_profile in training["training_profiles"].values():
                require(
                    training_profile["total_physical_fes_equation"]
                    == "training_physical_fes + inference_physical_fes = 24000",
                    "RLPSO training/inference/total FES equation differs",
                )
        else:
            require(
                training["training_fes_included_in_optimization_fes"] is False,
                f"{campaign_id}: offline or zero-FES training ledger differs",
            )

        resume = contract["resume_policy"]
        require(
            RESULT_KEY_FIELDS <= set(resume["result_key_fields"])
            and resume["reuse_only_complete_validated_results"] is True
            and resume["partial_results_are_never_reused"] is True
            and "fsync" in resume["atomic_commit"]
            and "rename" in resume["atomic_commit"],
            f"{campaign_id}: atomic resume contract differs",
        )

    require(
        tuple(observed_profiles) == EXPECTED_PROFILES
        and len(set(observed_profiles)) == len(EXPECTED_PROFILES),
        "profiles are omitted, duplicated, or reordered across campaigns",
    )
    require(
        sum(int(contract["formal_run_count"]) for contract in contracts)
        == int(suite["total_optimization_runs"])
        == 6625,
        "declared suite run total differs",
    )
    require(
        sum(
            int(contract["formal_complete_layout_evaluations"])
            for contract in contracts
        )
        == int(suite["total_campaign_physical_layout_evaluations"])
        == 106125750,
        "declared suite physical-evaluation total differs",
    )
    require(
        int(suite["one_time_offline_training_physical_fes"]) == 39700
        and int(
            suite[
                "total_physical_layout_evaluations_including_one_time_offline_training"
            ]
        )
        == 106165450,
        "declared suite offline-training accounting differs",
    )

    excluded = {
        row["profile_id"] for row in suite["matrix_profiles_deliberately_excluded"]
    }
    require(
        excluded
        == {
            "ppga__fode_e0_common",
            "tmoea__nysted_gga_asset_reconstruction",
        },
        "declared suite exclusion set differs",
    )

    for campaign_id in (
        "declared_taae_reconstruction_v1",
        "declared_tmoea_eq16_reconstruction_v1",
        "declared_pbea_reconstructions_v1",
    ):
        contract = next(
            row for row in contracts if row["campaign_id"] == campaign_id
        )
        reporting = contract["multiobjective_reporting"]
        require(
            reporting["cross_case_pooling"] is False
            and reporting["metrics"]
            and reporting["reference_front"]
            and reporting["normalization"]
            and reporting["hypervolume_reference_point"],
            f"{campaign_id}: multiobjective reporting contract incomplete",
        )

    fqfode = contracts[0]["training_work_separation"]["training_profiles"][
        "fqfode_seeded_training_declared_reconstruction_v1__fode_e0_common"
    ]
    require(
        fqfode["artifact_sha256"]
        == "9d047125f775a940a4c80eff50fa622c30be20b5b3cfcd55aa74f0ff614ba126"
        and int(fqfode["offline_training_physical_fes_once"]) == 39700
        and int(fqfode["inference_physical_fes_per_run"]) == 24000,
        "FQFODE offline-training artifact or FES ledger differs",
    )

    audit_declared_cross_sources()


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--require-binaries",
        action="store_true",
        help=(
            "require every frozen build/full binary and verify its SHA256; "
            "use this launch/evidence gate after the canonical build"
        ),
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    audit_legacy_suites()
    audit_declared_suite(require_binaries=args.require_binaries)
    print(
        "formal_suite_contract_audit_pass "
        "legacy_suites=2 legacy_campaigns=8 "
        "declared_suites=1 declared_campaigns=11 "
        "declared_profiles=15 declared_runs=6625 "
        "declared_complete_layout_evaluations=106125750 "
        "declared_offline_training_layout_evaluations=39700 "
        f"binary_gate={'required_and_verified' if args.require_binaries else 'verify_if_present'}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
