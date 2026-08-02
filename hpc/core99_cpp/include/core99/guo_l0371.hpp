/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: L0371 IET-GWM atmospheric-stability WFLO and DEEM
Paper/DOI: Guo et al., Influence of atmospheric stability on wind farm
layout optimization based on an improved Gaussian wake model;
10.1016/j.jweia.2021.104548. Primary PDF SHA-256:
227ac3844dd69fda900fc882595e39dfe08eb4483d889a31642c6b032c277e66.
Paper-provided facts: IET-GWM Eqs. (1)-(11), RSS wake superposition, Mosetti
cost/COE, three 2 km ideal cases, seven stability classes, Horns Rev 1,
grid encoding, >=5D visual layout scale, and DE F=CR=0.9.
Public code/data: no paper-linked author repository or data archive was
located. The paper supplies figures but no machine-readable arrays.
Direct wake predecessor: Cheng et al., DOI
10.1016/j.apenergy.2019.01.225, PDF SHA-256
19274a59889ba7b8310cc9dca226a0304beef4af91c02114d630def7fdd500cf,
confirms the MOST/IET-GWM equations and constants.
Direct DE predecessor: Wang et al., DOI 10.1109/TII.2017.2743761,
PDF SHA-256
eddbd51ef085bdf1ee0c7a5a1eb523d41b2af80336a83e2af3eedcf16fa60895,
supplies random replacement, 200-attempt initialization restart,
MaxFEs=150000 and 30 independent runs.
Horns wind-data supplement: Feng and Shen, DOI 10.3390/en8043075,
PDF SHA-256
d8f82df7d70cfbe02c3ff8ae36c9cc20ef0995b945486c46912a72a6c3d160d2,
supplies exact 12-sector Weibull parameters for the same mast record.
Missing/conflicts: target omits code, arrays, grid size, latitude, maximum
iterations/FEs, repeats, seeds and initialization. Eq. (13) is typeset as
strict >5D, while Fig. 4 visibly uses ten turbines at 200 m spacing with
D=40 m. Target Eq. (8) prints z0 inside the cosine whereas its Cheng
predecessor uses hub height z. Horns Rev power/Ct and stability arrays are
figure-only.
Reconstruction: use the 10x10, 200 m ideal grid evidenced by Figs. 4-6 and
the non-strict >=5D visual semantics; use target Eq. (8) literally with z0;
use latitude 47 degrees for ideal validation (Cheng) and 55.5 degrees for
Horns Rev; inherit 150000 physical feasible-layout evaluations and 30
runs from the direct DE predecessor; use a declared 200 m Horns grid
inferred from Fig. 12. Hashed fixture details:
shared/data/core99_l0371_proxy.bin and
shared/data/core99_l0371_proxy_NOTICE.md.
Problem semantic IDs:
l0371_ideal_ietgwm_grid_v1; l0371_horns_ietgwm_grid_proxy_v1.
Method semantic ID: l0371_deem_predecessor_completed_v1.
Production backend: pure C++ CPU. It precomputes all candidate-pair wake
ratios, maintains the DEEM O(N) moved-turbine cache from Wang et al.,
preserves sequential in-turn acceptance, uses a persistent full-core team
for heavy state axes, and lets the formal scheduler parallelize light
independent cases/seeds without oversubscription.
Controlling contract:
shared/contracts/core99_l0371_guo_stability_deem_2021.json.
Claim boundary: academic declared reproduction of every paper-native ideal
and Horns Rev problem and the predecessor-completed DEEM; not author code,
private arrays, exact random state, exact grid, or exact numerical replay.
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#pragma once

#include "fode/executor.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace core99::l0371 {

struct Point {
    double x_m = 0.0;
    double y_m = 0.0;
};

struct WindState {
    double from_degrees = 0.0;
    double speed_mps = 0.0;
    double probability = 0.0;
    int stability_index = 3;
};

struct Evaluation {
    double coe = 0.0;
    double average_power_kw = 0.0;
    double no_wake_power_kw = 0.0;
    double efficiency = 0.0;
    bool feasible = false;
};

struct RunConfig {
    std::uint64_t seed = 20260731;
    int workers = 20;
    std::uint64_t max_physical_fes = 150000;
};

struct RunResult {
    std::string case_id;
    std::string problem_semantic_id;
    std::string method_semantic_id;
    std::uint64_t seed = 0;
    int requested_workers = 0;
    int observed_workers = 0;
    std::uint64_t proposed_trials = 0;
    std::uint64_t physical_fes = 0;
    std::uint64_t rejected_constraint_trials = 0;
    Evaluation initial_evaluation;
    Evaluation best_evaluation;
    std::vector<int> best_candidate_indices;
    std::vector<double> best_power_history_kw;
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
    [[nodiscard]] const std::vector<WindState>& states() const noexcept;
    [[nodiscard]] int turbine_count() const noexcept;
    [[nodiscard]] double minimum_spacing_m() const noexcept;
    [[nodiscard]] double precomputation_seconds() const noexcept;
    [[nodiscard]] int observed_precomputation_workers() const noexcept;
    [[nodiscard]] bool is_horns() const noexcept;

    [[nodiscard]] Evaluation evaluate(
        const std::vector<int>& candidate_indices
    ) const;
    [[nodiscard]] RunResult optimize(const RunConfig& config) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

[[nodiscard]] std::vector<std::string> paper_case_ids();

}  // namespace core99::l0371
