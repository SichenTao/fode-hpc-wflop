/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T64 paper-case, penalty, numerical, and HPC regression
tests
Paper DOI: The Impact of Land Use Constraints in Multi-Objective
Energy-Noise Wind Farm Layout Optimization; 10.1016/j.renene.2015.06.026
Public source: no target source or native paper arrays were located.
Related public source:
https://gitlab.windenergy.dtu.dk/TOPFARM/PyWake.git at revision
5b07481ec9b3633a74844651648f266ba82a8b32 for an independent ISO check.
Missing/conflicts/reconstruction:
hpc/core99_cpp/include/core99/sorkhabi_t64.hpp
Test scope: all nine main paper cases, all four uniformity-map roles, static,
dynamic, and death penalty modes, exact physical evaluation accounting, and
one/all-worker scientific identity.
Method/problem semantic IDs: t64_nsga2_three_penalties_declared_reconstruction_v1;
t64_energy_noise_land13role_declared_reconstruction_v1
Controlling contract: shared/contracts/core99_t64_sorkhabi_2016.json
HPC design: persistent workers execute feasible population construction,
evaluation, offspring variation, death replacement, and dominance work.
Claim boundary: regression of the declared academic reconstruction, not author
code, native maps, native wind arrays, random streams, or numerical replay.
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "core99/sorkhabi_t64.hpp"

#include <cassert>
#include <cmath>
#include <iostream>
#include <set>
#include <string>
#include <vector>

namespace {

void check_paper_roles() {
    const auto roles = core99::t64::paper_case_ids();
    assert(roles.size() == 13U);
    std::set<std::string> unique(roles.begin(), roles.end());
    assert(unique.size() == 13U);
    for (const int availability : {70, 80, 90}) {
        for (const int turbines : {5, 10, 15}) {
            const core99::t64::Problem problem(
                availability,
                turbines
            );
            assert(problem.land_availability_percent() == availability);
            assert(problem.turbine_count() == turbines);
            assert(problem.map_variant() == 0);
            assert(
                std::abs(
                    problem.measured_land_availability()
                    - static_cast<double>(availability) / 100.0
                ) < 0.02
            );
            assert(!problem.receptors().empty());
            assert(std::isfinite(problem.uniformity_parameter()));
        }
    }
    std::set<double> uniformities;
    for (int map = 0; map < 4; ++map) {
        const core99::t64::Problem problem(80, 10, map);
        assert(problem.map_variant() == map);
        uniformities.insert(problem.uniformity_parameter());
    }
    assert(uniformities.size() == 4U);
}

void check_penalty_modes() {
    const core99::t64::Problem problem(90, 5);
    const std::vector<core99::t64::PenaltyMode> modes{
        core99::t64::PenaltyMode::static_1e4,
        core99::t64::PenaltyMode::static_4e4,
        core99::t64::PenaltyMode::dynamic_cgen_ngen,
        core99::t64::PenaltyMode::dynamic_cgen_half_ngen,
        core99::t64::PenaltyMode::death,
    };
    for (const auto mode : modes) {
        core99::t64::RunConfig config;
        config.seed = 64016;
        config.workers = 4;
        config.physical_fes = 300;
        config.penalty_mode = mode;
        const auto result = core99::t64::run(problem, config);
        assert(result.physical_fes == 300U);
        assert(result.population_size == 100);
        assert(result.observed_workers >= 2);
        assert(!result.front.empty());
        assert(result.evaluator_seconds > 0.0);
        assert(result.algorithm_seconds >= 0.0);
        assert(result.end_to_end_seconds > 0.0);
        for (const auto& point : result.front) {
            assert(std::isfinite(point.aep_gwh));
            assert(std::isfinite(point.maximum_spl_dba));
            assert(point.layout.size() == 5U);
        }
    }
}

void check_schedule_independence() {
    const core99::t64::Problem problem(90, 5);
    core99::t64::RunConfig config;
    config.seed = 6464;
    config.workers = 1;
    config.physical_fes = 300;
    config.penalty_mode =
        core99::t64::PenaltyMode::dynamic_cgen_ngen;
    const auto serial = core99::t64::run(problem, config);
    config.workers = 4;
    const auto parallel = core99::t64::run(problem, config);
    assert(serial.physical_fes == parallel.physical_fes);
    assert(serial.scientific_hash == parallel.scientific_hash);
    assert(serial.front.size() == parallel.front.size());
    assert(parallel.observed_workers >= 2);
}

}  // namespace

int main() {
    check_paper_roles();
    check_penalty_modes();
    check_schedule_independence();
    std::cout << "T64 C++ reconstruction tests passed\n";
    return 0;
}
