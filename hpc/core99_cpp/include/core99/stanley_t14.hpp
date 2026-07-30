/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T14 Stanley--Ning boundary-grid paper package
Paper/DOI: Massive Simplification of the Wind Farm Layout Optimization
Problem; 10.5194/wes-4-663-2019
Public source: https://github.com/byuflowlab/stanley2019-variable-reduction
at revision 62b590065f9541c4296338b3f1a0ee07cfcd28bc; archival DOI
10.5281/zenodo.3523383
Provided assets: Python 2/OpenMDAO/SNOPT drivers for direct, grid, and
boundary-grid layouts; Fortran wake, layout, constraint, and differentiated
models; three wind resources; three boundaries; experiment scripts
Missing/conflicts: SNOPT is proprietary and absent; the legacy Akima module is
absent; there is no root LICENSE although setupRevision.py declares Apache
License 2.0; paper turbine cut-in/generator values and released driver values
differ slightly; author random states and result files are absent
Reconstruction and resolution: paper-native seven unique cases, 100 turbines,
three representations, 100 starts, 2D spacing, 24x5 optimization quadrature,
and 360x50 final quadrature are retained; the five-variable boundary-grid
decoder follows paper plus public equations; the unavailable SNOPT driver is
replaced by a deterministic feasibility-first parallel evolution strategy and
is never described as author SNOPT; paper equations have priority where paper
and driver differ, while source-only constants are registered in the contract
Method/problem semantic IDs: t14_boundary_grid_parameterization_v1;
t14_stanley_2019_seven_unique_cases_v1
Controlling contract: shared/contracts/core99_t14_stanley_2019.json
Production backend: pure C++ CPU-HPC; persistent candidate-batch workers
during search and persistent wind-state workers during final evaluation
Claim boundary: academic declared reproduction of the paper contribution and
problem protocol, not author-exact SNOPT, gradient, RNG, or numeric replay
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#pragma once

#include "fode/executor.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace core99::t14 {

struct Point {
    double x = 0.0;
    double y = 0.0;
};

enum class Boundary {
    amalia,
    circle,
    square,
};

enum class WindRose {
    north_island,
    ukiah,
    victorville,
};

enum class Representation {
    direct,
    grid,
    boundary_grid,
};

struct Case {
    std::string id;
    double average_spacing_diameters = 4.0;
    Boundary boundary = Boundary::amalia;
    WindRose wind_rose = WindRose::north_island;
};

struct Evaluation {
    double optimization_aep_gwh = 0.0;
    double final_aep_gwh = 0.0;
    double constraint_violation_m = 0.0;
};

struct RunResult {
    std::string algorithm_id;
    std::string problem_id;
    std::vector<Point> best_layout;
    Evaluation best_evaluation;
    std::uint64_t seed = 0;
    std::uint64_t physical_fes = 0;
    std::uint64_t physical_fes_limit = 0;
    int requested_workers = 0;
    int observed_workers = 0;
    double evaluator_seconds = 0.0;
    double algorithm_seconds = 0.0;
    double end_to_end_seconds = 0.0;
    std::uint64_t scientific_hash = 0;
};

[[nodiscard]] std::vector<Case> paper_cases();
[[nodiscard]] std::vector<std::string> algorithm_ids();
[[nodiscard]] Representation representation_from_id(const std::string& id);

class Problem {
public:
    explicit Problem(Case paper_case);

    [[nodiscard]] const Case& paper_case() const noexcept;
    [[nodiscard]] const std::vector<Point>& boundary_vertices() const noexcept;
    [[nodiscard]] bool contains(const Point& point) const noexcept;
    [[nodiscard]] double constraint_violation(
        const std::vector<Point>& layout
    ) const noexcept;
    [[nodiscard]] double evaluate_optimization(
        const std::vector<Point>& layout
    ) const;
    [[nodiscard]] double evaluate_final(
        const std::vector<Point>& layout,
        fode::PersistentExecutor& executor
    ) const;

private:
    Case paper_case_;
    std::vector<Point> boundary_vertices_;
};

[[nodiscard]] std::vector<Point> decode_reference_layout(
    const Problem& problem,
    Representation representation,
    std::uint64_t seed
);

[[nodiscard]] RunResult run(
    const Problem& problem,
    const std::string& algorithm_id,
    std::uint64_t seed,
    std::uint64_t physical_fes_limit,
    int workers
);

}  // namespace core99::t14
