/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: bounded T62 semantic tests
Paper title/DOI: Optimization of Wind Turbine Layout Position in a Wind Farm
Using a Newly-Developed Two-Dimensional Wake Model;
10.1016/j.apenergy.2016.04.098
Public source: none located
Missing and reconstruction: author layouts/history are absent; completion
rules are declared in include/core99/gao_t62.hpp
Semantic IDs: t62_gao_case_b_grid_jensen_gaussian_v1;
t62_mpga_declared_reconstruction_v1
Controlling contract: shared/contracts/core99_t62_gao_2016.json
Claim boundary: printed anchors are flexible academic checks, not claims of
hidden author-layout or history replay
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "core99/gao_t62.hpp"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <vector>

int main() {
    const core99::t62::Problem one(1);
    const auto isolated = one.evaluate_layout({{100.0, 100.0}});
    assert(isolated.constraint_violation == 0.0);
    assert(std::abs(isolated.average_power_kw - 518.4) < 1.0e-10);
    assert(std::abs(isolated.efficiency - 1.0) < 1.0e-12);

    const core99::t62::Problem thirty_eight(38);
    std::vector<std::uint32_t> genes(76U, 0U);
    const auto layout = thirty_eight.decode(genes);
    assert(layout.size() == 38U);
    const auto value = thirty_eight.evaluate_layout(layout);
    assert(value.constraint_violation == 0.0);
    assert(value.average_power_kw > 0.0);
    assert(value.efficiency > 0.0 && value.efficiency <= 1.0);

    const double near = core99::t62::improved_wake_speed_ratio(
        2.5, 0.0, 0.62, 0.10
    );
    const double far = core99::t62::improved_wake_speed_ratio(
        10.0, 0.0, 0.62, 0.10
    );
    assert(near > 0.0 && near < far && far < 1.0);
    std::cout << "core99_t62_cpp_pass\n";
    return 0;
}
