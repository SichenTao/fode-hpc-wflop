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
    "L0608": dict(B=450, D=25, Nt=25, Nd=12, Nv=3, M=1, C=1, Ns=0,
                  FES=24000, B_rule="eighteen_d",
                  transition="success_history_linear_population_de",
                  symbol="hpc/wflop_cpp/src/algorithms.cpp::optimize_lshade",
                  evaluator="hpc/fode_cpp/src/evaluator.cpp::evaluate_population_hpc"),
    "T42": dict(B=50, D=40, Nt=40, Nd=4, Nv=1, M=1, C=0, Ns=80,
                FES=24000, transition="ppo_action_swarm_update",
                symbol="hpc/wflop_cpp/src/algorithms/rlpso.cpp::optimize_rlpso_paper_corrected_training_reconstruction",
                evaluator="hpc/fode_cpp/src/evaluator.cpp::evaluate_population_hpc",
                backend="hybrid_cpu_gpu_hpc_v1", learning="rlpso_ppo"),
    "T37": dict(B=120, D=25, Nt=25, Nd=12, Nv=3, M=1, C=1, Ns=0,
                FES=24000, B_rule="fixed",
                transition="adaptive_grouped_particle_update",
                symbol="hpc/wflop_cpp/src/algorithms.cpp::optimize_pso",
                evaluator="hpc/fode_cpp/src/evaluator.cpp::evaluate_population_hpc"),
    "T45": dict(B=30, D=40, Nt=40, Nd=12, Nv=5, M=1, C=1, Ns=30,
                FES=2430, B_rule="fixed",
                transition="attention_masked_ga_update",
                symbol="hpc/wflop_cpp/src/algorithms/alga_attention_declared_reconstruction.cpp::optimize_alga_attention_declared_reconstruction",
                evaluator="hpc/fode_cpp/src/evaluator.cpp::evaluate_population_hpc",
                backend="hybrid_cpu_gpu_hpc_v1", learning="alga_attention"),
    "T38": dict(B=450, D=25, Nt=25, Nd=6, Nv=1, M=1, C=1, Ns=0,
                FES=20000, B_rule="eighteen_d",
                transition="chaotic_success_history_de",
                symbol="hpc/wflop_cpp/src/algorithms.cpp::optimize_lshade",
                evaluator="hpc/fode_cpp/src/evaluator.cpp::evaluate_population_hpc"),
    "T39": dict(B=5, D=25, Nt=25, Nd=6, Nv=1, M=1, C=1, Ns=0,
                FES=20000, B_rule="fixed",
                transition="spherical_perturbation_selection",
                symbol="hpc/wflop_cpp/src/algorithms.cpp::optimize_ise",
                evaluator="hpc/fode_cpp/src/evaluator.cpp::evaluate_population_hpc"),
    "T46": dict(B=100, D=30, Nt=30, Nd=12, Nv=6, M=3, C=1, Ns=0,
                FES=10100, B_rule="fixed",
                transition="decomposition_neighborhood_archive",
                symbol="hpc/pbea_cpp/src/main.cpp::run_optimizer",
                evaluator="hpc/pbea_cpp/src/main.cpp::evaluate"),
    "S04": dict(B=27, D=50, Nt=50, Nd=10, Nv=13, M=1, C=0, Ns=150,
                FES=24000, B_rule="abs77d",
                transition="seeded_q_fractional_de_update",
                symbol="hpc/wflop_cpp/src/algorithms/rlfode_seeded_reconstruction.cpp::optimize_rlfode_seeded_training_reconstruction",
                evaluator="hpc/fode_cpp/src/evaluator.cpp::evaluate_population_hpc",
                learning="fqfode_qtable"),
    "S03": dict(B=27, D=50, Nt=50, Nd=10, Nv=13, M=1, C=0, Ns=0,
                FES=24000, B_rule="abs77d",
                transition="fractional_order_de_update",
                symbol="hpc/fode_cpp/src/optimizer.cpp::optimize_fode_hpc_controlled",
                evaluator="hpc/fode_cpp/src/evaluator.cpp::evaluate_population_hpc"),
    "S05": dict(B=1800, D=100, Nt=100, Nd=8, Nv=13, M=1, C=1, Ns=0,
                FES=24000, B_rule="eighteen_d",
                transition="adaptive_distributed_de_migration",
                symbol="hpc/wflop_cpp/src/algorithms.cpp::optimize_wfadde",
                evaluator="hpc/fode_cpp/src/evaluator.cpp::evaluate_population_hpc"),
    "T41": dict(B=60, D=25, Nt=25, Nd=12, Nv=3, M=1, C=1, Ns=0,
                FES=24000, B_rule="fixed",
                transition="adaptive_island_ga_update",
                symbol="hpc/wflop_cpp/src/algorithms.cpp::optimize_aiga",
                evaluator="hpc/fode_cpp/src/evaluator.cpp::evaluate_population_hpc"),
    "T47": dict(B=60, D=15, Nt=15, Nd=12, Nv=3, M=1, C=1, Ns=0,
                FES=24000, B_rule="fixed",
                transition="compact_probability_ga_update",
                symbol="hpc/wflop_cpp/src/algorithms.cpp::optimize_ciga",
                evaluator="hpc/fode_cpp/src/evaluator.cpp::evaluate_population_hpc"),
    "S01": dict(B=27, D=50, Nt=50, Nd=10, Nv=13, M=1, C=0, Ns=0,
                FES=24000, B_rule="abs77d",
                transition="competitive_elite_de_update",
                symbol="hpc/wflop_cpp/src/algorithms.cpp::optimize_cede",
                evaluator="hpc/fode_cpp/src/evaluator.cpp::evaluate_population_hpc"),
    "S02": dict(B=25, D=50, Nt=50, Nd=5, Nv=13, M=1, C=1, Ns=0,
                FES=24000, B_rule="half_d",
                transition="multi_strategy_success_history_de",
                symbol="hpc/wflop_cpp/src/algorithms.cpp::optimize_msshade",
                evaluator="hpc/fode_cpp/src/evaluator.cpp::evaluate_population_hpc"),
    "Y34": dict(B=120, D=100, Nt=100, Nd=7, Nv=13, M=1, C=1, Ns=0,
                FES=20000, B_rule="fixed",
                transition="large_scale_distributed_de",
                symbol="hpc/wflop_cpp/src/algorithms.cpp::optimize_lsde",
                evaluator="hpc/fode_cpp/src/evaluator.cpp::evaluate_population_hpc"),
    "T44": dict(B=50, D=40, Nt=40, Nd=16, Nv=8, M=1, C=1, Ns=0,
                FES=10000, B_rule="fixed",
                transition="bi_population_de_fusion",
                symbol="hpc/bde_ws56_cpp/src/evolution.cpp::run",
                evaluator="hpc/bde_ws56_cpp/src/evolution.cpp::evaluate_layout"),
    "T43": dict(B=30, D=50, Nt=50, Nd=16, Nv=7, M=1, C=1, Ns=0,
                FES=1500, B_rule="fixed",
                transition="power_law_perturbation_ga",
                symbol="hpc/ppga_cpp/src/evolution.cpp::run",
                evaluator="hpc/ppga_cpp/src/problem.cpp::evaluate_layout"),
    "Y06": dict(B=30, D=175, Nt=175, Nd=12, Nv=4, M=1, C=1, Ns=0,
                FES=3000, B_rule="fixed",
                transition="geometric_ga_cable_routing",
                symbol="hpc/gga_cpp/src/main.cpp::optimize",
                evaluator="hpc/gga_cpp/src/main.cpp::evaluate"),
    "T36": dict(B=30, D=72, Nt=72, Nd=12, Nv=4, M=2, C=1, Ns=0,
                FES=3000, B_rule="fixed",
                transition="topology_moea_front_update",
                symbol="hpc/gga_cpp/src/main.cpp::optimize_tmoea",
                evaluator="hpc/gga_cpp/src/main.cpp::evaluate"),
    "L0726": dict(B=50, D=111, Nt=111, Nd=12, Nv=1, M=1, C=1, Ns=0,
                  FES=10000, B_rule="fixed",
                  transition="nearest_free_geometric_ga",
                  symbol="hpc/geoga_cpp/src/evolution.cpp::run",
                  evaluator="hpc/geoga_cpp/src/problem.cpp::evaluate_layout"),
    "T40": dict(B=120, D=100, Nt=100, Nd=12, Nv=6, M=1, C=1, Ns=0,
                FES=24000, B_rule="fixed",
                transition="complex_grouped_particle_update",
                symbol="hpc/wflop_cpp/src/algorithms.cpp::optimize_pso",
                evaluator="hpc/fode_cpp/src/evaluator.cpp::evaluate_population_hpc"),
    "Y35": dict(B=120, D=25, Nt=25, Nd=12, Nv=3, M=1, C=1, Ns=0,
                FES=24000, B_rule="fixed",
                transition="hierarchical_grouped_particle_update",
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

# Each sequence is the actual target state transition at the source-symbol
# granularity available in this checkout.  A stage may map to the enclosing
# target function when the method implements that equation inline.
METHOD_STAGES: dict[str, list[tuple[str, str, str, str, str, str, str, str]]] = {
    "Y36": [
        ("T0_layout_corpus", "training corpus", "Ns*D", "ceil(Ns/B)*D", "batched", "layout samples", "hpc/taae_cpp/src/model.cpp::deterministic_layout_corpus", "unique layout tokens are generated and stably shuffled"),
        ("T1_transformer_pretrain", "forward, loss, backward, Adam", "Ns*D*D", "ceil(Ns/B)*D*D", "batched_reduction", "samples and attention heads", "hpc/taae_cpp/src/model.cpp::pretrain", "cross entropy pretraining through TransformerAutoencoder::train_batch"),
        ("T2_checkpoint_bridge", "artifact save and load", "D*D", "D*D", "ordered_once", "parameter registry", "hpc/taae_cpp/src/model.cpp::TransformerAutoencoder::save_checkpoint", "V2 checkpoint contains semantic ID, work ledger, Adam age, and hashes"),
        ("S0_initial_population", "uniform feasible initialization", "B*D", "D", "independent", "population members", "hpc/taae_cpp/src/evolution.cpp::initialize_population", "one hundred unique sorted layouts"),
        ("S1_complete_energy_noise_evaluation", "two objectives and constraint", "B*Nd*Nv*Nt*Nt", "Nd*Nv*Nt*Nt", "independent", "layouts and wind states", "hpc/taae_cpp/src/evolution.cpp::evaluate_population", "reciprocal power, A-weighted noise, cost violation"),
        ("S2_spea2_relative_fitness", "strength, density, normalization", "B*B*M", "B*M", "deterministic_reduction", "objective pairs", "hpc/taae_cpp/src/evolution.cpp::assign_spea2_relative_fitness", "SPEA2 strength plus kth-neighbor density"),
        ("S3_latent_de_decode_repair", "latent DE and decoded repair", "B*D*D", "D*D", "speculate_then_ordered_commit", "offspring proposals", "hpc/taae_cpp/src/evolution.cpp::generate_offspring", "F=0.3, one forced mutant dimension, bounded polynomial mutation"),
        ("S4_cdp_environmental_selection", "CDP plus NSGA-II survival", "B*B*M", "B*M", "deterministic_reduction", "parent-offspring objective pairs", "hpc/taae_cpp/src/evolution.cpp::environmental_selection", "feasibility, Pareto rank, crowding, stable source index"),
    ],
    "L0608": [
        ("S0_alshade_initialization", "18D initial population", "B*D", "D", "independent", "members", "hpc/wflop_cpp/src/algorithms.cpp::optimize_lshade", "Ninit=18D, Nmin=4"),
        ("S1_alshade_current_to_pbest", "current-to-pbest/1 mutation", "B*D", "D", "independent", "members and coordinates", "hpc/wflop_cpp/src/algorithms.cpp::optimize_lshade", "v=x+F*(pbest-x)+F*(r1-r2), p=0.11"),
        ("S2_alshade_complex_wake", "complex-wake complete evaluation", "B*Nd*Nv*Nt*Nt", "Nd*Nv*Nt*Nt", "independent", "layouts and wind states", "hpc/fode_cpp/src/evaluator.cpp::evaluate_population_hpc", "all native direction-speed tuples"),
        ("S3_alshade_success_memory", "weighted Lehmer memories", "B+D", "D", "ordered_reduction", "successful trials", "hpc/wflop_cpp/src/algorithms.cpp::optimize_lshade", "H=5 success-history F and CR memories"),
        ("S4_alshade_linear_reduction", "population and archive reduction", "B*log2(B)+B*D", "B*log2(B)", "ordered_commit", "ranked members", "hpc/wflop_cpp/src/algorithms.cpp::trim_archive", "linear population reduction with archive rate 1.4"),
    ],
    "T42": [
        ("T0_rlpso_rollout", "on-policy environment rollout", "Ns*D", "D", "ordered_interactions", "PPO transitions", "hpc/wflop_cpp/src/algorithms/rlpso.cpp::optimize_rlpso_training_reconstruction", "state r1,r2, four actions, every reward consumes one physical FES"),
        ("T1_rlpso_actor_critic_forward", "actor and critic inference", "Ns*D*D", "D*D", "batched_candidate", "rollout states", "hpc/wflop_cpp/src/algorithms/ppo.cpp::SeededPpo::evaluate", "2-256-64 actor and critic"),
        ("T2_rlpso_clipped_ppo_update", "clipped actor, critic, Adam", "Ns*D*D", "D*D", "ordered_optimizer", "rollout samples", "hpc/wflop_cpp/src/algorithms/ppo.cpp::SeededPpo::update", "gamma=.99, epsilon=.2, K=80, Adam"),
        ("S0_rlpso_swarm_initialize", "population and pbest initialization", "B*D", "D", "independent", "particles", "hpc/wflop_cpp/src/algorithms/rlpso.cpp::initialize", "population 50 and zero velocities"),
        ("S1_rlpso_complete_evaluation", "paper-source complete evaluator", "B*Nd*Nv*Nt*Nt", "Nd*Nv*Nt*Nt", "independent", "layouts and wind states", "hpc/fode_cpp/src/evaluator.cpp::evaluate_population_hpc", "training and inference FES share one exact ledger"),
        ("S2_rlpso_action_swarm_transition", "action-conditioned coefficients", "B*D", "D", "independent", "particles and coordinates", "hpc/wflop_cpp/src/algorithms/rlpso.cpp::optimize_rlpso_paper_corrected_training_reconstruction", "paper action step 0.001 then swarm update"),
        ("S3_rlpso_pbest_gbest_commit", "personal/global best reduction", "B*D", "B", "ordered_reduction", "particles", "hpc/wflop_cpp/src/algorithms/rlpso.cpp::optimize_rlpso_training_reconstruction", "strict fitness improvement and stable particle order"),
    ],
    "T37": [
        ("S0_agpso_population", "paper population initialization", "B*D", "D", "independent", "particles", "hpc/wflop_cpp/src/algorithms.cpp::optimize_pso", "paper population 120"),
        ("S1_agpso_staged_velocity", "pbest-only staged velocity", "B*D", "D", "independent_then_barrier", "particles and coordinates", "hpc/wflop_cpp/src/algorithms.cpp::optimize_pso", "v=c*r*(pbest-x), c=1.49618, no inertia"),
        ("S2_agpso_landuse_evaluation", "land-use complete evaluator", "B*Nd*Nv*Nt*Nt", "Nd*Nv*Nt*Nt", "independent", "layouts and wind states", "hpc/fode_cpp/src/evaluator.cpp::evaluate_population_hpc", "156 native land-use cells"),
        ("S3_agpso_tournament_mutation", "stagnation tournament mutation", "B*D", "D", "independent", "stagnant particles", "hpc/wflop_cpp/src/algorithms.cpp::optimize_pso", "gap 7, mutation .01, tournament round(.2B)"),
        ("S4_agpso_survival", "generation-entry improvement survival", "B*D", "B", "ordered_commit", "particles", "hpc/wflop_cpp/src/algorithms.cpp::optimize_pso", "retain update only on strict improvement"),
    ],
    "T45": [
        ("S0_alga_population", "30-layout initialization", "B*D", "D", "independent", "individuals", "hpc/wflop_cpp/src/algorithms/alga_attention_declared_reconstruction.cpp::initialize_population", "paper population 30"),
        ("T0_alga_attention_forward", "eight-head scaled attention", "B*B*D", "B*D", "batched", "individual pairs and heads", "hpc/wflop_cpp/src/algorithms/alga_attention_declared_reconstruction.cpp::attention_forward", "softmax(QK/sqrt(d))V"),
        ("T1_alga_backward_update", "full-batch MSE gradient update", "B*B*D", "B*D", "ordered_gradient_reduction", "individual pairs", "hpc/wflop_cpp/src/algorithms/alga_attention_declared_reconstruction.cpp::train_one_full_batch_step", "one deterministic gradient-descent update per generation"),
        ("S1_alga_terrain_wake_evaluation", "3D terrain Gaussian wake", "B*Nd*Nv*Nt*Nt", "Nd*Nv*Nt*Nt", "independent", "layouts and wind states", "hpc/fode_cpp/src/evaluator.cpp::evaluate_population_hpc", "terrain-adjusted Gaussian RSS wake"),
        ("S2_alga_masked_variation", "attention mask replacement", "B*D", "D", "independent", "24 non-elites", "hpc/wflop_cpp/src/algorithms/alga_attention_declared_reconstruction.cpp::cell_attention_scores", "replace ceil(.1D) lowest-attention genes from elite sequence"),
        ("S3_alga_elite_survival", "six-elite ordered survival", "B*log2(B)", "B*log2(B)", "ordered_commit", "individuals", "hpc/wflop_cpp/src/algorithms/alga_attention_declared_reconstruction.cpp::stable_rank_descending", "six current elites plus 24 evaluated offspring"),
    ],
    "T38": [
        ("S0_clshade_initialization", "18D initialization", "B*D", "D", "independent", "members", "hpc/wflop_cpp/src/algorithms.cpp::optimize_lshade", "Ninit=18D, Nmin=4"),
        ("S1_clshade_chaotic_map_choice", "CLS-M map-memory choice", "B*D", "D", "independent", "members", "hpc/wflop_cpp/src/algorithms.cpp::advance_chaos", "twelve maps, learning period 50"),
        ("S2_clshade_landuse_evaluation", "land-use complete evaluator", "B*Nd*Nv*Nt*Nt", "Nd*Nv*Nt*Nt", "independent", "layouts and wind states", "hpc/fode_cpp/src/evaluator.cpp::evaluate_population_hpc", "117 native cases"),
        ("S3_clshade_success_memory", "F/CR and map-success reduction", "B+D", "D", "ordered_reduction", "successful trials", "hpc/wflop_cpp/src/algorithms.cpp::optimize_lshade", "p=.11, H=5, shrink=.988"),
        ("S4_clshade_population_reduction", "linear population/archive commit", "B*log2(B)+B*D", "B*log2(B)", "ordered_commit", "ranked members", "hpc/wflop_cpp/src/algorithms.cpp::trim_archive", "Nmin=4 and archive rate 1.4"),
    ],
    "T39": [
        ("S0_ise_five_solutions", "five-member initialization", "B*D", "D", "independent", "members", "hpc/wflop_cpp/src/algorithms.cpp::optimize_ise", "N=5"),
        ("S1_ise_spherical_subspace", "spherical perturbation", "B*D", "D", "independent", "members and selected coordinates", "hpc/wflop_cpp/src/algorithms.cpp::optimize_ise", "scale F=10, selected dimension floor(D/3)"),
        ("S2_ise_landuse_evaluation", "154m land-use evaluator", "B*Nd*Nv*Nt*Nt", "Nd*Nv*Nt*Nt", "independent", "layouts and wind states", "hpc/fode_cpp/src/evaluator.cpp::evaluate_population_hpc", "117 native P1-P3 cases"),
        ("S3_ise_greedy_replacement", "target-trial selection", "B*D", "B", "ordered_commit", "members", "hpc/wflop_cpp/src/algorithms.cpp::optimize_ise", "strict expected-power improvement"),
        ("S4_ise_terminal_prefix", "exact partial terminal batch", "B*D", "D", "independent_then_commit", "remaining members", "hpc/wflop_cpp/src/algorithms.cpp::finish_result", "no evaluation beyond FES"),
    ],
    "T46": [
        ("S0_moead_weight_lattice", "weight and neighborhood construction", "B*B*M", "B*M", "ordered_once", "weights", "hpc/pbea_cpp/src/main.cpp::make_weights", "resolution 98 and neighborhood size 10"),
        ("S1_moead_probability_layout", "IPD-guided initialization/repair", "B*D", "D", "independent", "subproblems", "hpc/pbea_cpp/src/main.cpp::initialize_layout", "six registered initial probability distributions"),
        ("S2_moead_three_objective_evaluation", "power, land, cost evaluator", "B*Nd*Nv*Nt*Nt", "Nd*Nv*Nt*Nt", "independent", "layouts and wind states", "hpc/pbea_cpp/src/main.cpp::evaluate", "inverse power, land area, total cost"),
        ("S3_moead_de_offspring", "DE crossover and probability guidance", "B*D", "D", "independent", "subproblems", "hpc/pbea_cpp/src/main.cpp::make_offspring", "F=.5, CR=.9, polynomial mutation index 20"),
        ("S4_moead_neighborhood_replace", "Tchebycheff replacement", "B*B*M", "B*M", "ordered_commit", "neighborhoods", "hpc/pbea_cpp/src/main.cpp::weighted_tchebycheff", "probability .9 and at most two replacements"),
    ],
    "S04": [
        ("T0_fqfode_shared_training", "multi-agent Q-table training", "Ns*B*D", "D", "independent_agents_then_reduce", "agents and stages", "hpc/wflop_cpp/src/algorithms/rlfode_seeded_reconstruction.cpp::train_artifact", "three stage tables, declared seeded episodes"),
        ("T1_fqfode_q_update", "temporal-difference update", "Ns", "1", "ordered_per_agent", "state-action entries", "hpc/wflop_cpp/src/algorithms/rlfode_seeded_reconstruction.cpp::q_update", "Q <- Q + alpha*(r+gamma*maxQ'-Q)"),
        ("T2_fqfode_artifact_load", "immutable Q-table load", "Ns", "Ns", "ordered_once", "table entries", "hpc/wflop_cpp/src/algorithms/rlfode_seeded_reconstruction.cpp::load_artifact", "artifact hash and semantic profile checked"),
        ("S0_fqfode_fractional_history", "fractional-history mutant", "B*D*D", "D*D", "independent", "members and history", "hpc/wflop_cpp/src/algorithms/rlfode_seeded_reconstruction.cpp::fractional_history_value", "stage-local q chooses additive fractional-order action"),
        ("S1_fqfode_complete_evaluation", "FODE common evaluator", "B*Nd*Nv*Nt*Nt", "Nd*Nv*Nt*Nt", "independent", "layouts and wind states", "hpc/fode_cpp/src/evaluator.cpp::evaluate_population_hpc", "training and optimization FES are separate"),
        ("S2_fqfode_online_policy_commit", "online stage Q update", "B+D", "D", "ordered_reduction", "successful trials", "hpc/wflop_cpp/src/algorithms/rlfode_seeded_reconstruction.cpp::optimize_rlfode_seeded_training_reconstruction", "schedule-independent action events"),
    ],
    "S03": [
        ("S0_fode_dynamic_population", "dimension-dependent initialization", "B*D", "D", "independent", "members", "hpc/fode_cpp/src/optimizer.cpp::initialize_population", "Ninit=max(3,abs(77-D)), Nmin=min(4,Ninit)"),
        ("S1_fode_fractional_mutation", "Grunwald-Letnikov history mutation", "B*D*D", "D*D", "independent", "members, coordinates, history", "hpc/fode_cpp/src/optimizer.cpp::fractional_value", "fractional coefficient a=.8 over archived differences"),
        ("S2_fode_complete_evaluation", "FODE E0 complete evaluator", "B*Nd*Nv*Nt*Nt", "Nd*Nv*Nt*Nt", "independent", "layouts and wind states", "hpc/fode_cpp/src/evaluator.cpp::evaluate_population_hpc", "one complete expected-power call per FES"),
        ("S3_fode_success_archive", "success selection and archive", "B*D", "B", "ordered_commit", "members", "hpc/fode_cpp/src/optimizer.cpp::update_archive", "strict selection and archive rate 1.4"),
        ("S4_fode_linear_reduction", "population survivor reduction", "B*log2(B)+B*D", "B*log2(B)", "ordered_commit", "ranked rows", "hpc/fode_cpp/src/optimizer.cpp::survivor_rows", "terminal prefix retained before size reduction"),
    ],
    "S05": [
        ("S0_wfadde_initialization", "18D population initialization", "B*D", "D", "independent", "members", "hpc/wflop_cpp/src/algorithms.cpp::optimize_wfadde", "Ninit=18D and Nmin=4"),
        ("S1_wfadde_adaptive_mutation", "distributed adaptive DE mutation", "B*D", "D", "independent", "members and coordinates", "hpc/wflop_cpp/src/algorithms.cpp::optimize_wfadde", "success-conditioned mutation and failure archive"),
        ("S2_wfadde_weibull_evaluation", "multi-speed complete evaluator", "B*Nd*Nv*Nt*Nt", "Nd*Nv*Nt*Nt", "independent", "layouts, directions, speeds", "hpc/fode_cpp/src/evaluator.cpp::evaluate_population_hpc", "24 native declared cases"),
        ("S3_wfadde_failure_archive", "failure archive and success rate", "B*D+B", "D", "ordered_reduction", "failed/successful trials", "hpc/wflop_cpp/src/algorithms.cpp::optimize_wfadde", "archive and H=5 F/CR memories"),
        ("S4_wfadde_population_reduction", "adaptive linear reduction", "B*log2(B)+B*D", "B*log2(B)", "ordered_commit", "ranked members", "hpc/wflop_cpp/src/algorithms.cpp::trim_archive", "physical-FES-normalized target population"),
    ],
    "T41": [
        ("S0_aiga_sixty_population", "60-member initialization", "B*D", "D", "independent", "individuals", "hpc/wflop_cpp/src/algorithms.cpp::optimize_aiga", "paper-derived island population"),
        ("S1_aiga_turbine_order", "per-turbine contribution ranking", "B*Nd*Nv*Nt*Nt", "Nd*Nv*Nt*Nt", "independent", "layouts and turbines", "hpc/fode_cpp/src/evaluator.cpp::evaluate_population_hpc", "TotalAndPerTurbine detail"),
        ("S2_aiga_island_crossover", "island crossover and mutation", "B*D", "D", "independent", "islands and coordinates", "hpc/wflop_cpp/src/algorithms.cpp::optimize_aiga", "adaptive island exchange using turbine order"),
        ("S3_aiga_landuse_evaluation", "156-case complete evaluator", "B*Nd*Nv*Nt*Nt", "Nd*Nv*Nt*Nt", "independent", "offspring and wind states", "hpc/fode_cpp/src/evaluator.cpp::evaluate_population_hpc", "complete expected power"),
        ("S4_aiga_elitist_survival", "parent-offspring survival", "B*log2(B)+B*D", "B*log2(B)", "ordered_commit", "combined population", "hpc/wflop_cpp/src/algorithms.cpp::stable_rank_descending", "stable descending fitness"),
    ],
    "T47": [
        ("S0_ciga_sixty_population", "60-member initialization", "B*D", "D", "independent", "individuals", "hpc/wflop_cpp/src/algorithms.cpp::optimize_ciga", "paper-derived population"),
        ("S1_ciga_compact_probability", "compact distribution update", "B*D", "D", "ordered_reduction", "cell probabilities", "hpc/wflop_cpp/src/algorithms.cpp::optimize_ciga", "elite occupancy updates cell probability"),
        ("S2_ciga_chaotic_sampling", "chaos-driven compact sampling", "B*D", "D", "independent", "individuals and cells", "hpc/wflop_cpp/src/algorithms.cpp::optimize_ciga", "logistic-like chaos state and uniqueness repair"),
        ("S3_ciga_complete_evaluation", "four-condition complete evaluator", "B*Nd*Nv*Nt*Nt", "Nd*Nv*Nt*Nt", "independent", "layouts and wind states", "hpc/fode_cpp/src/evaluator.cpp::evaluate_population_hpc", "four declared native conditions"),
        ("S4_ciga_probability_commit", "elite probability commit", "B*D", "D", "ordered_commit", "ranked individuals", "hpc/wflop_cpp/src/algorithms.cpp::stable_rank_descending", "stable elite update"),
    ],
    "S01": [
        ("S0_cede_dynamic_population", "dimension-dependent initialization", "B*D", "D", "independent", "members", "hpc/wflop_cpp/src/algorithms.cpp::optimize_cede", "Ninit=max(4,abs(77-D))"),
        ("S1_cede_competitive_genetic_learning", "random and current-to-pbest trials", "2*B*D", "D", "independent", "two trials per target", "hpc/wflop_cpp/src/algorithms.cpp::optimize_cede", "competitive Eq.18-21 branch after both trials are evaluated"),
        ("S2_cede_complete_evaluation", "complete E0 evaluator", "2*B*Nd*Nv*Nt*Nt", "Nd*Nv*Nt*Nt", "independent", "trial layouts and wind states", "hpc/fode_cpp/src/evaluator.cpp::evaluate_population_hpc", "every actual trial evaluation enters FES"),
        ("S3_cede_winner_archive", "winner/loser competitive commit", "2*B*D", "B", "ordered_commit", "targets and trials", "hpc/wflop_cpp/src/algorithms.cpp::optimize_cede", "strict winner plus LSHADE archive"),
        ("S4_cede_success_memory_reduction", "F/CR memory and linear reduction", "B*log2(B)+B*D", "B*log2(B)", "ordered_reduction", "successful trials", "hpc/wflop_cpp/src/algorithms.cpp::trim_archive", "H=5, p=.11, archive rate 1.4"),
    ],
    "S02": [
        ("S0_msshade_half_dimension_population", "half-D initialization", "B*D", "D", "independent", "members", "hpc/wflop_cpp/src/algorithms.cpp::optimize_msshade", "N=max(4,round(.5D))"),
        ("S1_msshade_multi_strategy_mutation", "strategy ensemble trial construction", "B*D", "D", "independent", "members and coordinates", "hpc/wflop_cpp/src/algorithms.cpp::optimize_msshade", "success-history F/CR with multiple donor strategies"),
        ("S2_msshade_weibull_evaluation", "Weibull-bin complete evaluator", "B*Nd*Nv*Nt*Nt", "Nd*Nv*Nt*Nt", "independent", "layouts, directions, 13 speeds", "hpc/fode_cpp/src/evaluator.cpp::evaluate_population_hpc", "16 reconstructed native cases"),
        ("S3_msshade_archive_selection", "trial selection and archive", "B*D", "B", "ordered_commit", "members", "hpc/wflop_cpp/src/algorithms.cpp::optimize_msshade", "strict selection with external archive"),
        ("S4_msshade_memory_update", "weighted success-history update", "B+D", "D", "ordered_reduction", "successful parameters", "hpc/wflop_cpp/src/algorithms.cpp::optimize_msshade", "H=5 F/CR memories"),
    ],
    "Y34": [
        ("S0_lsde_fixed_population", "120-member initialization", "B*D", "D", "independent", "members", "hpc/wflop_cpp/src/algorithms.cpp::optimize_lsde", "fixed Ninit=120"),
        ("S1_lsde_large_scale_partition", "dimension-block distributed mutation", "B*D+B*D*D", "D*D", "independent", "members and coordinate blocks", "hpc/wflop_cpp/src/algorithms.cpp::optimize_lsde", "large-scale coordinate grouping with archive donors"),
        ("S2_lsde_multispeed_evaluation", "large-farm complete evaluator", "B*Nd*Nv*Nt*Nt", "Nd*Nv*Nt*Nt", "independent", "layouts, directions, 13 speeds", "hpc/fode_cpp/src/evaluator.cpp::evaluate_population_hpc", "12 declared 15x15 cases"),
        ("S3_lsde_success_archive", "success memory and archive", "B*D+B", "D", "ordered_reduction", "successful trials", "hpc/wflop_cpp/src/algorithms.cpp::optimize_lsde", "H=5 and archive rate 1.4"),
        ("S4_lsde_dimension_floor_reduction", "population reduction to ceil(.3D)", "B*log2(B)+B*D", "B*log2(B)", "ordered_commit", "ranked members", "hpc/wflop_cpp/src/algorithms.cpp::trim_archive", "minimum=max(4,ceil(.3D))"),
    ],
    "T44": [
        ("S0_bde_bip_population", "two 25-member populations", "B*D", "D", "independent", "members", "hpc/bde_ws56_cpp/src/evolution.cpp::run", "population 50 split equally"),
        ("S1_bde_dual_mutation", "subpopulation-specific DE mutation", "B*D", "D", "independent", "members and coordinates", "hpc/bde_ws56_cpp/src/evolution.cpp::run", "distinct donor pools followed by fusion"),
        ("S2_bde_standard_terrain_evaluation", "standard/Daegwallyeong evaluator", "B*Nd*Nv*Nt*Nt", "Nd*Nv*Nt*Nt", "independent", "layouts and wind states", "hpc/bde_ws56_cpp/src/evolution.cpp::evaluate_layout", "source WS1-4 and declared WS5-6 stay distinct"),
        ("S3_bde_fusion_exchange", "bi-population fusion", "B*D", "D", "barrier_then_independent", "paired members", "hpc/bde_ws56_cpp/src/evolution.cpp::run", "ordered exchange after both subpopulation evaluations"),
        ("S4_bde_elitist_commit", "stable best survival", "B*log2(B)+B*D", "B*log2(B)", "ordered_commit", "combined members", "hpc/bde_ws56_cpp/src/evolution.cpp::stable_rank_descending", "fixed tie order and exact terminal prefix"),
    ],
    "T43": [
        ("S0_ppga_thirty_population", "30-layout initialization/repair", "B*D", "D", "independent", "individuals", "hpc/ppga_cpp/src/evolution.cpp::initial_layout", "paper population 30"),
        ("S1_ppga_stagnation_diversity", "stagnation and diversity", "B*B*D", "B*D", "deterministic_reduction", "layout pairs", "hpc/ppga_cpp/src/evolution.cpp::offspring_parent_stagnant_proportion", "S fraction and pairwise diversity"),
        ("S2_ppga_power_law_perturbation", "Eq.18 gated perturbation", "B*D", "D", "independent", "offspring coordinates", "hpc/ppga_cpp/src/evolution.cpp::perturb_every_dimension_unrepaired", "finite-support power-law step on every selected dimension"),
        ("S3_ppga_3d_complete_evaluation", "terrain Gaussian wake evaluator", "B*Nd*Nv*Nt*Nt", "Nd*Nv*Nt*Nt", "independent", "layouts and 16x7 wind states", "hpc/ppga_cpp/src/problem.cpp::evaluate_layout", "complete structured Nantong 3D evaluation"),
        ("S4_ppga_parent_offspring_survival", "elitist stable survival", "B*log2(B)+B*D", "B*log2(B)", "ordered_commit", "parents and offspring", "hpc/ppga_cpp/src/evolution.cpp::update_best", "stable parent/offspring comparison"),
    ],
    "Y06": [
        ("S0_gga_candidate_initialization", "site candidate sampling", "B*D", "D", "independent", "individuals", "hpc/gga_cpp/src/main.cpp::random_layout", "unique indices in frozen Poisson candidate set"),
        ("S1_gga_geometry_mutation", "nearest-candidate geometric mutation", "B*D*D", "D*D", "independent", "offspring and candidates", "hpc/gga_cpp/src/main.cpp::repair", "geometry-aware replacement and uniqueness repair"),
        ("S2_gga_wake_cable_lcoe", "wake, routing, and LCOE evaluator", "B*(Nd*Nv*Nt*Nt+D*D)", "Nd*Nv*Nt*Nt+D*D", "independent", "layouts, winds, routing groups", "hpc/gga_cpp/src/main.cpp::evaluate", "RSS Jensen AEP plus balanced cable routes"),
        ("S3_gga_group_tree_routing", "balanced group MST pricing", "B*D*D", "D*D", "independent", "layout groups", "hpc/gga_cpp/src/main.cpp::priced_tree", "subtree-load cable capacity and price"),
        ("S4_gga_elitist_replacement", "parent-offspring LCOE survival", "B*log2(B)+B*D", "B*log2(B)", "ordered_commit", "combined population", "hpc/gga_cpp/src/main.cpp::optimize", "minimize LCOE with stable layout tie"),
    ],
    "T36": [
        ("S0_tmoea_ordered_layout", "ordered 72-of-130 initialization", "B*D", "D", "independent", "individuals", "hpc/gga_cpp/src/main.cpp::random_ordered_layout", "genotype order active; phenotype unique"),
        ("S1_tmoea_biobjective_evaluation", "negative AEP and cable cost", "B*(Nd*Nv*Nt*Nt+D*D)", "Nd*Nv*Nt*Nt+D*D", "independent", "layouts, winds, cable groups", "hpc/gga_cpp/src/main.cpp::tmoea_objectives", "paper Eq.16 wake plus same-author cable completion"),
        ("S2_tmoea_topology_mutation", "topology-aware mutation", "B*D*D", "D*D", "independent", "offspring positions", "hpc/gga_cpp/src/main.cpp::tmoea_topology_mutation", "ordered swap/replacement under cable topology"),
        ("S3_tmoea_rank_crowding", "biobjective nondomination/crowding", "B*B*M", "B*M", "deterministic_reduction", "objective pairs", "hpc/gga_cpp/src/main.cpp::bi_rank_and_crowding", "stable Pareto rank and crowding"),
        ("S4_tmoea_replacement", "topology replacement commit", "B*B*M+B*D", "B*M+D", "ordered_commit", "ranked combined population", "hpc/gga_cpp/src/main.cpp::tmoea_replacement_candidate", "paper Eq.16 profile identity retained"),
    ],
    "L0726": [
        ("S0_geoga_poisson_candidates", "deterministic candidate generation", "D*D", "D", "ordered_once", "candidate set", "hpc/geoga_cpp/src/problem.cpp::generate_candidates", "capped Bridson sampling to 180 candidates"),
        ("S1_geoga_random_layout", "50-layout initialization", "B*D", "D", "independent", "individuals", "hpc/geoga_cpp/src/evolution.cpp::random_layout", "111 unique candidate indices"),
        ("S2_geoga_nearest_free_mutation", "five-nearest free mutation", "B*D*D", "D*D", "independent", "offspring and candidates", "hpc/geoga_cpp/src/evolution.cpp::nearest_free_replacement", "one turbine replaced uniformly among five nearest free"),
        ("S3_geoga_anholt_proxy_evaluation", "12-bin AEP evaluator", "B*Nd*Nv*Nt*Nt", "Nd*Nv*Nt*Nt", "independent", "layouts and wind bins", "hpc/geoga_cpp/src/problem.cpp::evaluate_layout", "Jensen RSS and declared Anholt-structured proxy"),
        ("S4_geoga_best50_survival", "parent-offspring best-50", "B*log2(B)+B*D", "B*log2(B)", "ordered_commit", "combined population", "hpc/geoga_cpp/src/evolution.cpp::better", "AEP then lexicographic layout tie"),
    ],
    "T40": [
        ("S0_cgpso_population", "120-particle initialization", "B*D", "D", "independent", "particles", "hpc/wflop_cpp/src/algorithms.cpp::optimize_pso", "paper population 120"),
        ("S1_cgpso_cls_m_search", "CLS-M chaotic local search", "B*D*D", "D*D", "independent", "particles and chaotic maps", "hpc/wflop_cpp/src/algorithms.cpp::advance_chaos", "12 maps, L=25, lambda=.01*(1-t/T)"),
        ("S2_cgpso_large_complex_evaluation", "large complex-wind evaluator", "B*Nd*Nv*Nt*Nt", "Nd*Nv*Nt*Nt", "independent", "layouts and wind states", "hpc/fode_cpp/src/evaluator.cpp::evaluate_population_hpc", "21x21, up to 100 turbines"),
        ("S3_cgpso_particle_update", "paper staged PSO update", "B*D", "D", "independent_then_barrier", "particles and coordinates", "hpc/wflop_cpp/src/algorithms.cpp::optimize_pso", "omega=.7298, c=1.49618, pm=.01"),
        ("S4_cgpso_success_failure_memory", "map-memory and pbest commit", "B*D+B", "D", "ordered_reduction", "particles and maps", "hpc/wflop_cpp/src/algorithms.cpp::optimize_pso", "success and failure memories choose future maps"),
    ],
    "Y35": [
        ("S0_hgpso_population", "120-particle initialization", "B*D", "D", "independent", "particles", "hpc/wflop_cpp/src/algorithms.cpp::optimize_hgpso", "paper population 120"),
        ("S1_hgpso_hierarchy_assignment", "hierarchical group construction", "B*log2(B)+B*D", "B*log2(B)", "ordered_then_independent", "ranked hierarchy", "hpc/wflop_cpp/src/algorithms.cpp::optimize_hgpso", "leader/follower groups from stable fitness rank"),
        ("S2_hgpso_group_velocity", "hierarchical particle update", "B*D", "D", "independent", "groups and coordinates", "hpc/wflop_cpp/src/algorithms.cpp::optimize_hgpso", "group leader and personal-best contributions"),
        ("S3_hgpso_landuse_evaluation", "156-case complete evaluator", "B*Nd*Nv*Nt*Nt", "Nd*Nv*Nt*Nt", "independent", "layouts and wind states", "hpc/fode_cpp/src/evaluator.cpp::evaluate_population_hpc", "shared native land-use contract"),
        ("S4_hgpso_pbest_hierarchy_commit", "pbest and hierarchy reduction", "B*D+B*log2(B)", "B*log2(B)", "ordered_commit", "particles", "hpc/wflop_cpp/src/algorithms.cpp::stable_rank_descending", "strict pbest improvement and stable hierarchy"),
    ],
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


def evaluate_expression(expression: str, values: dict[str, int]) -> float:
    return float(eval(
        expression,
        {"__builtins__": {}},
        {**values, "ceil": math.ceil, "log2": math.log2},
    ))


def stage(
    specification: tuple[str, str, str, str, str, str, str, str],
    actual_values: dict[str, dict[str, int]],
) -> dict[str, object]:
    stage_id, role, work, span, classification, space, symbol, equation = (
        specification
    )
    measured: dict[str, dict[str, float]] = {}
    for scale, values in actual_values.items():
        b, d = values["B"], values["D"]
        live = max(64, 8 * b * d * (3 if stage_id.startswith("T") else 1))
        moved = max(64, live * (4 if "evaluation" in stage_id else 3))
        tasks = (
            b * values["Nd"]
            if "evaluation" in stage_id
            else max(values["Ns"], b)
            if stage_id.startswith("T")
            else b
        )
        measured[scale] = substitutions(
            evaluate_expression(work, values),
            evaluate_expression(span, values),
            tasks,
            live,
            moved,
        )
    return {
        "id": stage_id,
        "symbol_role": role,
        "work": work,
        "span": span,
        "equation_or_rule": equation,
        "source_symbol": symbol,
        "classification": classification,
        "parallel_space": space,
        "physical_fes": (
            "B complete evaluations" if "evaluation" in stage_id else "0"
        ),
        "actual_case_substitutions": measured,
    }


def population_size(design: dict[str, object], dimension: int) -> int:
    rule = design.get("B_rule", "fixed")
    if rule == "eighteen_d":
        return 18 * dimension
    if rule == "half_d":
        return max(4, round(0.5 * dimension))
    if rule == "abs77d":
        return max(3, abs(77 - dimension))
    return int(design["B"])


def case_shape(case: dict, defaults: dict[str, int]) -> tuple[int, int, int]:
    nt = int(case.get("turbine_count", defaults["Nt"]))
    nd = len(case.get("wind_directions_rad", []))
    nv = len(case.get("wind_speeds_mps", []))
    probabilities = case.get("joint_probabilities", [])
    if nd == 0 and probabilities:
        nd = len(probabilities)
    if nv == 0 and probabilities:
        nv = len(probabilities[0])
    return nt, nd or defaults["Nd"], nv or defaults["Nv"]


def derive_actual_values(
    row: dict[str, str], design: dict[str, object]
) -> tuple[dict[str, dict[str, int]], dict[str, object]]:
    defaults = {key: int(design[key]) for key in VARIABLES}
    asset_path = ROOT / row["native_asset"]
    source_paths = [row["native_asset"]]
    shapes: list[tuple[int, int, int, str]] = []
    if asset_path.is_file():
        document = json.loads(asset_path.read_text(encoding="utf-8"))
        for case in document.get("cases", []):
            nt, nd, nv = case_shape(case, defaults)
            shapes.append((nt, nd, nv, str(case.get("case_id", "case"))))
    if row["corpus_id"] == "T46":
        shapes = [
            (nt, 12, 6, f"{scenario}_tn{nt}")
            for scenario in ("ws1", "ws2")
            for nt in range(15, 31)
        ]
    elif row["corpus_id"] == "Y06":
        manifest_path = ROOT / ".source-cache/generated/gga_repaired/manifest.json"
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        source_paths.append(str(manifest_path.relative_to(ROOT)))
        shapes = [
            (
                int(case["turbines"]),
                int(case["direction_count"]),
                int(case["speed_bin_count"]),
                str(case["case"]),
            )
            for case in manifest["cases"]
        ]
    elif row["corpus_id"] == "T36":
        shapes = [(72, 12, 4, "Denmark_Nysted")]
    elif row["corpus_id"] == "L0726":
        shapes = [(111, 12, 1, "GeoGA_AnholtStructured_P3_111")]
    if not shapes:
        shapes = [(
            defaults["Nt"], defaults["Nd"], defaults["Nv"],
            row["problem_id"],
        )]
    shapes.sort(key=lambda item: item[0] * item[1] * item[2])
    selected = {
        "smallest": shapes[0],
        "representative": shapes[len(shapes) // 2],
        "largest": shapes[-1],
    }
    values: dict[str, dict[str, int]] = {}
    cases: dict[str, str] = {}
    for scale, (nt, nd, nv, case_id) in selected.items():
        values[scale] = {
            **defaults,
            "B": population_size(design, nt),
            "D": nt,
            "Nt": nt,
            "Nd": nd,
            "Nv": nv,
        }
        cases[scale] = case_id
    provenance = {
        "source_paths": source_paths,
        "source_sha256": {
            path: hashlib.sha256((ROOT / path).read_bytes()).hexdigest()
            for path in source_paths
        },
        "selected_native_cases": cases,
        "derivation": (
            "case dimensions are parsed from the frozen native contract; "
            "population follows the target source/parameter rule"
        ),
    }
    return values, provenance


def make_stages(
    corpus: str, actual_values: dict[str, dict[str, int]]
) -> list[dict[str, object]]:
    return [stage(specification, actual_values) for specification in METHOD_STAGES[corpus]]


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
    sources = {
        "taae_transformer": {
            "corpus_or_environment": "hpc/taae_cpp/src/model.cpp::deterministic_layout_corpus",
            "forward": "hpc/taae_cpp/src/model.cpp::TransformerAutoencoder::train_batch",
            "backward": "hpc/taae_cpp/src/model.cpp::TransformerAutoencoder::train_batch",
            "optimizer": "hpc/taae_cpp/src/model.cpp::TransformerAutoencoder::train_batch",
            "artifact": "hpc/taae_cpp/src/model.cpp::TransformerAutoencoder::save_checkpoint",
            "inference": "hpc/taae_cpp/src/model.cpp::TransformerAutoencoder::encode",
            "optimization_loop": "hpc/taae_cpp/src/evolution.cpp::run_declared_reconstruction",
        },
        "alga_attention": {
            "corpus_or_environment": "hpc/wflop_cpp/src/algorithms/alga_attention_declared_reconstruction.cpp::normalized_fitness_targets",
            "forward": "hpc/wflop_cpp/src/algorithms/alga_attention_declared_reconstruction.cpp::attention_forward",
            "backward": "hpc/wflop_cpp/src/algorithms/alga_attention_declared_reconstruction.cpp::train_one_full_batch_step",
            "optimizer": "hpc/wflop_cpp/src/algorithms/alga_attention_declared_reconstruction.cpp::train_one_full_batch_step",
            "artifact": "hpc/wflop_cpp/src/algorithms/alga_attention_declared_reconstruction.cpp::model_hash",
            "inference": "hpc/wflop_cpp/src/algorithms/alga_attention_declared_reconstruction.cpp::cell_attention_scores",
            "optimization_loop": "hpc/wflop_cpp/src/algorithms/alga_attention_declared_reconstruction.cpp::optimize_alga_attention_declared_reconstruction",
        },
        "rlpso_ppo": {
            "corpus_or_environment": "hpc/wflop_cpp/src/algorithms/rlpso.cpp::optimize_rlpso_training_reconstruction",
            "forward": "hpc/wflop_cpp/src/algorithms/ppo.cpp::SeededPpo::evaluate",
            "backward": "hpc/wflop_cpp/src/algorithms/ppo.cpp::SeededPpo::update",
            "optimizer": "hpc/wflop_cpp/src/algorithms/ppo.cpp::SeededPpo::update",
            "artifact": "hpc/wflop_cpp/src/algorithms/ppo.cpp::SeededPpo::parameter_hash",
            "inference": "hpc/wflop_cpp/src/algorithms/ppo.cpp::SeededPpo::sample_action",
            "optimization_loop": "hpc/wflop_cpp/src/algorithms/rlpso.cpp::optimize_rlpso_paper_corrected_training_reconstruction",
        },
        "fqfode_qtable": {
            "corpus_or_environment": "hpc/wflop_cpp/src/algorithms/rlfode_seeded_reconstruction.cpp::train_artifact",
            "forward": "hpc/wflop_cpp/src/algorithms/rlfode_seeded_reconstruction.cpp::choose_action",
            "backward": "hpc/wflop_cpp/src/algorithms/rlfode_seeded_reconstruction.cpp::q_update",
            "optimizer": "hpc/wflop_cpp/src/algorithms/rlfode_seeded_reconstruction.cpp::q_update",
            "artifact": "hpc/wflop_cpp/src/algorithms/rlfode_seeded_reconstruction.cpp::save_artifact",
            "inference": "hpc/wflop_cpp/src/algorithms/rlfode_seeded_reconstruction.cpp::QTableFractionalController",
            "optimization_loop": "hpc/wflop_cpp/src/algorithms/rlfode_seeded_reconstruction.cpp::optimize_rlfode_seeded_training_reconstruction",
        },
    }[kind]
    return {
        "kind": kind,
        **schema,
        "corpus_or_environment": sources["corpus_or_environment"],
        "forward": sources["forward"],
        "backward": sources["backward"],
        "gradient_aggregation": (
            "current custom CPU implementation uses fixed source/batch order; "
            "LibTorch CPU/CUDA deterministic equivalence remains a Step-4/5 gate"
        ),
        "optimizer": sources["optimizer"],
        "artifact": sources["artifact"],
        "inference": sources["inference"],
        "optimization_loop": sources["optimization_loop"],
        "source_symbols": sources,
        "backend_status": {
            "maturity": "candidate_pre_h6",
            "current": "custom_cpu",
            "proposed": ["libtorch_cpu", "libtorch_cuda", "hybrid_cpu_gpu"],
            "accepted": None,
        },
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
        actual_values, size_provenance = derive_actual_values(row, design)
        stage_rows = make_stages(corpus, actual_values)
        backend_candidate = str(design.get("backend", "cpu_hpc_v1"))
        gpu_analysis = (
            "training tensor work is material; CUDA and hybrid are proposed "
            "candidates whose acceptance requires Step-4/5 bounded validation "
            "and Step-6 measurement"
            if "learning" in design and corpus not in {"S04"}
            else "CPU is the H0-H4 candidate because the current work is "
            "irregular and branch-heavy; cpu_selected_after_analysis is "
            "withheld until exact-pair H6 measurements"
        )
        evaluator_source = str(design["evaluator"]).split("::", 1)[0]
        executor_source = "hpc/fode_cpp/src/executor.cpp"
        shared_components = {
            "persistent_executor": {
                "path": executor_source,
                "sha256": hashlib.sha256(
                    (ROOT / executor_source).read_bytes()
                ).hexdigest(),
            },
            "paper_native_evaluator": {
                "path": evaluator_source,
                "sha256": hashlib.sha256(
                    (ROOT / evaluator_source).read_bytes()
                ).hexdigest(),
            },
        }
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
                "actual_values": actual_values,
                "native_size_provenance": size_provenance,
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
                    item["id"]
                    for item in stage_rows
                    if "ordered" in str(item["classification"])
                    or "commit" in str(item["classification"])
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
                "backend_decision": {
                    "maturity": "candidate_pre_h6",
                    "proposed_backend": backend_candidate,
                    "accepted_backend": None,
                    "acceptance_gate": (
                        "H5 bounded equivalence plus H6 exact-pair performance"
                    ),
                },
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
                    item["id"]: item["source_symbol"]
                    for item in stage_rows
                },
                "shared_components": shared_components,
                "pair_specific_composition": {
                    "method_semantic_id": row["method_semantic_id"],
                    "ordered_stage_ids": [item["id"] for item in stage_rows],
                    "stage_ledger_sha256": hashlib.sha256(
                        json.dumps(
                            [
                                {
                                    key: item[key]
                                    for key in (
                                        "id", "work", "span",
                                        "equation_or_rule", "source_symbol",
                                    )
                                }
                                for item in stage_rows
                            ],
                            sort_keys=True,
                            separators=(",", ":"),
                        ).encode()
                    ).hexdigest(),
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
                "backend_candidates": [
                    f"{row['method_semantic_id']}__{backend_candidate}"
                ],
                "accepted_backend_id": None,
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
