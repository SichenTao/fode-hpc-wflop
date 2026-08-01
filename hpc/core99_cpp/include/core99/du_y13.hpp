/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: Y13 grid-WFLO consensus ADMM problem and CPU-HPC method
Paper/DOI: Du et al., A Generic Acceleration Framework for Grid-Based Wind
Farm Layout Optimization; 10.1109/TSTE.2025.3609006.
Primary paper: local four-page PDF SHA-256
7f3e0fefd9467db259b5a221d1e2042b244813d6bd3eaa08ccc645d4769c4a2f.
Public source/data: no paper-linked target code, repository, supplement or
machine-readable case data was found by title, DOI, author and institution
search on 2026-08-01. The paper states Gurobi but does not publish a version,
model file, warm start, solver state, convergence tolerance, adaptive-rho
rule, random state or the 36 Danish wind-scenario numeric values.
Paper-provided facts: four square grids 6x6, 10x10, 16x16 and 20x20; one WT
in half the cells; 5D cell pitch; NREL 5 MW turbine; 36 directions at 10-degree
intervals; Frandsen-Gaussian pair wakes; binary power BIP; scenario consensus;
the p=2 box-sphere equivalence; rho=0.5 initially; zero duals; warm start;
adaptive rho; scenario-parallel ADMM; final rounding; typical convergence in
at most ten iterations and at most 5 percent rounding deviation.
Cited public model authority: Tao et al., DOI 10.1109/TPWRS.2019.2916906,
author-accepted manuscript at
https://pure.royalholloway.ac.uk/ws/files/33855968/FINAL_VERSION_Siyu_I.pdf.
It supplies the Frandsen-Gaussian equations. The target paper's cited Danish
resource DOI 10.1109/TII.2025.3538067 does not expose machine-readable wind
values or a source repository.
Reconstruction: retain all four paper grids and directions; reconstruct a
deterministic offshore directional resource with an explicitly normalized
smooth Danish-sector prior and a disclosed NREL-5MW-style cubic/rated curve;
use the cited F-G formula sigma^2=r0^2+alpha^2*x^2, r0=0.8R and
alpha=0.56/log(zh/z0); implement the printed linear pair-loss BIP exactly.
The unavailable Gurobi QP is replaced by pinned open HiGHS revision
04024d701f79feb8e2f18bc3df0dffc04ef05088. The pinned active-set QP path
fails on the paper's highly degenerate power constraints despite an explicit
feasible probe, so each separable x-squared term is represented by 33 uniform
epigraph tangents. Its pointwise underestimation is bounded by 1/(4*32^2) on
[0,1]. The paper's maximization with a positive quadratic followed by arg-min
is sign-inconsistent; this package uses the standard equivalent minimization
of -f plus positive augmented quadratics. Printed Eq. 1d also remains active
when the target cell is absent; a standard tight M(1-x_i) implication restores
the intended binary semantics. Missing warm start is a deterministic exact-
cardinality boundary-priority layout. Missing convergence/rho rules are
residual tolerance 1e-3, residual balancing at ratio 10, and factor-1.5 growth
while binary rounding deviation exceeds five percent. Final rounding selects
the largest nwt consensus components with stable grid-index ties.
HPC realization: pair-wake matrices and the 36 independent scenario
subproblems use one persistent all-core team. Every HiGHS subproblem is
single-threaded to
avoid nested oversubscription. Scenario slots and consensus reductions have a
fixed order; the deterministic method has one paper-native run per grid case.
Method semantic ID: y13_l2box_consensus_admm_highs_declared_v1.
Problem semantic ID: y13_four_grid_fg36_declared_v1.
Protocol semantic ID: y13_native_four_case_single_run_v1.
Controlling contract: shared/contracts/core99_y13_du_grid_admm_2026.json.
Claim boundary: equation-level flexible academic reproduction of the target
algorithm-problem pair, not author Gurobi code, Danish data, warm start,
solver trajectory, objective table identity or timing replay.
Last evidence-audit date: 2026-08-01
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#pragma once

#include "fode/executor.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace core99::y13 {

enum class CaseId { grid_6 = 6, grid_10 = 10, grid_16 = 16, grid_20 = 20 };

struct Evaluation {
    double gross_aep_gwh = 0.0;
    double net_aep_gwh = 0.0;
    double efficiency_percent = 0.0;
    int turbines = 0;
    int requested_workers = 0;
    int observed_workers = 0;
    std::uint64_t scenario_pair_lookups = 0;
    double seconds = 0.0;
};

struct RunConfig {
    int workers = 20;
    int maximum_admm_iterations = 10;
    double convergence_tolerance = 1.0e-3;
    bool smoke = false;
};

struct IterationReceipt {
    int iteration = 0;
    double rho = 0.0;
    double primal_residual = 0.0;
    double dual_residual = 0.0;
    double rounding_deviation = 0.0;
    double subproblem_seconds = 0.0;
};

struct RunResult {
    std::string problem_semantic_id = "y13_four_grid_fg36_declared_v1";
    std::string method_semantic_id =
        "y13_l2box_consensus_admm_highs_declared_v1";
    std::string protocol_semantic_id =
        "y13_native_four_case_single_run_v1";
    CaseId case_id = CaseId::grid_6;
    int grid_side = 0;
    int cells = 0;
    int turbines = 0;
    int wind_scenarios = 0;
    int requested_workers = 0;
    int observed_workers = 0;
    int admm_iterations = 0;
    std::uint64_t scenario_subproblem_solves = 0;
    std::uint64_t complete_layout_evaluations = 0;
    double initial_rho = 0.5;
    double final_rho = 0.5;
    double final_rounding_deviation = 0.0;
    Evaluation initial_evaluation;
    Evaluation final_evaluation;
    std::vector<int> selected_cells;
    std::vector<IterationReceipt> iterations;
    double matrix_seconds = 0.0;
    double subproblem_seconds = 0.0;
    double evaluator_seconds = 0.0;
    double algorithm_seconds = 0.0;
    double end_to_end_seconds = 0.0;
    std::uint64_t scientific_hash = 0;
};

class Problem {
public:
    struct Data;

    Problem(CaseId case_id, int workers);
    ~Problem();
    Problem(Problem&&) noexcept;
    Problem& operator=(Problem&&) noexcept;
    Problem(const Problem&) = delete;
    Problem& operator=(const Problem&) = delete;

    [[nodiscard]] CaseId case_id() const noexcept;
    [[nodiscard]] int grid_side() const noexcept;
    [[nodiscard]] int cell_count() const noexcept;
    [[nodiscard]] int turbine_count() const noexcept;
    [[nodiscard]] int wind_scenario_count() const noexcept;
    [[nodiscard]] double cell_pitch_m() const noexcept;
    [[nodiscard]] double matrix_seconds() const noexcept;
    [[nodiscard]] int matrix_observed_workers() const noexcept;
    [[nodiscard]] const std::vector<int>& warm_start() const noexcept;
    [[nodiscard]] Evaluation evaluate(
        const std::vector<int>& selected,
        fode::PersistentExecutor& executor
    ) const;

private:
    std::unique_ptr<Data> data_;
    friend RunResult run(const Problem&, const RunConfig&);
};

[[nodiscard]] const char* case_name(CaseId value) noexcept;
[[nodiscard]] RunResult run(const Problem& problem, const RunConfig& config);

}  // namespace core99::y13
