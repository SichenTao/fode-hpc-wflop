/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: L0124 Gaussian-wake Mosetti/Grady grid problems and
MI-LXPM genetic optimizer
Paper/DOI: Wind farm layout optimization using a Gaussian-based wake model;
10.1016/j.renene.2017.02.017
Paper-provided facts: Eqs. (1)-(15), Algorithm 1, Tables 1-4, 2 km by 2 km
flat farm, 10x10 and 20x20 potential-location grids, 5D spacing, k*=0.055,
30/39/39 turbines for wind cases A/B/C, 500 generations, five independent
runs, and approximately 3.00e5 individual evaluations per run.
Public source: no author implementation or data archive was located in the
paper, publisher record, source search, or public-code search.
Direct predecessor: Deep et al., DOI 10.1016/j.amc.2009.02.044, supplies
MI-LXPM tournament size 3, Pc=0.8, Pm=0.005, Laplace a=0 and b_int=0.35,
Power-mutation p_int=4, stochastic integer truncation, and Deb feasibility
selection. The locally acquired predecessor PDF has SHA-256
8a6754a8a8e19f7473fcb49cfd1ae5a1bdc850fa4b18236a7d7995a533b26079.
Wake predecessor: Bastankhah and Porte-Agel,
DOI 10.1016/j.renene.2014.01.002, supplies epsilon=0.2*sqrt(beta) with
beta=0.5*(1+sqrt(1-Ct))/sqrt(1-Ct). Its local PDF SHA-256 is
b988d99662d5c61aa750c232b5f31634394a020f6f22e13b2e2fb1c9886f9389.
Missing/conflicts: author seeds/layouts/code, MI-LXPM parameters omitted from
the target paper, a machine-readable Fig. 6 wind array, strict-vs-nonstrict
5D boundary, and behavior for projected downstream separations below the
published 3D model-validity threshold. The target paper says that ranked best
individuals survive, whereas the cited MI-LXPM predecessor describes complete
old-population replacement.
Reconstruction: recover the 36x3 Fig. 6 joint probabilities from the PDF's
vector rectangle geometry at the plotted 0.001 precision; the recovered
unrounded display values sum to 1.002 because the paper only exposes rounded
bars. Retain those displayed probabilities without silent renormalization:
they yield 36.506188 MW no-wake power for 39 turbines, within 0.01% of the
36.502605 MW identity implied by Table 4. Accept distance >=5D as feasible
because the grid and reported layouts use 5D; apply the Gaussian wake only at
projected downstream distance >=3D; use predecessor-recommended MI-LXPM
parameters; retain the best 5% as the declared OPTIMTOOL-style completion of
the target paper's survival statement; population 600 follows 3.00e5/500;
counter-key all reconstructed random events.
Problem semantic ID: l0124_parada_gaussian_grid_v1
Method semantic ID: l0124_mi_lxpm_target_survival_completed_v1
Production backend: pure C++ CPU. Gaussian pair deficits are precomputed for
36 directions; a persistent team evaluates 600 independent individuals in
parallel. Five formal paper repeats provide the outer Waffle throughput axis.
Controlling contract: shared/contracts/core99_l0124_parada_2017.json
Claim boundary: academic declared reproduction of the target equations,
six paper cases and predecessor-completed MI-LXPM; not author-source,
author-random-state, or exact-layout replay
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#pragma once

#include "fode/executor.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace core99::l0124 {

struct Evaluation {
    double objective = 0.0;
    double expected_power_kw = 0.0;
    double efficiency = 0.0;
    double total_normalized_constraint_violation = 0.0;
    bool feasible = false;
};

struct RunConfig {
    std::uint64_t seed = 20260731;
    int workers = 20;
    int population = 600;
    int generations = 500;
};

struct RunResult {
    std::string problem_id;
    std::string problem_semantic_id;
    std::string method_semantic_id;
    std::uint64_t seed = 0;
    int requested_workers = 0;
    int observed_workers = 0;
    int population = 0;
    int generations = 0;
    std::uint64_t physical_fes = 0;
    Evaluation best_evaluation;
    std::vector<int> best_coordinates;
    std::vector<double> best_objective_history;
    double evaluator_seconds = 0.0;
    double algorithm_seconds = 0.0;
    double end_to_end_seconds = 0.0;
    std::uint64_t scientific_hash = 0;
};

class Problem {
public:
    explicit Problem(std::string problem_id);
    [[nodiscard]] const std::string& id() const noexcept;
    [[nodiscard]] const std::string& semantic_id() const noexcept;
    [[nodiscard]] int grid_size() const noexcept;
    [[nodiscard]] int turbine_count() const noexcept;
    [[nodiscard]] int variable_count() const noexcept;
    [[nodiscard]] double no_wake_power_kw() const noexcept;
    [[nodiscard]] Evaluation evaluate(
        const std::vector<int>& coordinates
    ) const;
    [[nodiscard]] std::vector<Evaluation> evaluate_population(
        const std::vector<std::vector<int>>& population,
        fode::PersistentExecutor& executor
    ) const;

private:
    struct WindState {
        double speed_mps = 0.0;
        double probability = 0.0;
        int direction = 0;
    };

    std::string id_;
    std::string semantic_id_;
    int grid_size_ = 0;
    int turbine_count_ = 0;
    std::vector<WindState> wind_states_;
    std::vector<double> deficit_square_;
    double no_wake_power_kw_ = 0.0;

    void configure_states();
    void precompute_deficits();
};

[[nodiscard]] double gaussian_deficit_ratio(
    double downstream_m,
    double crosswind_m
);

[[nodiscard]] RunResult run(
    const Problem& problem,
    const RunConfig& config
);

}  // namespace core99::l0124
