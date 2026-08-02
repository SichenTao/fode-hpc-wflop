/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T69 changing-wind robustness WFLOP and random search
Paper/DOI: Ju Feng and Wen Zhong Shen, Wind Farm Power Production in the
Changing Wind: Robustness Quantification and Layout Optimization;
10.1016/j.enconman.2017.06.005. Primary PDF SHA-256:
4bd7222cd3b8399811bf2e9e7825ad7564a0d1f96f99972c2495093ec0f37f6e.
Public source: DTU Orbit publishes the accepted manuscript, but exhaustive
title/DOI/author searches found no paper-linked implementation or numerical
archive. The code below is an independent academic reconstruction.
Paper-provided facts: Eqs. (1)-(12), the 1 m/s by 1 degree power surface,
PSRI/VoP, 12-sector 35-parameter wind model, lambda-PDF uncertainty with
lambda=1, variations (0.2,0.1,0.5), 1000 Monte Carlo distributions, Horns
Rev 1 with 80 V80-2MW turbines, 5D spacing, one-turbine random search, five
alpha/beta/gamma values, and 10000 complete feasible evaluations per case.
Missing assets: author source/random states; numeric Horns layout/boundary and
V80 curves; exact joint-distribution interpolation; Monte Carlo samples; the
three-year ten-minute series; quadrature boundary treatment; and independent
repeat count.
Same-lineage completion: T60 (DOI 10.1016/j.renene.2015.01.005) supplies the
cited Jensen/random-search semantics; pinned PyWake revision
5b07481ec9b3633a74844651648f266ba82a8b32 supplies Horns coordinates and V80
tables; DOI 10.3390/en8043075 supplies the target's hub-height three-year
12-sector parameters. Its piecewise joint PDF (Eq. 8) is selected because T69
does not identify which of that paper's piecewise/linear/spline variants it
used; this choice preserves the exact 35-parameter uncertainty construction.
Conflicts and resolutions: Eq. (2) calls 8770 the hours in a year; output
reports both literal 8770-hour and calendar 8760-hour AEP. Equation (4) omits
the non-dimensional density Jacobian; the literal paper quadrature therefore
divides physical probability mass by 22x360 and reproduces the reported VoP
order, while robustness ratios remain unaffected. Section 5 says
alpha=beta=0.95, but Table 3's R_long=4.750
is numerically possible only with beta=0.5 because sqrt(78.54/3.482)=4.750;
both equation_declared and table3_compatible profiles are executable and are
never conflated. The target describes moving one turbine to a random position,
so this implementation does not import T60's remembered successful ray.
Power-surface speed cells are 3..25 m/s; outside neighbors are zero-power and
direction neighbors are cyclic. One fixed counter-keyed 1000-scenario bank is
shared by every layout and study case; sample standard deviation is used.
Target method/problem semantic IDs: t69_random_position_rs_v1;
t69_horns_changing_wind_robustness_declared_v1.
Controlling contract: shared/contracts/core99_t69_feng_robustness_2017.json
Production backend: pure C++20 CPU-HPC. It preserves the 360x23 power surface,
updates a one-moved-turbine Jensen tensor in O(360N), evaluates direction and
scenario axes on one persistent all-core team, pre-aggregates 30 directions
into each sector, and uses fixed-index writes plus ordered reductions/commits.
Claim boundary: source-backed flexible academic reproduction of the complete
paper problem, robustness equations, conflict profiles and native parameter
sweeps; not author source, unavailable time series, random state or exact
numeric replay.
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#pragma once

#include "fode/executor.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace core99::t69 {

struct Point {
    double x_m = 0.0;
    double y_m = 0.0;
};

enum class Study { short_term, long_term, overall };
enum class ConflictProfile { equation_declared, table3_compatible };

struct Evaluation {
    double mean_power_mw = 0.0;
    double aep_mwh_paper_8770 = 0.0;
    double aep_mwh_calendar_8760 = 0.0;
    double variability_of_power = 0.0;
    double long_term_mean_mw = 0.0;
    double long_term_std_mw = 0.0;
    double short_robustness = 0.0;
    double long_robustness = 0.0;
    double table3_compatible_long_robustness = 0.0;
    double overall_robustness = 0.0;
    bool feasible = false;
};

struct RunConfig {
    Study study = Study::overall;
    ConflictProfile conflict_profile = ConflictProfile::equation_declared;
    double weight = 0.95;
    double alpha = 0.95;
    double beta = 0.95;
    std::uint64_t seed = 69001;
    std::uint64_t physical_fes = 10000;
    int workers = 20;
};

struct RunResult {
    std::string case_id;
    std::string method_semantic_id = "t69_random_position_rs_v1";
    std::string problem_semantic_id =
        "t69_horns_changing_wind_robustness_declared_v1";
    std::string protocol_semantic_id = "t69_native_15case_10000fes_v1";
    std::string study;
    std::string conflict_profile;
    double weight = 0.0;
    double effective_alpha = 0.0;
    double effective_beta = 0.0;
    double effective_gamma = 0.0;
    std::uint64_t seed = 0;
    std::uint64_t physical_fes = 0;
    std::uint64_t infeasible_proposals = 0;
    std::uint64_t accepted_moves = 0;
    int requested_workers = 0;
    int observed_workers = 0;
    std::uint64_t parallel_regions = 0;
    Evaluation reference;
    Evaluation final_evaluation;
    std::vector<Point> final_layout;
    double wind_scenario_precompute_seconds = 0.0;
    double wake_update_seconds = 0.0;
    double robustness_metric_seconds = 0.0;
    double algorithm_seconds = 0.0;
    double end_to_end_seconds = 0.0;
    std::uint64_t scientific_hash = 0;
};

class Problem {
public:
    explicit Problem(
        int workers = 20,
        std::uint64_t scenario_seed = 69000,
        int scenario_count = 1000
    );
    ~Problem();
    Problem(Problem&&) noexcept;
    Problem& operator=(Problem&&) noexcept;
    Problem(const Problem&) = delete;
    Problem& operator=(const Problem&) = delete;

    [[nodiscard]] int turbine_count() const noexcept;
    [[nodiscard]] int direction_count() const noexcept;
    [[nodiscard]] int speed_count() const noexcept;
    [[nodiscard]] int scenario_count() const noexcept;
    [[nodiscard]] std::vector<Point> paper_initial_layout() const;
    [[nodiscard]] Evaluation evaluate(
        const std::vector<Point>& layout,
        double alpha = 0.95,
        double beta = 0.95,
        double gamma = 0.95,
        ConflictProfile profile = ConflictProfile::equation_declared
    ) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    friend RunResult run(const Problem&, const RunConfig&);
};

[[nodiscard]] RunResult run(const Problem& problem, const RunConfig& config);
[[nodiscard]] std::vector<double> paper_weights();
[[nodiscard]] std::string to_string(Study value);
[[nodiscard]] std::string to_string(ConflictProfile value);

}  // namespace core99::t69
