/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: Y16 BMM/IMM, BDA, role and HPC regression tests
Paper DOI: 10.1109/TSTE.2026.3686029
First-party supporting patent: CN121683298A/CN121683298B
Public asset, missing information, conflicts, corrections, reconstruction,
semantic IDs, production backend, controlling contract and claim boundary:
include/core99/huang_y16.hpp
Claim boundary: flexible academic reconstruction, not author code, private
site/wind/terrain arrays, Gurobi or numerical replay.
Last evidence-audit date: 2026-08-01
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "core99/huang_y16.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>

int main() {
    const auto scenarios=core99::y16::paper_scenarios();
    assert(scenarios.size()==31U);
    assert(std::count_if(scenarios.begin(),scenarios.end(),[](const auto& item) {
        return item.model==core99::y16::ModelKind::bmm;
    })==9);
    assert(std::count_if(scenarios.begin(),scenarios.end(),[](const auto& item) {
        return item.model==core99::y16::ModelKind::imm;
    })==22);
    const auto iterator=std::find_if(
        scenarios.begin(),scenarios.end(),[](const auto& item) {
            return item.case_id=="Y16_case1_type3_n40_g2p5_imm_ac_i3";
        }
    );
    assert(iterator!=scenarios.end());
    assert(iterator->turbine.diameter_m==155.0);
    assert(iterator->turbine_count==40);
    assert(iterator->ti_intervals==3);

    core99::y16::RunConfig config;
    config.angle_start=1;
    config.angle_count=2;
    config.pattern_start=0;
    config.pattern_count=1;
    config.maximum_bda_iterations=2;
    config.mip_time_limit_seconds=30.0;
    config.workers=1;
    const auto serial=core99::y16::run(*iterator,config);
    config.workers=4;
    const auto parallel=core99::y16::run(*iterator,config);
    assert(serial.layout.size()==40U);
    assert(serial.evaluation.feasible);
    assert(serial.evaluation.minimum_spacing_m>=5.0*155.0-1.0e-5);
    assert(serial.scientific_hash==parallel.scientific_hash);
    assert(serial.selected_angle_degrees==parallel.selected_angle_degrees);
    assert(serial.selected_pattern==parallel.selected_pattern);
    assert(parallel.observed_workers>=2);
    assert(parallel.evaluator_rejected_subproblems==0);
    std::cout << "Y16 C++ flexible-reproduction tests passed\n";
    return 0;
}
