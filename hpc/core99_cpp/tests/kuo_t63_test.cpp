/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T63 bounded proxy and paper-geometry semantic tests
Paper/DOI: Wind Farm Layout Optimization on Complex Terrains - Integrating a
CFD Wake Model with Mixed-Integer Programming;
10.1016/j.apenergy.2016.06.085
Public source/missing/reconstruction: include/core99/kuo_t63.hpp
Controlling contract: shared/contracts/core99_t63_kuo_2016.json
Claim boundary: proxy/geometry tests, not author CFD numerical replay
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "core99/kuo_t63.hpp"

#include <cassert>
#include <cmath>
#include <iostream>

int main(int argc, char** argv) {
    assert(argc == 2);
    const core99::t63::Problem problem(argv[1]);
    assert(problem.grid_size() == 20);
    assert(problem.turbine_count() == 20);
    double probability = 0.0;
    for (int sector = 0; sector < 12; ++sector) {
        probability += problem.wind_probability(sector);
    }
    assert(std::abs(probability - 1.0) < 1.0e-6);
    assert(problem.wind_probability(9) > 0.30);
    assert(problem.elevation_m(0) >= 100.0);
    assert(problem.elevation_m(399) <= 600.0);
    assert(problem.background_speed_mps(0, 0) > 0.0);
    assert(problem.spacing_conflict(0, 1));
    assert(!problem.spacing_conflict(0, 3));
    std::cout << "core99_t63_cpp_pass\n";
    return 0;
}
