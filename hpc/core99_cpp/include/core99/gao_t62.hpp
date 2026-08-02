/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T62 Jensen-Gaussian wake model, paper-native Case (b),
and multi-population genetic algorithm (MPGA)
Paper/DOI: Optimization of Wind Turbine Layout Position in a Wind Farm Using
a Newly-Developed Two-Dimensional Wake Model;
10.1016/j.apenergy.2016.04.098
Public source: no paper-linked author code or numeric archive was located
Cited predecessor used to complete MPGA fields: Wind turbine layout
optimization using multi-population genetic algorithm and a case study in
Hong Kong offshore; 10.1016/j.jweia.2015.01.018; legal Elsevier API copy
stored as Gao2015_Wind.pdf, SHA-256
13ffeac89390e507d2509a5bf0e18d7c2e4bef7e2bff8a20f74bb02829a79408
Paper-provided assets: improved Jensen-Gaussian equations (17)-(19);
2000 m square Case (b), 12 m/s and 36 equal wind directions; N=38/39/40;
10 by 10 grid with 200 m cells; 20-bit X/Y coordinates; five-diameter
spacing; population size 20; 500 unchanged-generation stop; printed power,
fitness, and efficiency anchors
Predecessor-provided assets: ten populations, crossover range 0.7-0.9,
mutation range 0.001-0.05, 40 individuals in the predecessor experiment,
20-bit variables, elite retention, immigration, and 500-generation stop
Missing/conflicts: the target paper gives no source, seeds, layouts, ambient
turbulence used in optimization, MPGA selection/crossover/migration schedule,
or repeat count; it simultaneously requires grid-centre sites and continuous
2N coordinates; Fig. 6 says 20 individuals while the predecessor says 40;
Eq. (4) labels 0.5/log(z/z0) as axial induction although it is the wake-decay
coefficient; the inherited turbine table calls 40 m a rotor radius while its
power equation uses a 20 m radius; Table 1 fitness for N=38 and N=39 is
inconsistent with its own cost equation and total-power values
Resolution: paper-first primary mode decodes 20-bit coordinates then snaps
and deterministically repairs them onto the 100 unique 200 m grid centres;
a continuous-coordinate sensitivity mode is retained; use 20 individuals per
deme from the target paper, ten demes and parameter ranges from the cited
predecessor; use tournament-2, one-point crossover, per-bit mutation, elitism,
and 20-generation ring migration as declared deterministic reconstructions;
use 10% ambient turbulence from the paper's improved-model validation because
the optimization value is absent; interpret the turbine as 40 m diameter
because Eq. (5) and the printed free-stream powers require a 20 m radius
Problem semantic IDs: t62_gao_case_b_grid_jensen_gaussian_v1;
t62_gao_case_b_continuous_jensen_gaussian_sensitivity_v1
Method semantic ID: t62_mpga_declared_reconstruction_v1
Production backend: pure C++ CPU; evaluator pair kernels are precomputed for
the grid problem; all deme-individual evaluations run on one persistent
full-resource team; state generation and elite/migration commits are ordered
Claim boundary: academic declared reproduction of the paper's new wake model,
native problem, and MPGA optimization vehicle, not author-code or
author-numerical replay
Controlling contract: shared/contracts/core99_t62_gao_2016.json
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#pragma once

#include "fode/executor.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace core99::t62 {

enum class SiteMode { paper_grid, continuous_sensitivity };

struct Point {
    double x_m = 0.0;
    double y_m = 0.0;
};

struct Evaluation {
    double objective = 0.0;
    double average_power_kw = 0.0;
    double efficiency = 0.0;
    double cost = 0.0;
    double constraint_violation = 0.0;
};

struct MpgaConfig {
    int demes = 10;
    int individuals_per_deme = 20;
    int unchanged_generations = 500;
    int maximum_generations = 5000;
    int migration_period = 20;
};

struct RunResult {
    std::string problem_semantic_id;
    std::string method_semantic_id;
    int turbine_count = 0;
    std::uint64_t seed = 0;
    int generations = 0;
    int unchanged_generations = 0;
    std::uint64_t physical_fes = 0;
    int requested_workers = 0;
    int observed_workers = 0;
    double evaluator_seconds = 0.0;
    double algorithm_seconds = 0.0;
    double end_to_end_seconds = 0.0;
    Evaluation best_evaluation;
    std::vector<Point> best_layout;
    std::uint64_t scientific_hash = 0;
};

class Problem {
public:
    explicit Problem(int turbine_count, SiteMode mode = SiteMode::paper_grid);
    [[nodiscard]] int turbine_count() const noexcept;
    [[nodiscard]] SiteMode site_mode() const noexcept;
    [[nodiscard]] const std::string& semantic_id() const noexcept;
    [[nodiscard]] std::vector<Point> decode(
        const std::vector<std::uint32_t>& genes
    ) const;
    [[nodiscard]] Evaluation evaluate_genes(
        const std::vector<std::uint32_t>& genes
    ) const;
    [[nodiscard]] Evaluation evaluate_layout(
        const std::vector<Point>& layout
    ) const;
    [[nodiscard]] std::vector<Evaluation> evaluate_population(
        const std::vector<std::vector<std::uint32_t>>& population,
        fode::PersistentExecutor& executor
    ) const;

private:
    int turbine_count_ = 0;
    SiteMode mode_ = SiteMode::paper_grid;
    std::string semantic_id_;
    std::vector<double> grid_pair_deficit_squared_;
};

[[nodiscard]] double paper_cost(int turbine_count);
[[nodiscard]] double improved_wake_speed_ratio(
    double downstream_diameters,
    double crosswind_diameters,
    double thrust_coefficient,
    double ambient_turbulence
);
[[nodiscard]] RunResult run_mpga(
    const Problem& problem,
    std::uint64_t seed,
    int workers,
    const MpgaConfig& config = {}
);

}  // namespace core99::t62
