#!/usr/bin/env python3
"""Generate pair-specific H0-H4 dossiers for the Gao-Tao paper scope.

The generator composes a method-family state machine with the exact
paper-native problem/protocol identity. It does not upgrade an unavailable
comparator implementation: implementation status and symbol availability are
explicit fields in the required-pair registry and each dossier.
"""

from __future__ import annotations

import csv
import hashlib
import json
import math
import platform
import re
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "hpc/paper_packages"
PAIR_REGISTRY = ROOT / "docs/hpc_required_pairs.tsv"


def read_tsv(path: Path) -> list[dict[str, str]]:
    with path.open(encoding="utf-8", newline="") as handle:
        return list(csv.DictReader(handle, delimiter="\t"))


def slug(value: str) -> str:
    return re.sub(r"[^a-z0-9]+", "_", value.lower()).strip("_")


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


TARGET_ID = {
    "A-LSHADE": "alshade",
    "RLPSO": "rlpso_paper_corrected_training_reconstruction_v1",
    "ALGA": "alga_attention_declared_reconstruction_v1",
    "MOEA/D-P": "moead_p",
    "FQFODE": "fqfode_seeded_training_declared_reconstruction_v1",
    "FODE": "fode",
    "MS-SHADE": "msshade",
    "TAAE": "taae_transformer_evolution_declared_reconstruction_v1",
    "T-MOEA": "tmoea",
}

FAMILY = {
    "fode": "de_fractional",
    "fqfode_seeded_training_declared_reconstruction_v1": "de_q_learning",
    "rlpso_paper_corrected_training_reconstruction_v1": "pso_ppo",
    "alga_attention_declared_reconstruction_v1": "ga_attention",
    "taae_transformer_evolution_declared_reconstruction_v1": "transformer_moea",
    "moead_p": "multiobjective_decomposition",
    "moead": "multiobjective_decomposition",
    "morime": "multiobjective_population",
    "armoea": "multiobjective_population",
    "nsgaii": "multiobjective_population",
    "nsga2": "multiobjective_population",
    "gde3": "multiobjective_population",
    "smpso": "multiobjective_population",
    "spea2": "multiobjective_population",
    "mopso": "multiobjective_population",
    "ppga": "ga_power_law",
    "gga": "ga_cable",
    "tmoea": "multiobjective_cable",
    "geoga": "ga_geometry",
    "pso": "pso",
    "cgpso": "pso",
    "agpso": "pso",
    "hgpso": "pso",
    "glpso": "pso",
    "clpso": "pso",
    "gwo": "gwo",
    "de": "de",
    "lshade": "de",
    "clshade": "de",
    "lshadecnepsin": "de",
    "cjade": "de",
    "scjade": "de",
    "shade": "de",
    "cede": "de",
    "msshade": "de",
    "bde": "de_bipopulation",
    "wfadde": "de",
    "lsde": "de_distributed",
    "alshade": "de",
    "ise": "spherical",
    "se": "spherical",
    "aga": "ga",
    "ga": "ga",
    "sugga": "ga_surrogate",
    "aiga": "ga",
    "ciga": "ga_compact",
    "siga": "ga",
    "algsa": "gravitational",
    "hgsa": "gravitational",
    "saofgde": "de_geometry",
}

IMPLEMENTATION = {
    "fode": "hpc/fode_cpp/src/optimizer.cpp::optimize_fode_hpc",
    "fqfode_seeded_training_declared_reconstruction_v1":
        "hpc/wflop_cpp/src/algorithms/rlfode_seeded_reconstruction.cpp::optimize_rlfode_seeded_training_reconstruction",
    "rlpso_paper_corrected_training_reconstruction_v1":
        "hpc/wflop_cpp/src/algorithms/rlpso.cpp::optimize_rlpso_paper_corrected_training_reconstruction",
    "alga_attention_declared_reconstruction_v1":
        "hpc/wflop_cpp/src/algorithms/alga_attention_declared_reconstruction.cpp::optimize_alga_attention_declared_reconstruction",
    "taae_transformer_evolution_declared_reconstruction_v1":
        "hpc/taae_cpp/src/evolution.cpp::run_declared_reconstruction",
    "moead_p": "hpc/pbea_cpp/src/main.cpp::run_optimizer",
    "moead": "hpc/pbea_cpp/src/main.cpp::run_optimizer",
    "morime": "hpc/pbea_cpp/src/main.cpp::run_morime",
    "armoea": "hpc/pbea_cpp/src/main.cpp::run_armoea",
    "nsgaii": "hpc/pbea_cpp/src/main.cpp::run_nsga2",
    "mopso": "hpc/pbea_cpp/src/main.cpp::run_mopso",
    "ppga": "hpc/ppga_cpp/src/evolution.cpp::run",
    "gga": "hpc/gga_cpp/src/main.cpp::optimize",
    "tmoea": "hpc/gga_cpp/src/main.cpp::optimize_tmoea",
    "geoga": "hpc/geoga_cpp/src/evolution.cpp::run",
}

EXISTING_GENERIC = {
    "aga", "sugga", "ise", "agpso", "cgpso", "lshade", "clshade",
    "cede", "msshade", "bde", "hgpso", "aiga", "ciga", "lsde",
    "wfadde", "alshade", "pso", "de", "shade", "cjade", "scjade",
    "lshadecnepsin", "se", "algsa", "hgsa", "glpso", "clpso", "siga",
}
GENERIC_SYMBOL = {
    "aga": "optimize_ga",
    "sugga": "optimize_ga",
    "ise": "optimize_ise",
    "agpso": "optimize_pso",
    "cgpso": "optimize_pso",
    "lshade": "optimize_lshade",
    "clshade": "optimize_lshade",
    "alshade": "optimize_lshade",
    "cede": "optimize_cede",
    "msshade": "optimize_msshade",
    "bde": "optimize_bde",
    "hgpso": "optimize_hgpso",
    "aiga": "optimize_aiga",
    "siga": "optimize_aiga",
    "ciga": "optimize_ciga",
    "lsde": "optimize_lsde",
    "wfadde": "optimize_wfadde",
    "pso": "optimize_pso_comparator",
    "glpso": "optimize_pso_comparator",
    "clpso": "optimize_pso_comparator",
    "de": "optimize_de_comparator",
    "shade": "optimize_de_comparator",
    "cjade": "optimize_de_comparator",
    "scjade": "optimize_de_comparator",
    "lshadecnepsin": "optimize_de_comparator",
    "se": "optimize_spherical_comparator",
    "algsa": "optimize_gravitational_comparator",
    "hgsa": "optimize_gravitational_comparator",
    "ppga": "optimize_ppga",
}
for algorithm in EXISTING_GENERIC:
    IMPLEMENTATION.setdefault(
        algorithm,
        "hpc/wflop_cpp/src/algorithms.cpp::" + GENERIC_SYMBOL[algorithm],
    )


def implementation_for(
    corpus_id: str, algorithm: str, executable: bool
) -> str:
    if not executable:
        return "planned_unimplemented_native_comparator"
    if corpus_id == "T45" and algorithm == "ppga":
        return "hpc/wflop_cpp/src/algorithms.cpp::optimize_ppga"
    return IMPLEMENTATION.get(
        algorithm, "planned_unimplemented_native_comparator"
    )

HETERO_EXECUTABLE = {
    ("T46", "moead_p"), ("T46", "moead"), ("T46", "morime"),
    ("T46", "armoea"), ("T46", "nsgaii"), ("T46", "mopso"),
    ("T43", "ppga"), ("Y06", "gga"), ("T36", "tmoea"),
    ("L0726", "geoga"),
}

NATIVE_ASSET = {
    "Y36": "shared/contracts/taae_zhangbei_structured_declared_proxy_cases.json",
    "T42": ".source-cache/generated/rpso_source_problem/benchmark_cases.json",
    "T45": "shared/contracts/alga_guishan_3d_declared_proxy_cases.json",
    "T46": "shared/contracts/pbea_execution_contract.json",
    "S04": "shared/contracts/benchmark_cases.json",
    "T44": "shared/contracts/bde_ws56_declared_proxy_cases.json",
    "T43": "shared/contracts/ppga_nantong_structured_3d_declared_proxy_cases.json",
    "Y06": "shared/contracts/gga_problem_semantics.json",
    "T36": "shared/contracts/tmoea_nysted_paper_wake_gga_router_problem.json",
    "L0726": "shared/contracts/geoga_anholt_structured_declared_proxy_case.json",
}


def integer(text: str, fallback: int) -> int:
    values = [int(value) for value in re.findall(r"\d+", text)]
    return max(values) if values else fallback


def workload(protocol: dict[str, str], corpus_id: str) -> dict[str, int]:
    matrix = protocol["case_matrix"]
    budget = integer(protocol["physical_budget"], 10000)
    objective = protocol["objective_and_direction"].lower()
    return {
        "B": 100 if "multiobjective" in objective or corpus_id == "Y36" else 30,
        "D": integer(matrix, 50),
        "Nt": integer(matrix, 50),
        "Nd": 16 if "3d" in matrix.lower() else 10,
        "Nv": 7 if "3d" in matrix.lower() else 4,
        "M": 3 if "three" in objective or "energy-noise-cost" in objective else (
            2 if "biobjective" in objective or "aep;minimize" in objective else 1
        ),
        "C": 1 if "constraint" in objective else 0,
        "Ns": 100000 if corpus_id == "Y36" else 0,
        "FES": budget,
    }


def stages(family: str, learned: bool) -> list[dict[str, object]]:
    common = [
        {
            "id": "S0_initialize",
            "classification": "independent",
            "work": "B*D",
            "span": "D",
            "parallel_space": "population members",
            "physical_fes": "B",
            "symbol_role": "initialization",
        },
        {
            "id": "S1_evaluate",
            "classification": "independent",
            "work": "B*Nd*Nt*Nt*Nv",
            "span": "Nd*Nt*Nt*Nv",
            "parallel_space": "layouts and wind directions",
            "physical_fes": "B per complete population batch",
            "symbol_role": "paper-native evaluator",
        },
    ]
    family_stage = {
        "pso": ("velocity_position_update", "B*D", "D"),
        "pso_ppo": ("ppo_action_and_swarm_update", "B*D+Ns", "D+Ns"),
        "gwo": ("three_leader_position_update", "B*D", "D"),
        "ga": ("selection_crossover_mutation_repair", "B*D", "D"),
        "ga_attention": ("attention_train_mask_variation", "B*B*D", "B*D"),
        "ga_surrogate": ("surrogate_guided_variation", "B*D", "D"),
        "ga_power_law": ("power_law_variation_repair", "B*D", "D"),
        "ga_cable": ("geometry_variation_and_route", "B*(D+D*D)", "D*D"),
        "ga_geometry": ("geometry_mutation_repair", "B*D", "D"),
        "ga_compact": ("compact_distribution_update", "B*D", "D"),
        "de": ("mutation_crossover_repair", "B*D", "D"),
        "de_fractional": ("fractional_history_mutation", "B*D+D*D", "D*D"),
        "de_q_learning": ("q_action_fractional_mutation", "B*D+1", "D"),
        "de_bipopulation": ("bipopulation_fusion_mutation", "B*D", "D"),
        "de_distributed": ("subpopulation_update_migration", "B*D", "D"),
        "de_geometry": ("geometry_aware_de_variation", "B*D", "D"),
        "spherical": ("spherical_coordinate_variation", "B*D", "D"),
        "gravitational": ("all_pair_force_and_update", "B*B*D", "B*D"),
        "multiobjective_decomposition": (
            "neighborhood_variation_scalarization_archive",
            "B*B*M+B*D",
            "B*M+D",
        ),
        "multiobjective_population": (
            "variation_nondominated_sort_archive",
            "B*B*M+B*D",
            "B*M+D",
        ),
        "multiobjective_cable": (
            "topology_variation_front_archive",
            "B*(D*D)+B*B*M",
            "D*D+B*M",
        ),
        "transformer_moea": (
            "encode_train_latent_variation_decode_repair",
            "Ns*D+B*D*D",
            "Ns*D+B*D",
        ),
    }.get(family, ("method_specific_transition", "B*D", "D"))
    common.append(
        {
            "id": "S2_" + family_stage[0],
            "classification": (
                "barrier_delimited"
                if family in {"pso", "pso_ppo", "gwo", "ga_attention"}
                else "independent"
            ),
            "work": family_stage[1],
            "span": family_stage[2],
            "parallel_space": "members, coordinates, or model samples",
            "physical_fes": "0 before offspring evaluation",
            "symbol_role": "algorithm transition",
        }
    )
    common.extend(
        [
            {
                "id": "S3_ordered_commit",
                "classification": "ordered_reduction",
                "work": "B*log2(B)+B*M",
                "span": "B*log2(B)+B*M",
                "parallel_space": "ordered stable commit",
                "physical_fes": "0",
                "symbol_role": "selection archive and state visibility",
            },
            {
                "id": "S4_serialize",
                "classification": "intrinsically_serial",
                "work": "B*D",
                "span": "B*D",
                "parallel_space": "none",
                "physical_fes": "0",
                "symbol_role": "atomic result receipt",
            },
        ]
    )
    if learned:
        common.insert(
            0,
            {
                "id": "T0_train_or_load_artifact",
                "classification": "barrier_delimited",
                "work": "Ns*D",
                "span": "ceil(Ns/B)*D",
                "parallel_space": "training samples and model batches",
                "physical_fes": "contract-specific and separately ledgered",
                "symbol_role": "training lifecycle",
            },
        )
    return common


def evaluate_expression(expression: str, values: dict[str, int]) -> float:
    allowed = {
        **{key: float(value) for key, value in values.items()},
        "log2": math.log2,
        "ceil": math.ceil,
    }
    return float(eval(expression, {"__builtins__": {}}, allowed))


def quantify_stages(
    stage_rows: list[dict[str, object]],
    actual_values: dict[str, dict[str, int]],
) -> None:
    for stage in stage_rows:
        measured: dict[str, dict[str, float]] = {}
        for case_class, values in actual_values.items():
            work = max(1.0, evaluate_expression(str(stage["work"]), values))
            span = max(1.0, evaluate_expression(str(stage["span"]), values))
            if stage["id"] == "S1_evaluate":
                peak = 8.0 * (
                    values["B"] * values["D"]
                    + values["B"] * values["Nd"] * values["Nt"]
                )
                transferred = 8.0 * (
                    values["B"] * values["D"]
                    + 2
                    * values["B"]
                    * values["Nd"]
                    * values["Nt"]
                    * values["Nt"]
                )
            elif str(stage["id"]).startswith("T0_"):
                peak = 8.0 * max(
                    values["Ns"] * values["D"],
                    values["B"] * values["D"] * values["D"],
                    1,
                )
                transferred = 3.0 * peak
            else:
                peak = 8.0 * max(values["B"] * values["D"], 1)
                transferred = 3.0 * peak
            measured[case_class] = {
                "work_units": work,
                "critical_path_units": span,
                "available_parallelism_W_over_S": work / span,
                "task_count": float(
                    values["B"] * values["Nd"]
                    if stage["id"] == "S1_evaluate"
                    else values["B"]
                ),
                "peak_live_memory_bytes": peak,
                "bytes_transferred": transferred,
                "arithmetic_intensity_work_per_byte": work / transferred,
            }
        stage["actual_case_substitutions"] = measured


def lscpu_fields() -> dict[str, str]:
    completed = subprocess.run(
        ["lscpu", "-J"], capture_output=True, text=True, check=True
    )
    rows = json.loads(completed.stdout)["lscpu"]
    return {
        str(row["field"]).rstrip(":"): str(row["data"])
        for row in rows
    }


def main() -> int:
    matrix = {
        row["corpus_id"]: row
        for row in read_tsv(ROOT / "docs/paper_package_completion.tsv")
    }
    protocols = read_tsv(ROOT / "docs/paper_experiment_protocols.tsv")
    scalar_rows = read_tsv(ROOT / "docs/scalar_problem_package_registry.tsv")
    scalar = {row["corpus_id"]: row for row in scalar_rows}
    comparator_rows = read_tsv(ROOT / "docs/scalar_comparator_registry.tsv")
    method_by_algorithm = {
        row["algorithm_id"]: row["method_semantic_id"]
        for row in comparator_rows
    }

    cpu = lscpu_fields()
    architecture = {
        "architecture_id": "spark2_cpu20_aarch64_20260730_v1",
        "hostname": platform.node(),
        "machine": platform.machine(),
        "processor_model": cpu.get("Model name", "unavailable"),
        "visible_hardware_threads": int(cpu["CPU(s)"]),
        "physical_cores": (
            int(cpu["Core(s) per socket"]) * int(cpu["Socket(s)"])
        ),
        "threads_per_core": int(cpu["Thread(s) per core"]),
        "sockets": int(cpu["Socket(s)"]),
        "numa_nodes": int(cpu["NUMA node(s)"]),
        "numa_cpu_map": cpu.get("NUMA node0 CPU(s)", "unavailable"),
        "cache_hierarchy": {
            "L1d": cpu.get("L1d cache", "unavailable"),
            "L1i": cpu.get("L1i cache", "unavailable"),
            "L2": cpu.get("L2 cache", "unavailable"),
            "L3": cpu.get("L3 cache", "unavailable"),
        },
        "vector_instruction_set": (
            "ARM NEON/ASIMD plus SVE/SVE2 visible in lscpu flags"
            if "sve2" in cpu.get("Flags", "")
            else "architecture flags recorded in lscpu"
        ),
        "memory_bandwidth": (
            "not yet measured for this pair; mandatory H6 measurement"
        ),
        "compiler": subprocess.run(
            ["c++", "--version"], capture_output=True, text=True, check=True
        ).stdout.splitlines()[0],
        "flags": "-O3 -DNDEBUG -ffp-contract=off",
        "worker_policy": "workers=0 resolves to all visible hardware threads",
        "affinity_hypothesis": (
            "one persistent team pinned within the single visible NUMA node"
        ),
        "first_touch_hypothesis": (
            "parallel first touch for population-sized and evaluator scratch "
            "buffers that exceed one private cache"
        ),
        "false_sharing_hypothesis": (
            "thread-local scratch and cache-line-separated counters"
        ),
    }
    rows: list[dict[str, str]] = []
    for protocol in protocols:
        corpus_id = protocol["corpus_id"]
        authority = matrix[corpus_id]
        target = TARGET_ID.get(
            authority["target_algorithm"],
            slug(authority["target_algorithm"]).replace("_", ""),
        )
        algorithms = [target]
        target_aliases = {
            target,
            slug(authority["target_algorithm"]).replace("_", ""),
        }
        for value in protocol["comparator_algorithms"].split(";"):
            algorithm = value.strip().lower().replace("-", "")
            algorithm = {"nsga2": "nsgaii", "smpso": "smpso"}.get(
                algorithm, algorithm
            )
            if (
                algorithm
                and algorithm not in target_aliases
                and algorithm not in algorithms
            ):
                algorithms.append(algorithm)
        problem_id = authority["paper_native_problem_id"]
        asset = (
            scalar[corpus_id]["case_contract"]
            if corpus_id in scalar
            else NATIVE_ASSET.get(corpus_id, "unresolved")
        )
        values = workload(protocol, corpus_id)
        for position, algorithm in enumerate(algorithms):
            role = "target" if position == 0 else "comparator"
            pair_id = (
                f"{corpus_id}__{slug(algorithm)}__"
                f"{slug(authority['problem_semantic_id'])}__{role}"
            )
            method_id = (
                authority["method_semantic_id"]
                if role == "target"
                else method_by_algorithm.get(
                    algorithm, f"{algorithm}_paper_comparator_reconstruction_v1"
                )
            )
            if corpus_id in scalar:
                executable = algorithm in EXISTING_GENERIC or algorithm == "fode"
            else:
                executable = (
                    role == "target"
                    or (corpus_id, algorithm) in HETERO_EXECUTABLE
                    or (
                        corpus_id == "T42"
                        and algorithm in {"pso", "de", "cgpso", "agpso"}
                    )
                    or (
                        corpus_id == "T45"
                        and algorithm in {"aga", "sugga", "ppga"}
                    )
                    or (
                        corpus_id == "S04"
                        and (
                            algorithm in EXISTING_GENERIC
                            or algorithm == "fode"
                        )
                    )
                )
            if corpus_id == "Y36":
                executable = role == "target"
            implementation = implementation_for(
                corpus_id, algorithm, executable
            )
            family = FAMILY.get(algorithm, "paper_specific")
            learned = family in {
                "pso_ppo", "ga_attention", "de_q_learning",
                "transformer_moea", "ga_surrogate",
            }
            stage_rows = stages(family, learned)
            actual_values = {
                "smallest": {
                    key: max(1, value // 2) for key, value in values.items()
                },
                "representative": values,
                "largest": {
                    **values,
                    "B": max(values["B"], 120),
                    "D": max(values["D"], values["Nt"]),
                },
            }
            quantify_stages(stage_rows, actual_values)
            dossier = {
                "schema_version": 1,
                "dossier_maturity": "draft_h0_h4_scaffold_unadmitted",
                "analysis_id": f"hpc_analysis__{pair_id}__v1",
                "pair_id": pair_id,
                "corpus_id": corpus_id,
                "paper_doi": authority["doi"],
                "algorithm_id": algorithm,
                "method_semantic_id": method_id,
                "problem_id": problem_id,
                "problem_semantic_id": authority["problem_semantic_id"],
                "paper_protocol_id": protocol["paper_protocol_id"],
                "role": role,
                "implementation_status": (
                    "executable_baseline" if executable
                    else "planned_missing_native_comparator"
                ),
                "H0_scientific_state_machine": {
                    "stages": [stage["id"] for stage in stage_rows],
                    "physical_fes_boundary": (
                        "one complete paper-native objective/constraint "
                        "evaluation of one candidate layout"
                    ),
                    "visibility": (
                        "generation inputs are immutable during independent "
                        "work; stable ordered commit exposes the next state"
                    ),
                },
                "H1_work_and_data_movement": {
                    "defined_variables": {
                        "B": "population or batch size",
                        "D": "decision dimension",
                        "Nt": "turbine count",
                        "Nd": "wind-direction count",
                        "Nv": "wind-speed count",
                        "M": "objective count",
                        "C": "constraint count",
                        "Ns": "training sample count",
                        "FES": "complete physical evaluation budget",
                    },
                    "actual_values": actual_values,
                    "stage_ledger": stage_rows,
                    "reuse_proofs": [
                        (
                            "Coordinate conversion and terrain lookup depend "
                            "only on layout cells and are reused across Nd*Nv "
                            "wind states without changing objective equations."
                        ),
                        (
                            "Wake geometry is independent of speed within one "
                            "direction and is reused across Nv power-curve "
                            "calls while preserving fixed reduction order."
                        ),
                    ],
                    "data_layout": (
                        "contiguous row-major population and per-state arrays; "
                        "thread-local scratch avoids false sharing"
                    ),
                    "source_work_model": {
                        stage["id"]: stage["work"] for stage in stage_rows
                    },
                    "proposed_work_model": {
                        stage["id"]: stage["work"] for stage in stage_rows
                    },
                },
                "H2_dependency_and_parallel_width": {
                    "dependency_edges": [
                        [stage_rows[index]["id"], stage_rows[index + 1]["id"]]
                        for index in range(len(stage_rows) - 1)
                    ],
                    "parallel_spaces": [
                        stage["parallel_space"] for stage in stage_rows
                    ],
                    "ordered_sections": [
                        stage["id"] for stage in stage_rows
                        if stage["classification"] in {
                            "ordered_reduction", "intrinsically_serial"
                        }
                    ],
                    "random_contract": (
                        "counter-keyed event ownership by seed, generation, "
                        "member, coordinate, and draw; schedule independent"
                    ),
                },
                "H3_performance_and_granularity": {
                    "predicted_dominant_stage": "S1_evaluate",
                    "serial_fraction_model": (
                        "(ordered_commit+serialization)/sum(stage_work)"
                    ),
                    "parallel_ceiling_model": (
                        "1/(serial_fraction+(1-serial_fraction)/P)"
                    ),
                    "granularity_rule": (
                        "retain short loops ordered when task count is below "
                        "persistent-team dispatch crossover; heavy layout or "
                        "wind-state work uses all visible workers"
                    ),
                    "measurements_required": [
                        "1,2,4,8,12,16,20 workers",
                        "five balanced-order repetitions",
                        "stage attribution at least 95 percent",
                        "speedup confidence interval and load balance",
                        "measured memory bandwidth and cache behavior",
                    ],
                },
                "H4_implementation_mapping": {
                    "primary_symbol": implementation,
                    "evaluator_symbol": (
                        "paper-package-specific evaluator; scalar packages "
                        "use fode::evaluate_population_hpc"
                    ),
                    "executor_symbol": "fode::PersistentExecutor::parallel_for",
                    "reduction_order": "stable fixed index order",
                    "selected_mapping": (
                        "persistent all-visible-core team with adaptive "
                        "granularity and ordered scientific commit"
                    ),
                    "rejected_mappings": [
                        (
                            "nested seed/process parallelism rejected for "
                            "single-run H_q because it oversubscribes cores"
                        ),
                        (
                            "unordered floating reduction rejected because it "
                            "changes the scientific trajectory"
                        ),
                    ],
                },
                "architecture": architecture,
                "native_asset": asset,
                "claim_boundary": authority["claim_boundary"],
            }
            package = OUT / corpus_id
            docs = package / "docs"
            analysis = package / "analysis"
            docs.mkdir(parents=True, exist_ok=True)
            analysis.mkdir(parents=True, exist_ok=True)
            json_path = analysis / f"{slug(pair_id)}_hpc_analysis.json"
            json_path.write_text(
                json.dumps(dossier, indent=2, sort_keys=True) + "\n",
                encoding="utf-8",
            )
            digest = sha256(json_path)
            md_path = docs / f"{slug(pair_id)}_mathematical_parallel_design.md"
            md_path.write_text(
                "\n".join(
                    [
                        f"# H0-H4 design: {pair_id}",
                        "",
                        f"- Paper: `{authority['doi']}`",
                        f"- Method: `{method_id}`",
                        f"- Problem: `{authority['problem_semantic_id']}`",
                        f"- Protocol: `{protocol['paper_protocol_id']}`",
                        f"- Status: `{dossier['implementation_status']}`",
                        f"- JSON SHA-256: `{digest}`",
                        "",
                        "## H0 state machine",
                        "",
                        " -> ".join(stage["id"] for stage in stage_rows),
                        "",
                        "## H1 work and reuse",
                        "",
                        *[
                            f"- `{stage['id']}`: W={stage['work']}, "
                            f"S={stage['span']}, {stage['classification']}."
                            for stage in stage_rows
                        ],
                        "",
                        "## H2 dependencies",
                        "",
                        (
                            "Independent candidate/model work is "
                            "barrier-delimited before deterministic stable "
                            "selection and state publication."
                        ),
                        "",
                        "## H3 granularity and bound",
                        "",
                        dossier["H3_performance_and_granularity"][
                            "granularity_rule"
                        ],
                        "",
                        "## H4 mapping",
                        "",
                        f"Primary source symbol: `{implementation}`.",
                        "",
                        f"Claim boundary: {authority['claim_boundary']}",
                        "",
                    ]
                ),
                encoding="utf-8",
            )
            rows.append(
                {
                    "pair_id": pair_id,
                    "corpus_id": corpus_id,
                    "role": role,
                    "algorithm_id": algorithm,
                    "method_semantic_id": method_id,
                    "problem_id": problem_id,
                    "problem_semantic_id": authority["problem_semantic_id"],
                    "paper_protocol_id": protocol["paper_protocol_id"],
                    "native_asset": asset,
                    "implementation_status": dossier["implementation_status"],
                    "analysis_id": dossier["analysis_id"],
                    "analysis_path": str(json_path.relative_to(ROOT)),
                    "analysis_sha256": digest,
                    "validation_status": "pending_h5_h6",
                    "theory_status": "draft_h0_h4_scaffold_unadmitted",
                }
            )
    fields = list(rows[0])
    with PAIR_REGISTRY.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=fields, delimiter="\t")
        writer.writeheader()
        writer.writerows(rows)
    print(
        f"hpc_theory_plans_generated pairs={len(rows)} "
        f"executable={sum(row['implementation_status'] == 'executable_baseline' for row in rows)} "
        f"planned_missing={sum(row['implementation_status'] != 'executable_baseline' for row in rows)}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
