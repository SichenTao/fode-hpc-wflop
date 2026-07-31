/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T05 Turner QIP/MILP/bounding WFLO package
Paper: S.D.O. Turner, D.A. Romero, P.Y. Zhang, C.H. Amon and
T.C.Y. Chan, A New Mathematical Programming Approach to Optimize Wind Farm
Layouts, Renewable Energy 63 (2014) 674-680,
DOI 10.1016/j.renene.2013.10.023.
Primary PDF SHA-256:
06ed7e56a21e2ad7d9480985029adb334f2f64f742b7533072f494126d553be1.
Public source: no paper-linked author code or data was found. A legally
accessible Elsevier API PDF was consumed for equations, tables and figures.
Paper-provided facts: Eqs. (1)-(10), Jensen/Katic deficit superposition,
fixed-cardinality binary 10x10 grid, QIP interaction objective, equivalent
MILP linearization, neighboring-K convex lower-bound policy, one-cell
post-solver local search, Table-5 turbine/model constants, Cases A-C and
the model-specific K values in Tables 6-8.
Missing: CPLEX model files, callbacks, tolerances, initial solutions and
random state were not published; CPLEX 12.1 is proprietary and unavailable;
Figure 5 exposes no numerical Case-C probability array; the paper reports no
repeat protocol and no finite node budget apart from a 24-hour solver cap.
Reconstruction: pure-C++ deterministic fixed-cardinality QIP branch-and-bound
with an admissible interaction lower bound, the paper neighboring-K bound,
parallel deterministic multistart incumbent construction and the disclosed
one-cell local search; Case-C joint probabilities are a normalized Figure-5
digitization; one deterministic run per paper-native case is the protocol.
Method semantic ID:
t05_turner_qip_milp_bounding_declared_open_solver_v1.
Problem semantic IDs: t05_case_a_grid100_v1; t05_case_b_grid100_v1;
t05_case_c_figure5_digitized_grid100_v1.
Protocol semantic ID: t05_six_native_cases_one_deterministic_run_v1.
Production backend: pure C++ CPU with one persistent full-core team for
interaction assembly, independent incumbent searches, branch-frontier
expansion and final scenario evaluation; no nested oversubscription.
Claim boundary: academic equation-level reconstruction of the mathematical
programming framework, not CPLEX, author code, exact Figure-5 data, numerical
replay, or an optimality claim when the declared node limit is reached.
Contract: shared/contracts/core99_t05_turner_math_programming_2014.json.
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace core99::t05 {

using Layout = std::vector<int>;

struct WindState {
    double from_degrees = 0.0;
    double speed_mps = 0.0;
    double probability = 0.0;
};

struct PaperCase {
    std::string case_id;
    std::string problem_semantic_id;
    std::string formulation;
    int turbine_count = 0;
    std::vector<WindState> wind_states;
    double published_power_kw = 0.0;
};

struct RunConfig {
    std::uint64_t seed = 201410023ULL;
    int workers = 20;
    int multistarts = -1;
    std::uint64_t node_limit = 0;
};

struct RunResult {
    std::string case_id;
    std::string problem_semantic_id;
    std::string method_semantic_id;
    std::string formulation;
    std::uint64_t seed = 0;
    int requested_workers = 0;
    int observed_workers = 0;
    int turbine_count = 0;
    int multistarts = 0;
    std::uint64_t node_limit = 0;
    std::uint64_t explored_nodes = 0;
    std::uint64_t local_candidate_evaluations = 0;
    double qip_objective = 0.0;
    double milp_linearized_objective = 0.0;
    double admissible_lower_bound = 0.0;
    double relative_gap = 0.0;
    double expected_power_kw = 0.0;
    double published_power_kw = 0.0;
    bool exact_certificate = false;
    Layout best_layout;
    double interaction_assembly_seconds = 0.0;
    double incumbent_search_seconds = 0.0;
    double branch_and_bound_seconds = 0.0;
    double local_search_seconds = 0.0;
    double power_evaluation_seconds = 0.0;
    double end_to_end_seconds = 0.0;
    std::uint64_t scientific_hash = 0;
};

class Problem {
public:
    explicit Problem(std::string case_id);

    [[nodiscard]] const PaperCase& paper_case() const noexcept;
    [[nodiscard]] double qip_objective(const Layout& layout) const;
    [[nodiscard]] double milp_linearized_objective(const Layout& layout) const;
    [[nodiscard]] double expected_power_kw(const Layout& layout) const;
    [[nodiscard]] bool feasible(const Layout& layout) const noexcept;
    [[nodiscard]] RunResult optimize(const RunConfig& config) const;

private:
    PaperCase case_;
    std::vector<double> interaction_;
};

[[nodiscard]] std::vector<std::string> paper_case_ids();

}  // namespace core99::t05
