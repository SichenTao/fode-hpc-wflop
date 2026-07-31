/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T19 probabilistic-inference WFLOP academic reproduction
Paper: A. Dhoot, E.G.A. Antonini, D.A. Romero and C.H. Amon,
Optimizing Wind Farms Layouts for Maximum Energy Production Using
Probabilistic Inference: Benchmarking Reveals Superior Computational
Efficiency and Scalability, Energy 223 (2021) 120035,
DOI 10.1016/j.energy.2021.120035.
Primary PDF SHA-256:
761c90c22671dfe350770cbee2419f80dbab7a48f6f7773904b1544981ce4c6e.
Public source: paper-linked Vladimir Kolmogorov SRMP v1.01,
https://pub.ist.ac.at/~vnk/software/SRMP-v1.01.zip, SHA-256
e2bf3376f5d4b68c3ed9c88ef37ede187455f907ee0ee71b8572ae04f4be6848,
GPL-3.0-or-later. This optional T19 target is consequently distributed under
GPL-3.0-or-later; it does not relicense the repository's other components.
The pinned source receives one audited modern-C++ safety patch: add an empty
virtual destructor to FactorType because Energy deletes derived factor types
through that base pointer. ASAN otherwise diagnoses new-delete-type-mismatch;
the patch changes neither solver arithmetic nor data structures.
Unavailable source: the paper says its 5,000 clusters came from a modified
GEMPLP generator based on Sontag et al.; the cited public URL now returns 403
and the authors' modification, cluster list and wrapper are not published.
Paper-provided facts: Eqs. (1)-(20), binary QIP/MRF conversion, Jensen/RSS
wakes, WR-1 and WR-36, historical 100-cell cases, realistic NREL-5MW
100/400/2,500-cell cases, 5R spacing, official sequential TRW-S, 5,000
triplets for 100/400 cells, no triplets plus rounding for 2,500 cells,
10,000 SRMP iterations by source default and a one-hour paper cutoff.
Missing: numerical WR-36 array, beta, modified cluster-scoring code and
clusters, exact rounding, random/order state, author wrapper and numerical
NREL curves. The paper and public thesis do not resolve these fields.
Declared reconstruction: reuse the platform's versioned Figure-3/Turner
WR-36 digitization and FLORIS-v2.4 NREL-5MW curve lineage; choose beta as
1.01 times the maximum pair-interaction row sum; generate deterministic top
interaction/conflict triangles; decode to exactly K with deterministic
spacing repair and one-swap improvement. Raw and repaired cardinalities are
both reported. SRMP v1.01's SharedPairwiseFactorType retains a legacy
two-argument InitFactor overload while its base declares the flags argument;
the project wrapper supplies only that forwarding compatibility override and
does not alter solver arithmetic. Triplet roles use the source-supported
fixed insertion ordering (`sort_flag=-1`): the author wrapper/order was not
published, and v1.01's automatic factor quicksort does not terminate safely
for this dense equal-structure triplet graph on the target toolchain. These
completions are not represented as author assets.
HPC design: one persistent all-core C++ executor parallelizes interaction
assembly, cluster scoring and full nonlinear AEP verification. Official
TRW-S remains sequential inside one role because its ordered forward/backward
message dependency and monotone-bound semantics are not legally separable;
independent paper roles are scheduled across all cores without nested
oversubscription. The no-triplet 2,500-cell roles use SRMP's shared binary
pair cost table to reduce memory while retaining exact Eq. (11) energy;
100/400-cell triplet roles use source-native general pair factors because
SRMP v1.01 explicitly does not implement MPLP messages for shared factors.
Method semantic ID: t19_srmp_trws_declared_triplet_reconstruction_v1.
Problem semantic IDs: t19_historical_grid100_jensen_v1;
t19_realistic_grid100_400_2500_jensen_nrel5mw_v1.
Protocol semantic ID: t19_112_deterministic_paper_roles_v1.
Physical FES: one complete posterior nonlinear AEP evaluation of one complete
layout. Matrix construction, message sweeps and repair operations are reported
separately and are not relabelled as physical FES.
Claim boundary: flexible equation/source-level academic reproduction, not the
unpublished author wrapper, triplet generator, beta, exact layouts, numerical
replay, or a first claim for TRW-S/message-passing parallelization.
Contract: shared/contracts/core99_t19_dhoot_2021.json.
Last evidence-audit date: 2026-08-01
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace core99::t19 {

enum class WindRegime { wr1, wr36 };
enum class ProblemFamily { historical, realistic };

struct Point {
    double x_m = 0.0;
    double y_m = 0.0;
};

struct WindState {
    double from_degrees = 0.0;
    double speed_mps = 0.0;
    double probability = 0.0;
};

struct ProblemConfig {
    ProblemFamily family = ProblemFamily::realistic;
    WindRegime wind = WindRegime::wr1;
    int cell_count = 100;
    int turbine_count = 10;
};

struct Evaluation {
    double expected_power_kw = 0.0;
    double aep_gwh = 0.0;
    double qip_wake_objective = 0.0;
    double minimum_spacing_m = 0.0;
    bool exact_cardinality = false;
    bool spacing_feasible = false;
    std::uint64_t physical_fes = 0;
};

struct SolveConfig {
    int workers = 20;
    int maximum_iterations = 10000;
    double time_limit_seconds = 3600.0;
    double convergence_epsilon = 1.0e-8;
    int requested_triplets = -1;
    bool one_swap_improvement = true;
};

struct SolveReceipt {
    std::string problem_semantic_id;
    std::string method_semantic_id =
        "t19_srmp_trws_declared_triplet_reconstruction_v1";
    std::string protocol_semantic_id =
        "t19_112_deterministic_paper_roles_v1";
    ProblemConfig problem;
    int requested_workers = 1;
    int observed_workers = 1;
    int requested_triplets = 0;
    int generated_triplets = 0;
    int raw_cardinality = 0;
    int repaired_cardinality = 0;
    int repair_operations = 0;
    int local_swap_operations = 0;
    int maximum_iterations = 0;
    double time_limit_seconds = 0.0;
    double beta = 0.0;
    double srmp_lower_bound = 0.0;
    double raw_augmented_energy = 0.0;
    double repaired_augmented_energy = 0.0;
    Evaluation evaluation;
    std::vector<int> layout;
    double interaction_assembly_seconds = 0.0;
    double triplet_generation_seconds = 0.0;
    double graph_assembly_seconds = 0.0;
    double sequential_trws_seconds = 0.0;
    double repair_and_local_search_seconds = 0.0;
    double nonlinear_aep_seconds = 0.0;
    double end_to_end_seconds = 0.0;
    std::uint64_t scientific_hash = 0;
};

class Problem {
public:
    explicit Problem(ProblemConfig config);

    [[nodiscard]] const ProblemConfig& config() const noexcept;
    [[nodiscard]] std::string semantic_id() const;
    [[nodiscard]] const std::vector<Point>& cells() const noexcept;
    [[nodiscard]] const std::vector<WindState>& wind_states() const noexcept;
    [[nodiscard]] double minimum_spacing_requirement_m() const noexcept;
    [[nodiscard]] Evaluation evaluate(const std::vector<int>& layout) const;
    [[nodiscard]] SolveReceipt solve(const SolveConfig& config = {}) const;

private:
    ProblemConfig config_;
    std::vector<Point> cells_;
    std::vector<WindState> wind_states_;
};

[[nodiscard]] std::vector<ProblemConfig> paper_roles();
[[nodiscard]] const char* family_name(ProblemFamily family) noexcept;
[[nodiscard]] const char* wind_name(WindRegime wind) noexcept;

}  // namespace core99::t19
