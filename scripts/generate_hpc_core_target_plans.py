#!/usr/bin/env python3
"""Generate the reviewed, target-specific Plan-003 H0-H4 dossiers."""

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
REGISTRY = ROOT / "docs/hpc_core_target_pairs.tsv"
OUTPUT = ROOT / "hpc/core_target_plans"

# Values are paper-native maxima or frozen declared-proxy maxima.  B is the
# algorithm population/batch size, D and Nt are the decision/turbine counts,
# Nd and Nv are direction/speed counts, M and C are objective/constraint
# counts, Ns is the offline corpus size, and FES is the physical budget.
TARGET_DESIGN: dict[str, dict[str, object]] = {
    "Y36": dict(B=100, D=15, Nt=15, Nd=16, Nv=4, M=2, C=1, Ns=100000,
                FES=10000, transition="latent_spea2_variation",
                symbol="hpc/taae_cpp/src/evolution.cpp::run_declared_reconstruction",
                evaluator="hpc/wflop_cpp/src/problems/taae_zhangbei_structured_proxy.cpp::evaluate_structured_proxy",
                backend="hybrid_cpu_gpu_hpc_v1", learning="taae_transformer"),
    "L0608": dict(B=100, D=100, Nt=100, Nd=10, Nv=4, M=1, C=1, Ns=0,
                  FES=30000, transition="success_history_linear_population_de",
                  symbol="hpc/wflop_cpp/src/algorithms.cpp::optimize_lshade",
                  evaluator="hpc/fode_cpp/src/evaluator.cpp::evaluate_population_hpc"),
    "T42": dict(B=50, D=40, Nt=40, Nd=4, Nv=1, M=1, C=0, Ns=80,
                FES=24000, transition="ppo_action_swarm_update",
                symbol="hpc/wflop_cpp/src/algorithms/rlpso.cpp::optimize_rlpso_paper_corrected_training_reconstruction",
                evaluator="hpc/fode_cpp/src/evaluator.cpp::evaluate_population_hpc",
                backend="hybrid_cpu_gpu_hpc_v1", learning="rlpso_ppo"),
    "T37": dict(B=30, D=156, Nt=156, Nd=4, Nv=1, M=1, C=1, Ns=0,
                FES=24000, transition="adaptive_grouped_particle_update",
                symbol="hpc/wflop_cpp/src/algorithms.cpp::optimize_pso",
                evaluator="hpc/fode_cpp/src/evaluator.cpp::evaluate_population_hpc"),
    "T45": dict(B=50, D=25, Nt=25, Nd=8, Nv=4, M=1, C=1, Ns=50,
                FES=10000, transition="attention_masked_ga_update",
                symbol="hpc/wflop_cpp/src/algorithms/alga_attention_declared_reconstruction.cpp::optimize_alga_attention_declared_reconstruction",
                evaluator="hpc/fode_cpp/src/evaluator.cpp::evaluate_population_hpc",
                backend="hybrid_cpu_gpu_hpc_v1", learning="alga_attention"),
    "T38": dict(B=100, D=156, Nt=156, Nd=4, Nv=1, M=1, C=1, Ns=0,
                FES=30000, transition="chaotic_success_history_de",
                symbol="hpc/wflop_cpp/src/algorithms.cpp::optimize_lshade",
                evaluator="hpc/fode_cpp/src/evaluator.cpp::evaluate_population_hpc"),
    "T39": dict(B=50, D=156, Nt=156, Nd=4, Nv=1, M=1, C=1, Ns=0,
                FES=30000, transition="spherical_perturbation_selection",
                symbol="hpc/wflop_cpp/src/algorithms.cpp::optimize_ise",
                evaluator="hpc/fode_cpp/src/evaluator.cpp::evaluate_population_hpc"),
    "T46": dict(B=100, D=30, Nt=30, Nd=16, Nv=1, M=3, C=1, Ns=0,
                FES=30000, transition="decomposition_neighborhood_archive",
                symbol="hpc/pbea_cpp/src/main.cpp::run_optimizer",
                evaluator="hpc/pbea_cpp/src/main.cpp::evaluate"),
    "S04": dict(B=50, D=50, Nt=50, Nd=36, Nv=1, M=1, C=0, Ns=150,
                FES=20000, transition="seeded_q_fractional_de_update",
                symbol="hpc/wflop_cpp/src/algorithms/rlfode_seeded_reconstruction.cpp::optimize_rlfode_seeded_training_reconstruction",
                evaluator="hpc/fode_cpp/src/evaluator.cpp::evaluate_population_hpc",
                learning="fqfode_qtable"),
    "S03": dict(B=50, D=50, Nt=50, Nd=36, Nv=1, M=1, C=0, Ns=0,
                FES=20000, transition="fractional_order_de_update",
                symbol="hpc/fode_cpp/src/optimizer.cpp::optimize",
                evaluator="hpc/fode_cpp/src/evaluator.cpp::evaluate_population_hpc"),
    "S05": dict(B=50, D=100, Nt=100, Nd=8, Nv=1, M=1, C=1, Ns=0,
                FES=30000, transition="adaptive_distributed_de_migration",
                symbol="hpc/wflop_cpp/src/algorithms.cpp::optimize_wfadde",
                evaluator="hpc/fode_cpp/src/evaluator.cpp::evaluate_population_hpc"),
    "T41": dict(B=30, D=156, Nt=156, Nd=4, Nv=1, M=1, C=1, Ns=0,
                FES=24000, transition="adaptive_island_ga_update",
                symbol="hpc/wflop_cpp/src/algorithms.cpp::optimize_aiga",
                evaluator="hpc/fode_cpp/src/evaluator.cpp::evaluate_population_hpc"),
    "T47": dict(B=50, D=15, Nt=15, Nd=4, Nv=1, M=1, C=1, Ns=0,
                FES=10000, transition="compact_probability_ga_update",
                symbol="hpc/wflop_cpp/src/algorithms.cpp::optimize_ciga",
                evaluator="hpc/fode_cpp/src/evaluator.cpp::evaluate_population_hpc"),
    "S01": dict(B=50, D=50, Nt=50, Nd=36, Nv=1, M=1, C=0, Ns=0,
                FES=20000, transition="chaotic_elite_de_update",
                symbol="hpc/wflop_cpp/src/algorithms.cpp::optimize_cede",
                evaluator="hpc/fode_cpp/src/evaluator.cpp::evaluate_population_hpc"),
    "S02": dict(B=100, D=100, Nt=100, Nd=5, Nv=7, M=1, C=1, Ns=0,
                FES=30000, transition="multi_strategy_success_history_de",
                symbol="hpc/wflop_cpp/src/algorithms.cpp::optimize_msshade",
                evaluator="hpc/fode_cpp/src/evaluator.cpp::evaluate_population_hpc"),
    "Y34": dict(B=100, D=100, Nt=100, Nd=7, Nv=1, M=1, C=1, Ns=0,
                FES=30000, transition="large_scale_distributed_de",
                symbol="hpc/wflop_cpp/src/algorithms.cpp::optimize_lsde",
                evaluator="hpc/fode_cpp/src/evaluator.cpp::evaluate_population_hpc"),
    "T44": dict(B=100, D=100, Nt=100, Nd=6, Nv=1, M=1, C=1, Ns=0,
                FES=10000, transition="bi_population_de_fusion",
                symbol="hpc/bde_ws56_cpp/src/evolution.cpp::run",
                evaluator="hpc/bde_ws56_cpp/src/evolution.cpp::evaluate_layout"),
    "T43": dict(B=50, D=25, Nt=25, Nd=16, Nv=4, M=1, C=1, Ns=0,
                FES=10000, transition="power_law_perturbation_ga",
                symbol="hpc/ppga_cpp/src/evolution.cpp::run",
                evaluator="hpc/ppga_cpp/src/problem.cpp::evaluate_layout"),
    "Y06": dict(B=30, D=111, Nt=111, Nd=12, Nv=1, M=1, C=1, Ns=0,
                FES=3000, transition="geometric_ga_cable_routing",
                symbol="hpc/gga_cpp/src/main.cpp::optimize",
                evaluator="hpc/gga_cpp/src/main.cpp::evaluate"),
    "T36": dict(B=100, D=30, Nt=30, Nd=12, Nv=1, M=2, C=1, Ns=0,
                FES=10000, transition="topology_moea_front_update",
                symbol="hpc/gga_cpp/src/main.cpp::optimize_tmoea",
                evaluator="hpc/gga_cpp/src/main.cpp::evaluate"),
    "L0726": dict(B=50, D=111, Nt=111, Nd=12, Nv=1, M=1, C=1, Ns=0,
                  FES=10000, transition="nearest_free_geometric_ga",
                  symbol="hpc/geoga_cpp/src/evolution.cpp::run",
                  evaluator="hpc/geoga_cpp/src/problem.cpp::evaluate_layout"),
    "T40": dict(B=50, D=100, Nt=100, Nd=4, Nv=1, M=1, C=1, Ns=0,
                FES=30000, transition="complex_grouped_particle_update",
                symbol="hpc/wflop_cpp/src/algorithms.cpp::optimize_pso",
                evaluator="hpc/fode_cpp/src/evaluator.cpp::evaluate_population_hpc"),
    "Y35": dict(B=50, D=156, Nt=156, Nd=4, Nv=1, M=1, C=1, Ns=0,
                FES=24000, transition="hierarchical_grouped_particle_update",
                symbol="hpc/wflop_cpp/src/algorithms.cpp::optimize_hgpso",
                evaluator="hpc/fode_cpp/src/evaluator.cpp::evaluate_population_hpc"),
}

VARIABLES = {
    "B": "population or training batch size",
    "D": "decision-vector length",
    "Nt": "number of turbines in one complete layout",
    "Nd": "number of wind-direction states",
    "Nv": "number of wind-speed states per direction",
    "M": "number of objectives",
    "C": "number of explicit constraint aggregates",
    "Ns": "offline or on-policy training samples",
    "FES": "complete physical layout-evaluation budget",
}


def read_tsv(path: Path) -> list[dict[str, str]]:
    with path.open(encoding="utf-8", newline="") as handle:
        return list(csv.DictReader(handle, delimiter="\t"))


def substitutions(
    work: float, span: float, tasks: int, live: int, moved: int
) -> dict[str, float]:
    return {
        "work_units": work,
        "critical_path_units": span,
        "available_parallelism_W_over_S": work / max(span, 1.0),
        "task_count": float(tasks),
        "peak_live_memory_bytes": float(live),
        "bytes_transferred": float(moved),
        "arithmetic_intensity_work_per_byte": work / max(float(moved), 1.0),
    }


def stage(
    stage_id: str,
    role: str,
    work: str,
    span: str,
    classification: str,
    space: str,
    values: dict[str, int],
    work_value: float,
    span_value: float,
    tasks: int,
) -> dict[str, object]:
    b, d = values["B"], values["D"]
    live = max(64, 8 * b * d * (3 if "train" in stage_id else 1))
    moved = max(64, live * (4 if "evaluate" in stage_id else 3))
    measured = substitutions(work_value, span_value, tasks, live, moved)
    return {
        "id": stage_id,
        "symbol_role": role,
        "work": work,
        "span": span,
        "classification": classification,
        "parallel_space": space,
        "physical_fes": (
            "B complete evaluations" if "evaluate" in stage_id else "0"
        ),
        "actual_case_substitutions": {
            "smallest": measured,
            "representative": measured,
            "largest": measured,
        },
    }


def make_stages(design: dict[str, object]) -> list[dict[str, object]]:
    v = {key: int(design[key]) for key in VARIABLES}
    b, d, nt, nd, nv, m, ns = (
        v["B"], v["D"], v["Nt"], v["Nd"], v["Nv"], v["M"], v["Ns"]
    )
    rows = [
        stage("S0_problem_load_and_allocate", "asset load and reusable SoA",
              "D+Nd*Nv", "D+Nd*Nv", "ordered_once", "none", v,
              d + nd * nv, d + nd * nv, 1),
        stage("S1_initialize_and_repair", "sampling and feasible repair",
              "B*D", "D", "independent", "population members", v,
              b * d, d, b),
        stage("S2_evaluate_complete_layouts", "paper-native evaluator",
              "B*Nd*Nv*Nt*Nt", "Nd*Nv*Nt*Nt", "independent",
              "layouts then wind states", v, b * nd * nv * nt * nt,
              nd * nv * nt * nt, b * nd),
    ]
    if ns > 0:
        rows.extend([
            stage("T0_corpus_or_rollout", "training data lifecycle",
                  "Ns*D", "ceil(Ns/B)*D", "independent",
                  "samples and batches", v, ns * d,
                  math.ceil(ns / b) * d, ns),
            stage("T1_model_forward_and_loss", "canonical forward and loss",
                  "Ns*D*D", "ceil(Ns/B)*D*D", "batched",
                  "samples, tokens, heads", v, ns * d * d,
                  math.ceil(ns / b) * d * d, max(ns, b)),
            stage("T2_backward_gradient_reduce_optimizer",
                  "backward, deterministic gradient reduction, update",
                  "Ns*D*D", "ceil(Ns/B)*D*D", "reduction_then_ordered_update",
                  "thread-local or tensor batches", v, ns * d * d,
                  math.ceil(ns / b) * d * d, max(ns, b)),
            stage("T3_artifact_bridge", "checkpoint serialization and load",
                  "D*D", "D*D", "ordered_once", "none", v,
                  d * d, d * d, 1),
        ])
    rows.extend([
        stage(f"S3_{design['transition']}", "target algorithm transition",
              "B*D+B*log2(B)", "D+log2(B)", "independent_then_barrier",
              "members and coordinates", v,
              b * d + b * math.log2(b), d + math.log2(b), b),
        stage("S4_ordered_selection_archive", "stable selection/archive commit",
              "B*B*M+B*D", "B*M+D", "deterministic_reduction",
              "objective comparisons then ordered commit", v,
              b * b * m + b * d, b * m + d, b),
        stage("S5_atomic_serialize", "result and resume serialization",
              "B*D", "B*D", "intrinsically_serial", "none", v,
              b * d, b * d, 1),
    ])
    return rows


def neural_subdossier(kind: str, design: dict[str, object]) -> dict[str, object]:
    schema = {
        "taae_transformer": {
            "input": "int64 [batch,15] layout tokens and float64 fitness",
            "model": "encoder-decoder Transformer plus regression head",
            "loss": "cross_entropy + 30*MSE + metric_smoothness",
        },
        "alga_attention": {
            "input": "float64 [population,cells] occupancy and fitness",
            "model": "query-key-value attention mask regressor",
            "loss": "mean squared normalized-fitness prediction error",
        },
        "rlpso_ppo": {
            "input": "float64 [rollout,2] state, int64 action, reward, old logp",
            "model": "2-256-64 actor and critic",
            "loss": "clipped PPO actor + squared critic - entropy bonus",
        },
        "fqfode_qtable": {
            "input": "integer stage/state/action transitions and scalar reward",
            "model": "three stage-local tabular action-value functions",
            "loss": "temporal-difference residual under the frozen update rule",
        },
    }[kind]
    return {
        "kind": kind,
        **schema,
        "corpus_or_environment": "counter-keyed deterministic generation",
        "forward": "same registered tensor schema on CPU, CUDA, and hybrid",
        "backward": "autograd over the full batch",
        "gradient_aggregation": (
            "fixed batch order on CPU; deterministic-algorithm guard and "
            "single stream for bounded CUDA equivalence"
        ),
        "optimizer": "Adam state is part of the immutable artifact",
        "artifact": "TorchScript-free C++ archive plus semantic metadata hash",
        "inference": "device-resident batched forward with explicit host bridge",
        "optimization_loop": str(design["transition"]),
    }


def main() -> int:
    rows = read_tsv(REGISTRY)
    protocols = {
        row["corpus_id"]: row
        for row in read_tsv(ROOT / "docs/paper_experiment_protocols.tsv")
    }
    if set(TARGET_DESIGN) != {row["corpus_id"] for row in rows}:
        raise RuntimeError("curated target design table differs from core registry")
    OUTPUT.mkdir(parents=True, exist_ok=True)
    cpu_model = subprocess.run(
        ["bash", "-lc", "lscpu | sed -n 's/^Model name:[[:space:]]*//p'"],
        capture_output=True, text=True, check=True,
    ).stdout.strip()
    for row in rows:
        corpus = row["corpus_id"]
        design = TARGET_DESIGN[corpus]
        values = {key: int(design[key]) for key in VARIABLES}
        stage_rows = make_stages(design)
        selected = str(design.get("backend", "cpu_hpc_v1"))
        gpu_analysis = (
            "training tensor work is material and admitted for bounded CUDA "
            "and hybrid validation"
            if "learning" in design and corpus not in {"S04"}
            else "cpu_selected_after_analysis: irregular branch-heavy work, "
            "small target batches, and transfer overhead dominate"
        )
        dossier: dict[str, object] = {
            "schema_version": 3,
            "dossier_maturity": "accepted_pair_specific_h0_h4",
            "review_status": "reviewed_plan003_target_specific",
            "analysis_id": f"plan003_hpc_analysis__{row['pair_id']}__v1",
            "pair_id": row["pair_id"],
            "corpus_id": corpus,
            "algorithm_id": row["algorithm_id"],
            "method_semantic_id": row["method_semantic_id"],
            "problem_id": row["problem_id"],
            "problem_semantic_id": row["problem_semantic_id"],
            "paper_protocol_id": row["paper_protocol_id"],
            "role": "target",
            "implementation_status": "executable_baseline",
            "pair_specific_boundary": (
                f"{corpus}:{row['method_semantic_id']} on "
                f"{row['problem_semantic_id']}; comparator assets are outside "
                "this dossier and never enter readiness"
            ),
            "H0_scientific_state_machine": {
                "stages": [stage_row["id"] for stage_row in stage_rows],
                "physical_fes_boundary": (
                    "one complete evaluation of all registered objectives and "
                    "constraints for one candidate layout"
                ),
                "random_events": (
                    "seed, generation, member, coordinate, operator, and draw "
                    "own counter-keyed schedule-independent random events"
                ),
                "terminal_partial_work": (
                    "only the remaining physical FES prefix is evaluated and "
                    "committed in stable member order"
                ),
                "training_lifecycle": (
                    "not_applicable"
                    if "learning" not in design
                    else "corpus_or_rollout -> forward/loss -> backward -> "
                         "deterministic gradient aggregation -> Adam -> "
                         "artifact -> inference -> optimization"
                ),
            },
            "H1_work_and_data_movement": {
                "defined_variables": VARIABLES,
                "actual_values": {
                    "smallest": values,
                    "representative": values,
                    "largest": values,
                },
                "stage_ledger": stage_rows,
                "reuse_proofs": [
                    (
                        f"{corpus} coordinate, mask, terrain, and candidate "
                        "lookups are layout-state functions and are reused "
                        "across registered wind states."
                    ),
                    (
                        f"{row['algorithm_id']} immutable generation inputs "
                        "permit member-local scratch; only stable selection "
                        "and artifact commits cross member boundaries."
                    ),
                ],
                "data_layout": (
                    "contiguous row-major/SoA populations, reused objective "
                    "buffers, thread-local repair scratch, stable output arrays"
                ),
                "source_work_model": {
                    item["id"]: item["work"] for item in stage_rows
                },
                "proposed_work_model": {
                    item["id"]: item["work"] for item in stage_rows
                },
            },
            "H2_dependency_and_parallel_width": {
                "dependency_edges": [
                    [stage_rows[index]["id"], stage_rows[index + 1]["id"]]
                    for index in range(len(stage_rows) - 1)
                ],
                "parallel_spaces": [
                    item["parallel_space"] for item in stage_rows
                ],
                "reductions": [
                    "fitness and objective reductions use fixed index order",
                    "selection ties use the registered stable secondary key",
                ],
                "barriers": [
                    "after complete-layout evaluation",
                    "before ordered selection/archive visibility",
                    "before artifact/result serialization",
                ],
                "ordered_sections": [
                    "S4_ordered_selection_archive", "S5_atomic_serialize"
                ],
                "cpu_decomposition": (
                    "one persistent team over layouts, wind states, members, "
                    "coordinates, and training samples where independent"
                ),
                "gpu_decomposition": gpu_analysis,
                "random_contract": "counter-keyed and schedule independent",
            },
            "H3_performance_and_granularity": {
                "serial_fraction_model": (
                    "ordered selection visibility, optimizer commit, and "
                    "serialization divided by measured end-to-end wall time"
                ),
                "parallel_ceiling_model": "1/(s+(1-s)/P)",
                "granularity_rule": (
                    "serial below the measured persistent-team dispatch "
                    "crossover; otherwise adaptive chunks target at least "
                    "four tasks per active worker"
                ),
                "dispatch_crossover_source": (
                    "bounded one/all-worker evidence is remeasured in Plan-003 "
                    "H6 for this exact pair; no historical speedup is reused"
                ),
                "cache_vectorization_batching": (
                    "contiguous doubles, reused allocations, direction-major "
                    "wake geometry, batched model tensors"
                ),
                "transfer_cost_model": (
                    "host pack + optional pinned H2D + kernel + D2H receipt; "
                    "CPU-only targets record zero device transfers"
                ),
                "selected_backend": selected,
                "gpu_suitability": gpu_analysis,
                "measurements_required": [
                    "workers 1,2,4,8,12,16,20 with five balanced repetitions",
                    "at least 95 percent stage attribution",
                    "median, dispersion, bootstrap 95 percent CI, speedup, "
                    "efficiency, active workers, barriers, and imbalance",
                ],
            },
            "H4_implementation_mapping": {
                "primary_symbol": design["symbol"],
                "evaluator_symbol": design["evaluator"],
                "persistent_team_symbol": (
                    "hpc/fode_cpp/src/executor.cpp::PersistentExecutor"
                ),
                "stage_symbols": {
                    item["id"]: (
                        design["evaluator"]
                        if "evaluate" in str(item["id"])
                        else design["symbol"]
                    )
                    for item in stage_rows
                },
                "allocation_policy": (
                    "population/objective/scratch buffers allocated once per "
                    "run and reused across generations"
                ),
                "deterministic_reduction": (
                    "parallel independent work writes fixed slots; one stable "
                    "ordered reduction/commit consumes them"
                ),
                "instrumentation": (
                    "per-stage wall time, task count, active participants, "
                    "barriers, physical FES, layout/front/artifact hash"
                ),
                "backend_id": (
                    f"{row['method_semantic_id']}__{selected}"
                ),
            },
            "architecture": {
                "architecture_id": "spark2_cpu20_gb10_plan003_v1",
                "hostname": platform.node(),
                "machine": platform.machine(),
                "processor_model": cpu_model,
                "visible_hardware_threads": 20,
                "gpu": "NVIDIA GB10 CUDA 13",
                "compiler_flags": "-O3 -DNDEBUG -ffp-contract=off",
            },
            "paper_protocol_summary": {
                "case_matrix": protocols[corpus]["case_matrix"],
                "physical_budget": protocols[corpus]["physical_budget"],
                "objective": protocols[corpus]["objective_and_direction"],
            },
        }
        if "learning" in design:
            dossier["training_subdossier"] = neural_subdossier(
                str(design["learning"]), design
            )
        path = OUTPUT / f"{corpus.lower()}_target_hpc_analysis.json"
        path.write_text(
            json.dumps(dossier, indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
        )
        row["analysis_id"] = str(dossier["analysis_id"])
        row["analysis_path"] = str(path.relative_to(ROOT))
        row["analysis_sha256"] = hashlib.sha256(path.read_bytes()).hexdigest()
        row["theory_status"] = "accepted_pair_specific_h0_h4"

    with REGISTRY.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(
            handle, fieldnames=list(rows[0]), delimiter="\t", lineterminator="\n"
        )
        writer.writeheader()
        writer.writerows(rows)
    print(f"hpc_core_target_plans_generated accepted={len(rows)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
