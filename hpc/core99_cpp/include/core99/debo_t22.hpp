/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T22 DEBO and IEA Task 37 case-study-4 interface
Paper/DOI: A Comparison of Eight Optimization Methods Applied to a Wind Farm
Layout Optimization Problem; 10.5194/wes-8-865-2023
Public source: https://github.com/jaredthomas68/thomas2022-8-opt-algs-wflop
at revision 41d7290b8cc9bf3d90b25d844312f4790037806d; archive
10.5281/zenodo.7125349
Provided assets: case-study boundary, turbine, wind-resource, baseline/result
layouts, and Python/Julia AEP evaluators; no executable DEBO implementation
Paper-preserved problem: 81 IEA 10 MW turbines, five disconnected Borssele
regions, 396 m spacing, 360 direction bins, 20 conditional speed bins,
simplified Gaussian wake, RSS wake combination, and AEP maximization
Paper-preserved method: Algorithms 1--3, greedy boundary-first sequential
allocation followed by shuffled discrete local search; dx=dy=100 m,
dmin=2D, dmax=5D, L=1000 m, Lmin=5 m, ns=6, rho=0.75
Missing/conflicts: author DEBO code and exact shuffle implementation are not
public; the repository has no license file; its Python evaluator header says
case study 3 although the loaded assets and paper are case study 4; the
archived DEBO YAML embeds a stale AEP value while recalculation with the same
repository's frozen wind/model gives 2913.221 GWh, matching the paper; the
rounded author baseline coordinates accumulate 1.131 m polygon violation
Resolution: DEBO is independently reconstructed from paper pseudocode;
counter-keyed Fisher-Yates shuffle, deterministic lexicographic ties, exact
paper data compiled by scripts/generate_core99_t22_data.py; no author code is
copied; archived layouts are independently recalculated rather than trusting
embedded derived values, and the baseline rounding residual is reported
Method/problem semantic IDs: t22_debo_paper_reconstruction_v1;
t22_iea37_cs4_gaussian_aep_v1
Controlling contract: shared/contracts/core99_t22_iea37_cs4.json and
shared/contracts/core99_paper_admissions.json
Production backend: pure C++ CPU-HPC; candidate layouts are evaluated over a
persistent all-core team, while fixed-layout evaluation parallelizes wind
directions
Claim boundary: academic declared reproduction, not author-source or
author-exact numerical DEBO reproduction
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#pragma once

#include "fode/executor.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace core99::t22 {

struct Point {
    double x = 0.0;
    double y = 0.0;
};

struct Evaluation {
    double aep_mwh = 0.0;
    double wake_loss_fraction = 0.0;
    double constraint_violation_m = 0.0;
};

struct RunResult {
    std::string problem_id = "t22_iea37_cs4";
    std::vector<Point> best_layout;
    Evaluation best_evaluation;
    std::uint64_t seed = 0;
    std::uint64_t physical_fes = 0;
    bool paper_termination_reached = false;
    int requested_workers = 0;
    int observed_workers = 0;
    double evaluator_seconds = 0.0;
    double algorithm_seconds = 0.0;
    double end_to_end_seconds = 0.0;
    std::uint64_t scientific_hash = 0;
};

class Problem {
public:
    Problem();

    [[nodiscard]] const std::string& id() const noexcept;
    [[nodiscard]] int turbine_count() const noexcept;
    [[nodiscard]] bool inside(const Point& point) const noexcept;
    [[nodiscard]] double constraint_violation(
        const std::vector<Point>& layout
    ) const;
    [[nodiscard]] Evaluation evaluate(
        const std::vector<Point>& layout
    ) const;
    [[nodiscard]] Evaluation evaluate(
        const std::vector<Point>& layout,
        fode::PersistentExecutor& executor
    ) const;
    [[nodiscard]] std::vector<Evaluation> evaluate_candidates(
        const std::vector<std::vector<Point>>& layouts,
        fode::PersistentExecutor& executor
    ) const;
    [[nodiscard]] std::vector<Point> author_base_layout() const;
    [[nodiscard]] std::vector<Point> author_debo_layout() const;
    [[nodiscard]] double ideal_aep_mwh() const;

private:
    std::string id_ = "t22_iea37_cs4";
};

[[nodiscard]] RunResult run(
    const Problem& problem,
    std::uint64_t seed,
    std::uint64_t physical_fes_limit,
    int workers
);

}  // namespace core99::t22
