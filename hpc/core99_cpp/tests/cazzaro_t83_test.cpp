/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T83 paper-role, three-scale, constraint and HPC regression
Paper DOI: 10.1016/j.apenergy.2022.118830
Public source, missing information and completion: include/core99/cazzaro_t83.hpp
Claim boundary: tests the academic reconstruction, not author UK-array replay.
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "core99/cazzaro_t83.hpp"

#include <cassert>
#include <cmath>
#include <iostream>

int main() {
    const auto cases = core99::t83::paper_cases();
    assert(cases.size() == 8U);
    assert(cases.front().seed_role == 'A');
    assert(cases.back().seed_role == 'H');
    assert(cases.front().paper_rectangle_npv_meur == 4705.9);
    core99::t83::Problem problem(CORE99_T31_DATA_ROOT, 'A', 4);
    assert(problem.source_candidate_positions() > problem.candidate_positions());
    assert(problem.candidate_positions() >= 100);
    core99::t83::RunConfig config;
    config.workers = 1;
    config.seed = 83017;
    config.fixed_micro_cycles = 2;
    config.micro_time_seconds = 0.0;
    config.macro_cell_axis_override = 1;
    const auto serial = core99::t83::run(problem, config);
    config.workers = 4;
    const auto parallel = core99::t83::run(problem, config);
    assert(serial.turbines == 100);
    assert(serial.macro_rectangles_evaluated == 30);
    assert(serial.meso_positions.size() == 100U);
    assert(serial.optimized_shape_positions.size() == 100U);
    assert(serial.optimized_rectangle_positions.size() == 100U);
    assert(serial.meso_shape.feasible);
    assert(serial.optimized_shape.feasible);
    assert(serial.optimized_rectangle.feasible);
    assert(std::abs(serial.macro_rectangle.npv_meur - 4705.9) < 1.0e-8);
    assert(serial.scientific_hash == parallel.scientific_hash);
    assert(serial.optimized_shape_positions == parallel.optimized_shape_positions);
    assert(serial.optimized_rectangle_positions
        == parallel.optimized_rectangle_positions);
    assert(parallel.observed_workers >= 2);
    std::cout << "T83 C++ reconstruction tests passed\n";
    return 0;
}
