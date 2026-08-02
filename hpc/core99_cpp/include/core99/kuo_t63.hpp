/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T63 complex-terrain MIP and iterative CFD-update package
Paper title/DOI: Wind Farm Layout Optimization on Complex Terrains -
Integrating a CFD Wake Model with Mixed-Integer Programming;
10.1016/j.apenergy.2016.06.085
Public source: no paper-linked code/data. HiGHS v1.15.1 commit
04024d701f79feb8e2f18bc3df0dffc04ef05088 (MIT) is the open MILP backend.
Paper-provided assets: Eqs. (1)-(5); six-step CFD-MIP lifecycle; 2.8 km square
20x20 grid; 20 turbines; 12 sectors; 140 m pitch; CT=0.8; hub=77 m; D=80 m;
1.5 MW; five-diameter spacing; C={1,.7,.4,.2,0}; 30-second MIP report; printed
iteration, CFD-count, objective, efficiency, and runtime anchors
Target contribution: iterative coupling that solves the discrete MIP,
simulates newly promising single-turbine locations, updates their wake rows,
and stops when no new location is selected
Missing/conflicts: author terrain array, numeric wind rose, no-turbine and
single-turbine CFD fields, flat-terrain wake field, Gurobi model/source,
solver tolerances, layouts, per-iteration arrays, and seeds are unavailable.
The printed total runtime is dominated by private CFD and is not portable.
Reconstruction: digitize Figures 4-5; calculate Eq.(5) background speed; use
a declared Jensen initial row and terrain-aware single-turbine surrogate row;
linearize every selected quadratic x_i*x_j term exactly; solve the resulting
MILP with HiGHS; retain paper relaxation values and convergence lifecycle
Problem semantic ID: t63_carleton_figure_proxy_cfd_surrogate_v1
Method semantic ID: t63_iterative_cfd_mip_highs_reconstruction_v1
Production backend: pure C++ CPU plus pinned HiGHS C++ MILP; pair-field
generation uses a persistent executor, each case preserves sequential MIP
updates, and the five independent relaxation cases partition the node budget
Controlling contract: shared/contracts/core99_t63_kuo_2016.json
Claim boundary: academic declared reproduction of equations, discrete problem,
relaxation study, exact MILP linearization, and iterative update lifecycle on
a figure/surrogate proxy; not author CFD, Gurobi, or numerical replay
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#pragma once

#include "fode/executor.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace core99::t63 {

struct IterationReceipt {
    int iteration = 0;
    int new_cfd_locations = 0;
    int cumulative_cfd_locations = 0;
    int cfd_simulations = 0;
    double mip_objective = 0.0;
    double mip_dual_bound = 0.0;
    double mip_gap = 0.0;
    double mip_seconds = 0.0;
    std::string mip_status;
    std::vector<int> selected_cells;
};

struct RunConfig {
    double relaxation = 0.2;
    int workers = 20;
    // At most 400 locations can become newly known. One final solve proves
    // that no new location remains, so 401 is the finite semantic ceiling.
    int maximum_iterations = 401;
    double mip_time_limit_seconds = 30.0;
};

struct RunResult {
    std::string problem_semantic_id;
    std::string method_semantic_id;
    double relaxation = 0.0;
    int requested_workers = 0;
    int observed_workers = 0;
    int iterations = 0;
    int cfd_locations = 0;
    int cfd_simulations = 0;
    double final_true_objective = 0.0;
    double no_wake_upper_bound = 0.0;
    double layout_efficiency = 0.0;
    double field_generation_seconds = 0.0;
    double mip_seconds = 0.0;
    double end_to_end_seconds = 0.0;
    std::vector<int> final_layout;
    std::vector<IterationReceipt> history;
    std::uint64_t scientific_hash = 0;
};

class Problem {
public:
    explicit Problem(const std::string& proxy_path);
    [[nodiscard]] const std::string& semantic_id() const noexcept;
    [[nodiscard]] int grid_size() const noexcept;
    [[nodiscard]] int turbine_count() const noexcept;
    [[nodiscard]] double elevation_m(int cell) const;
    [[nodiscard]] double wind_probability(int sector) const;
    [[nodiscard]] double background_speed_mps(int cell, int sector) const;
    [[nodiscard]] bool spacing_conflict(int left_cell, int right_cell) const;

private:
    struct Data;
    std::shared_ptr<const Data> data_;
    std::string semantic_id_;
    friend RunResult run(const Problem&, const RunConfig&);
};

[[nodiscard]] RunResult run(const Problem& problem, const RunConfig& config);

}  // namespace core99::t63
