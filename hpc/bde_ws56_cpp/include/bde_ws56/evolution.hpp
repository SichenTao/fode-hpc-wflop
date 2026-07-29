/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: BDE WS5/WS6 declared-proxy execution interface
Paper title: Discrete Bi-Population Differential Evolution for Optimizing
Complex Wind Farm Layouts in Diverse Terrains
DOI: 10.1016/j.energy.2025.137885
Paper provides: WS5/WS6 cardinalities and unevenness, Fig.5 topology,
21x21 at 231 m, 28x28 at 250 m, N=30/35/40, L=50, FES=10000, and Eqs.18-27.
Public author code URL: https://raw.githubusercontent.com/toyamaailab/toyamaailab.github.io/main/resource/BDE-WindFarm_code.zip
Public author code revision or archive hash: sha256:f4a317d4d727a9d452f76376373e2c8ad5546e35ff19530eda0ba328682dd140
Public code/assets provide: WS1-WS4 arrays, NA_type=13 topology, 231 m
drivers, evaluator functions, and a BDE transition implementation.
Known missing information: exact WS5/WS6 arrays and author adjudication of
spacing, scale-schedule, and 10000-versus-10050 budget conflicts.
Reconstruction performed here: two isolated P3 composite problem identities
and a distinct paper-Imax400/source-resolved exact-FES CPU method.
Method evidence tier: M2_CITATION_PREDECESSOR subtype
paper_equation_direct_source_resolved.
Problem evidence tier: P3_DECLARED_PROXY subtype composite_proxy.
Method semantic ID: bde_paper_equations_imax400_exact_fes_v1
Problem semantic ID: bde2025_ws5_paper250_declared_proxy_v1 and
bde2025_ws6_paper250_declared_proxy_v1
Controlling contracts: shared/contracts/bde_ws56_declared_proxy_contract.json
and shared/contracts/bde_ws56_transition_parity_audit.json
Claim boundary: no original WS5/WS6 array, original 250 m result, or ranking
reproduction; never pool or rank with WS1-WS4 source replay.
Last evidence audit date: 2026-07-29
Paper-preserved fields: WS5 8x12 uneven, WS6 8x16 uneven, standard 21x21 at
231 m, Daegwallyeong-structured 28x28 at 250 m, 30/35/40 turbines,
population 50, FES 10000, Imax=2*FES/L=400, and Eqs. 18--26.
Composite authority: the paper fixes cardinalities, sizes, counts, 250 m, and
the numbered Fig.5 topology; the manual Fig.5 transcription is independently
cross-checked against author-source NA_type=13, which executes at 231 m.
Declared P3 completions: exact wind arrays, independent speed/direction
marginals, stable ties, counter-keyed randomness, and exact budget handling.
Method semantic ID: bde_paper_equations_imax400_exact_fes_v1
Problem semantic IDs: bde2025_ws5_paper250_declared_proxy_v1 and
bde2025_ws6_paper250_declared_proxy_v1
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#pragma once

#include "fode/case.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace bde_ws56 {

inline constexpr const char* kMethodSemanticId =
    "bde_paper_equations_imax400_exact_fes_v1";
inline constexpr const char* kExecutionProfileId =
    "bde2025_ws56_p3_paper_schedule_cpu_v1";
inline constexpr const char* kWs5ProblemSemanticId =
    "bde2025_ws5_paper250_declared_proxy_v1";
inline constexpr const char* kWs6ProblemSemanticId =
    "bde2025_ws6_paper250_declared_proxy_v1";

struct Config {
    std::uint64_t seed = 20260729;
    std::uint64_t physical_fes = 10000;
    int workers = 0;
    std::string execution_mode = "auto";
};

struct StageReceipt {
    double wall_seconds = 0.0;
    std::uint64_t parallel_regions = 0;
    std::uint64_t task_items = 0;
    std::uint64_t participant_activations = 0;
    int distinct_participants = 0;
    int peak_region_participants = 0;
};

struct WorkReceipt {
    std::uint64_t complete_layout_evaluations = 0;
    std::uint64_t ranked_individuals = 0;
    std::uint64_t fusion_memberships = 0;
    std::uint64_t mutation_vectors = 0;
    std::uint64_t crossover_gene_trials = 0;
    std::uint64_t forced_crossover_genes = 0;
    std::uint64_t repair_random_draws = 0;
    std::uint64_t accepted_replacements = 0;
};

struct Result {
    std::string method_semantic_id;
    std::string execution_profile_id;
    std::string problem_semantic_id;
    std::string case_id;
    std::string objective_semantics_hash;
    std::string feasible_set_hash;
    std::uint64_t seed = 0;
    std::uint64_t physical_fes = 0;
    std::uint64_t generations = 0;
    std::uint64_t schedule_imax = 400;
    int population_size = 50;
    int requested_workers = 0;
    int resolved_workers = 0;
    std::string requested_execution_mode;
    std::string resolved_execution_mode;
    double total_wall_seconds = 0.0;
    StageReceipt initialization_stage;
    StageReceipt fusion_variation_repair_stage;
    StageReceipt evaluator_stage;
    StageReceipt selection_other_stage;
    WorkReceipt work;
    std::vector<int> best_layout_1based;
    double best_expected_power_kw = 0.0;
    double no_wake_expected_power_kw = 0.0;
    double conversion_efficiency_percent = 0.0;
    std::string best_layout_hash;
    std::string population_layout_hash;
};

std::string problem_semantic_id(const fode::CaseData& data);
std::string objective_semantics_hash(const fode::CaseData& data);
std::string feasible_set_hash(const fode::CaseData& data);
double turbine_power_kw(double wind_speed_mps);
double no_wake_expected_power_kw(const fode::CaseData& data);
Result run(const Config& config, const fode::CaseData& data);
double evaluate_layout(
    const fode::CaseData& data,
    const std::vector<int>& layout_1based,
    int workers
);
std::string result_to_json(const Result& result);

}  // namespace bde_ws56
