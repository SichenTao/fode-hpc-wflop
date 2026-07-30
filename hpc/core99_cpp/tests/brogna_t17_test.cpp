/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T17 bounded equation and geometry semantic tests
Paper/DOI: A New Wake Model and Comparison of Eight Algorithms for Layout
Optimization of Wind Farms in Complex Terrain; 10.1016/j.apenergy.2019.114189
Public source/missing/reconstruction: include/core99/brogna_t17.hpp
Controlling contract: shared/contracts/core99_t17_brogna_2020.json
Claim boundary: equation/geometry tests, not private-site numerical replay
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "core99/brogna_t17.hpp"

#include <cassert>
#include <cmath>
#include <iostream>

int main() {
    const auto layout = core99::t17::paper_figure_2_layout();
    assert(layout.size() == 25U);
    double minimum = 1.0e100;
    for (std::size_t left = 0; left < layout.size(); ++left) {
        for (std::size_t right = left + 1U; right < layout.size(); ++right) {
            minimum = std::min(minimum, std::hypot(
                layout[left].x_m - layout[right].x_m,
                layout[left].y_m - layout[right].y_m
            ));
        }
    }
    assert(minimum >= 4.0 * 93.0);
    const double centre_near =
        core99::t17::gaussian_deficit_ratio(3.0, 0.0, 0.747);
    const double centre_far =
        core99::t17::gaussian_deficit_ratio(10.0, 0.0, 0.747);
    const double radial =
        core99::t17::gaussian_deficit_ratio(3.0, 2.0, 0.747);
    assert(centre_near > centre_far);
    assert(centre_far > radial);
    assert(core99::t17::gaussian_deficit_ratio(41.0, 0.0, 0.747) == 0.0);
    std::cout << "core99_t17_cpp_pass\n";
    return 0;
}
