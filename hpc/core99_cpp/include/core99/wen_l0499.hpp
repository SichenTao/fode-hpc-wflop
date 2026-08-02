/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: L0499 uncertain-wind CVaR WFLO and fixed-count binary GA
Paper/DOI: Wen, Song and Wang, Wind farm layout optimization with uncertain
wind condition; 10.1016/j.enconman.2022.115347. Primary PDF SHA-256:
268c05c8a936f1b13cfb48515f25df402c533a28f372bdf152addb523bafa184.
Paper-provided facts: Jensen/partial-overlap Eqs. (1)-(2), yearly WRFV and
AEP Eqs. (3)-(7), Dirichlet-Multinomial Eqs. (10)-(11), multivariate-normal
approximation Eqs. (14)-(17), analytical VaR/CVaR Eq. (18), TO/SO/RO
objectives, beta=0.8, V=8 m/s, turbine/grid settings, 20 independent GA
runs, one simulated Case A and 41-station Case B.
Public code/data: no paper-linked source or machine-readable NDAWN archive
was located. The customized binary-GA predecessor DOI is
10.1109/CCTA41146.2020.9206378; legal full-text retrieval was attempted
but no usable full text was obtained at the audit date.
Missing/conflicts: target omits the 41x20-year arrays, exact Fig. 7 curve
knots, population/generation budget, seeds and operational definitions of
selection, crossover, mutation and survival. The target describes 12x12
"cells" in 2000 m and 10x10 "cells" in 1550 m, while the stated minimum
distance is only satisfied when candidate coordinates span the boundary
inclusively; that interpretation is used.
Reconstruction: a hashed 41-station proxy and digitized Fig. 7 knots are in
shared/data/core99_l0499_proxy.bin and its NOTICE. The fixed-count GA is
completed as population 64, 312 post-initial generations, binary tournament,
0.9 count-preserving crossover, one occupied/unoccupied swap per child and
elitist (mu+lambda) survival. Every choice is isolated and replaceable.
Problem semantic IDs: l0499_case_a_dm_cvar_grid_v1;
l0499_case_b_ndawn41_proxy_dm_cvar_grid_v1.
Method semantic ID: l0499_fixed_count_binary_ga_completed_v1.
Production backend: pure C++ CPU. It precomputes every direction/candidate
wake ratio, evaluates the full population with a persistent full-core team,
uses counter-keyed random events and provides identical one/all-core
scientific trajectories. Formal scheduling may use independent case/seed
parallelism when it gives higher aggregate throughput.
Controlling contract:
shared/contracts/core99_l0499_wen_uncertain_cvar_2022.json.
Claim boundary: academic declared reproduction of the paper equations,
all three objective variants and all 42 problem records; not author code,
NDAWN arrays, exact GA settings, random state or numerical replay.
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace core99::l0499 {

struct Point {
    double x_m = 0.0;
    double y_m = 0.0;
};

struct Evaluation {
    std::vector<double> sector_power_kw;
    double expected_aep_mwh = 0.0;
    double aep_standard_deviation_mwh = 0.0;
    double cvar_mwh = 0.0;
    double minimum_sector_power_kw = 0.0;
    double objective = 0.0;
    bool feasible = false;
};

struct RunConfig {
    std::uint64_t seed = 2026049900;
    int workers = 20;
    std::uint64_t max_physical_fes = 20032;
};

struct RunResult {
    std::string case_id;
    std::string problem_semantic_id;
    std::string method_semantic_id;
    std::uint64_t seed = 0;
    int requested_workers = 0;
    int observed_workers = 0;
    std::uint64_t physical_fes = 0;
    std::uint64_t generations = 0;
    Evaluation initial_best;
    Evaluation best_evaluation;
    std::vector<int> best_candidate_indices;
    std::vector<double> best_objective_history;
    double precomputation_seconds = 0.0;
    double evaluator_seconds = 0.0;
    double algorithm_seconds = 0.0;
    double end_to_end_seconds = 0.0;
    std::uint64_t scientific_hash = 0;
};

class Problem {
public:
    Problem(
        std::string case_id,
        const std::string& proxy_path,
        int workers = 20
    );
    ~Problem();
    Problem(Problem&&) noexcept;
    Problem& operator=(Problem&&) noexcept;
    Problem(const Problem&) = delete;
    Problem& operator=(const Problem&) = delete;

    [[nodiscard]] const std::string& case_id() const noexcept;
    [[nodiscard]] const std::string& semantic_id() const noexcept;
    [[nodiscard]] const std::vector<Point>& candidates() const noexcept;
    [[nodiscard]] int turbine_count() const noexcept;
    [[nodiscard]] int sector_count() const noexcept;
    [[nodiscard]] int station_index() const noexcept;
    [[nodiscard]] const std::string& objective_variant() const noexcept;
    [[nodiscard]] double minimum_spacing_m() const noexcept;
    [[nodiscard]] double precomputation_seconds() const noexcept;
    [[nodiscard]] int observed_precomputation_workers() const noexcept;
    [[nodiscard]] const std::vector<double>& wind_mean() const noexcept;
    [[nodiscard]] const std::vector<double>& wind_covariance() const noexcept;

    [[nodiscard]] Evaluation evaluate(
        const std::vector<int>& candidate_indices
    ) const;
    [[nodiscard]] RunResult optimize(const RunConfig& config) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

[[nodiscard]] std::vector<std::string> paper_case_ids();

}  // namespace core99::l0499
