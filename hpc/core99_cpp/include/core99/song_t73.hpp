/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T73 integrated offshore layout and opportunistic
condition-based maintenance problem and two-stage optimization API
Paper/DOI: Song et al., Integrated Optimization of Offshore Wind Farm Layout
Design and Turbine Opportunistic Condition-Based Maintenance;
10.1016/j.cie.2018.04.051. Target PDF SHA-256:
d4ff6a796b03a3d187921addb2c661b035755d5385081ef9b24c8afff554e2c5.
Paper-provided method: binary GA selects the number and grid locations;
continuous pattern search refines the selected coordinates; k-means partitions
the turbines; Monte Carlo simulation selects the periodic inspection interval
under the Table-2 opportunistic/condition/corrective maintenance rules.
Paper-provided problem: an approximately 3 km by 1.5 km New Jersey offshore
site, 342 candidate locations, 232 reported turbines, Jensen wake, two Weibull
dayparts, lognormal wind direction, 20 percent coastward speed reduction, four
component classes, 25-year contract, Table-4 costs, and the Table-3/Table-5
native result roles.
Public source status: exact-title, DOI, author and GitHub searches on 2026-08-01
found no target source, data archive or random states. The paper-designated
predecessor, Li and Wang, 2016 WSC, DOI 10.1109/WSC.2016.7822324, has an
official open proceedings PDF at https://www.informs-sim.org/wsc16papers/254.pdf
(SHA-256 3fc8b66a9cc171ead6f99e27682639339495b2fd29be21d840426e923b6b371c).
It supplies the same GA--pattern-search sequence, 50 GA generations, 200
pattern-search iterations and cable-cost lineage, but its distinct native
problem has 132 candidates on a roughly 7 km by 3 km site and is not silently
substituted for T73.
Missing assets: target code; exact field polygon and 342 coordinates; 2014
ten-minute wind series; price, capacity-revenue and outage traces; GA
population/operators; pattern-search settings; k-means initialization;
component degradation distributions and thresholds; inspection/downtime
costs; Monte Carlo replication count; raw layouts, traces and repeat count.
Paper conflicts: Sections 3.1/5.1 and Fig. 1 state 342 candidates while Sec.
3.5 prints M=232, apparently confusing the optimized turbine count with the
encoding length; production uses 342 decision bits. Section 3.1 states 100 m
adjacent candidate spacing and the site dimensions support that value, while
Sec. 5.1 says 400 m; production uses the internally consistent 100 m grid.
Section 5.2 says four clusters while Fig. 8's caption says two; both profiles
are executable, with four clusters the equation-text primary profile. Fig. 7
incorrectly calls the continuous layout Stage 2 although Secs. 3.5/5.1 and
Table 3 identify it as Part 2 of Stage 1; production follows the latter.
Declared reconstruction: deterministic irregular 342-point mask on the stated
site; 40 counter-keyed wind samples; published Jensen/power/capital equations;
source-backed 50-generation binary GA; deterministic all-core rotating-block
coordinate pattern search for 200 iterations; deterministic k-means; event-
driven lognormal-rate component degradation with explicitly versioned thresholds,
inspection/downtime economics, high-wind inspection delay and 1000 Monte Carlo
replications. Completions
are not calibrated to reproduce Tables 3--5.
Method semantic ID: t73_ga_pattern_kmeans_ocbm_declared_v1.
Problem semantic ID: t73_nj342_layout_maintenance_declared_v1.
Protocol semantic ID: t73_table3_table5_12roles_25seed_v1.
Production backend: pure C++20 CPU-HPC. One persistent all-core executor
parallelizes complete GA candidates, pattern-poll candidates and maintenance
replications across each sequentially reported inspection interval. Immutable
geometry/scenario tables,
counter-keyed random events, fixed-index writes and ordered commits preserve
one/all-core scientific identity. Wind samples and direction trigonometry are
precomputed; the continuous poll evaluates independent complete candidates.
Controlling contract: shared/contracts/core99_t73_song_2018.json.
Claim boundary: source-backed flexible academic reconstruction of the target
coupled problem, target method sequence and all twelve native paper roles; not
author source, unavailable NJ/wind/degradation arrays or numerical replay.
Last evidence-audit date: 2026-08-01
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace core99::t73 {

enum class ClusterProfile { equation_text_four, figure_caption_two };

struct Point {
    double x_m = 0.0;
    double y_m = 0.0;
};

struct LayoutEvaluation {
    bool feasible = false;
    int turbine_count = 0;
    double annual_energy_mwh = 0.0;
    double energy_revenue_usd = 0.0;
    double capacity_revenue_usd = 0.0;
    double annualized_capital_usd = 0.0;
    double pre_maintenance_profit_usd = 0.0;
    double minimum_spacing_m = 0.0;
};

struct MaintenanceEvaluation {
    int inspection_interval_days = 0;
    double mean_cost_usd = 0.0;
    double inspection_cost_usd = 0.0;
    double opportunistic_cost_usd = 0.0;
    double condition_cost_usd = 0.0;
    double corrective_cost_usd = 0.0;
    double downtime_cost_usd = 0.0;
    double mean_downtime_days = 0.0;
};

struct RoleResult {
    std::string role;
    LayoutEvaluation layout;
    MaintenanceEvaluation maintenance;
    double integrated_profit_usd = 0.0;
};

struct RunConfig {
    std::uint64_t seed = 73001;
    int workers = 20;
    int ga_population = 100;
    int ga_generations = 50;
    int pattern_iterations = 200;
    int maintenance_replications = 1000;
    ClusterProfile cluster_profile = ClusterProfile::equation_text_four;
};

struct RunResult {
    std::string case_id;
    std::string method_semantic_id =
        "t73_ga_pattern_kmeans_ocbm_declared_v1";
    std::string problem_semantic_id =
        "t73_nj342_layout_maintenance_declared_v1";
    std::string protocol_semantic_id =
        "t73_table3_table5_12roles_25seed_v1";
    std::uint64_t seed = 0;
    int requested_workers = 0;
    int observed_workers = 0;
    std::uint64_t parallel_regions = 0;
    int ga_population = 0;
    int ga_generations = 0;
    int pattern_iterations = 0;
    int maintenance_replications = 0;
    int cluster_count = 0;
    std::uint64_t layout_evaluations = 0;
    std::uint64_t wind_scenario_turbine_evaluations = 0;
    std::uint64_t component_life_events = 0;
    double scenario_precompute_seconds = 0.0;
    double layout_evaluator_seconds = 0.0;
    double algorithm_seconds = 0.0;
    double maintenance_simulation_seconds = 0.0;
    double end_to_end_seconds = 0.0;
    std::uint64_t scientific_hash = 0;
    std::vector<Point> discrete_layout;
    std::vector<Point> continuous_layout;
    std::vector<int> cluster_assignment;
    std::vector<RoleResult> roles;
};

class Problem {
public:
    struct Impl;

    Problem();
    ~Problem();
    Problem(Problem&&) noexcept;
    Problem& operator=(Problem&&) noexcept;
    Problem(const Problem&) = delete;
    Problem& operator=(const Problem&) = delete;

    [[nodiscard]] int candidate_count() const noexcept;
    [[nodiscard]] const std::vector<Point>& candidate_points() const noexcept;
    [[nodiscard]] LayoutEvaluation evaluate_layout(
        const std::vector<Point>& layout
    ) const;
    [[nodiscard]] MaintenanceEvaluation evaluate_maintenance(
        const std::vector<Point>& layout,
        const std::vector<int>& clusters,
        int inspection_interval_days,
        int replications,
        std::uint64_t seed,
        int workers
    ) const;

private:
    std::unique_ptr<Impl> impl_;
    friend RunResult run(const Problem&, const RunConfig&);
};

[[nodiscard]] RunResult run(const Problem& problem, const RunConfig& config);
[[nodiscard]] std::string to_string(ClusterProfile value);
[[nodiscard]] std::vector<int> paper_inspection_intervals();

}  // namespace core99::t73
