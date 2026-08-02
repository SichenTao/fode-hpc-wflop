/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: L0649 FLOWERS-AEP model, analytical gradient and WFLO
Paper/DOI: LoCascio et al., FLOWERS AEP: An Analytical Model for Wind Farm
Layout Optimization; 10.1002/WE.2954.
Primary paper: local 18-page PDF SHA-256
e058017dbbaa6f21831b34097eec239b7ae410513f650eec7206d75dcb7f86fe.
Public source: https://github.com/locascio-m/flowers, paper-era revision
dcb729f7ea4ab9307344e45c329b6f50796e861b (2024-05-22). The repository
contains the FLOWERS equations, analytical gradient, WR1-WR9 WIND Toolkit
pickles, the nine-turbine problem and SNOPT wrapper. No license file or
license declaration is present at that revision, so it is used as a semantic
and numerical oracle only and no author code or pickle is redistributed.
Paper-provided facts: NREL 5 MW rotor diameter D=126 m; wake expansion k=0.05
for offshore flow; WR7; a 14D square; nine turbines initialized as a 3x3 array
with 4D spacing; ten Fourier modes; analytical AEP gradient; SNOPT optimality
tolerance 1e-3 and feasibility tolerance 1e-4; reported FLOWERS objective
gain 13.8 percent, 52 major steps and 1.8 s on the authors' machine.
Missing: licensed SNOPT binary/state, pyOptSparse and FLORIS revision locks,
the author optimization history/output file, final coordinates, CPU/software
environment, and RNG state for the two 200-case randomized experiments.
Paper/source conflict: Section 3 defines random layouts on an (N+1)-by-6
node grid. Paper-era tools.py revision dcb729f uses (N+1)-by-(N+1); current
revision 749303c corrects the second dimension to six. Paper mathematics and
the corrected current source control any random-layout extension.
Reconstruction: independently implement the printed nondimensional FLOWERS
Equations 16 and 33/38-42. The ten WR7 Fourier coefficients are independently
recomputed from the public paper-linked WR7 data and NREL-5MW tables and are
validated against the author implementation. Proprietary SNOPT is replaced by
a deterministic projected limited-memory BFGS optimizer using the same
analytical gradient, square bounds and 1e-3 projected-gradient tolerance.
HPC realization: all O(N^2 M) ordered turbine-pair terms are evaluated in a
persistent all-core C++20 team; pair slots and AEP/gradient reductions retain
fixed order. The paper-native nine-turbine optimization and a separate
paper-scale N=500, M=10 evaluation are both available. Small problems may use
fewer participating workers when their nine independent target rows expose
less concurrency than the machine.
Method semantic ID: l0649_flowers_aep_analytic_gradient_projected_lbfgs_v1.
Problem semantic ID: l0649_wr7_nine_turbine_14d_square_v1.
Protocol semantic ID: l0649_native_single_optimization_plus_n500_h6_v1.
Controlling contract: shared/contracts/core99_l0649_flowers_aep_2024.json.
Claim boundary: source-oracled flexible academic reproduction of the target
FLOWERS model, analytical gradient and paper-native WFLO problem; not the
author source distribution, SNOPT trajectory, FLORIS baselines, randomized
case corpus, postprocessed AEP table identity or machine timing replay.
Last evidence-audit date: 2026-08-01
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#pragma once

#include "fode/executor.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace core99::l0649 {

struct Point {
    double x_m = 0.0;
    double y_m = 0.0;
    bool operator==(const Point&) const = default;
};

struct Evaluation {
    double aep_wh = 0.0;
    std::vector<Point> gradient_wh_per_m;
    int turbines = 0;
    int fourier_modes = 0;
    int requested_workers = 0;
    int observed_workers = 0;
    std::uint64_t ordered_pair_terms = 0;
    double seconds = 0.0;
};

struct IterationReceipt {
    int iteration = 0;
    double objective = 0.0;
    double aep_wh = 0.0;
    double projected_gradient_inf = 0.0;
    double accepted_step = 0.0;
};

struct RunConfig {
    int workers = 20;
    int maximum_iterations = 200;
    double optimality_tolerance = 1.0e-3;
    bool smoke = false;
};

struct RunResult {
    std::string problem_semantic_id =
        "l0649_wr7_nine_turbine_14d_square_v1";
    std::string method_semantic_id =
        "l0649_flowers_aep_analytic_gradient_projected_lbfgs_v1";
    std::string protocol_semantic_id =
        "l0649_native_single_optimization_plus_n500_h6_v1";
    int requested_workers = 0;
    int observed_workers = 0;
    int iterations = 0;
    std::uint64_t objective_gradient_calls = 0;
    Evaluation initial_evaluation;
    Evaluation final_evaluation;
    std::vector<Point> final_layout;
    std::vector<IterationReceipt> history;
    double objective_gain_percent = 0.0;
    double evaluator_seconds = 0.0;
    double algorithm_seconds = 0.0;
    double end_to_end_seconds = 0.0;
    std::uint64_t scientific_hash = 0;
};

class FlowersModel {
public:
    explicit FlowersModel(int workers, int fourier_modes = 10);

    [[nodiscard]] int fourier_modes() const noexcept;
    [[nodiscard]] double rotor_diameter_m() const noexcept;
    [[nodiscard]] double boundary_side_m() const noexcept;
    [[nodiscard]] const std::vector<Point>& initial_layout() const noexcept;
    [[nodiscard]] Evaluation evaluate(
        const std::vector<Point>& layout,
        bool gradient,
        fode::PersistentExecutor& executor
    ) const;

private:
    int workers_ = 1;
    int modes_ = 10;
    std::vector<Point> initial_layout_;
};

[[nodiscard]] std::vector<Point> make_paper_scale_layout(
    int turbines,
    std::uint64_t seed = 649
);
[[nodiscard]] RunResult run(const FlowersModel&, const RunConfig&);

}  // namespace core99::l0649
