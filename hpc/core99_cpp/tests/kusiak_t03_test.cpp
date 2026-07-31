/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: bounded T03 semantic tests
Paper title and DOI: Design of Wind Farm Layout for Maximum Wind Energy
Capture, 10.1016/j.renene.2009.08.019.
Public source: no author implementation was located.
Missing fields and Reconstruction: include/core99/kusiak_t03.hpp.
Semantic IDs and Contract: shared/contracts/core99_t03_kusiak_cases.json.
Authority and Claim boundary: tests preserve the declared
reconstruction boundary and do not claim author-exact values
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "core99/kusiak_t03.hpp"

#include <cassert>
#include <cmath>
#include <iostream>

int main() {
    const core99::t03::Problem scenario1("t03_kusiak_s1_n2");
    const core99::t03::Problem scenario2("t03_kusiak_s2_n2");
    assert(scenario1.turbine_count() == 2);
    assert(scenario1.scenario() == 1);
    const std::vector<core99::t03::Point> separated{
        {-250.0, 0.0},
        {250.0, 0.0}
    };
    const auto first = scenario1.evaluate(separated);
    const auto second = scenario2.evaluate(separated);
    assert(first.expected_power_kw > 0.0);
    assert(first.inverse_power > 0.0);
    assert(first.constraint_violation == 0.0);
    assert(second.expected_power_kw > 0.0);
    assert(
        std::abs(first.expected_power_kw - second.expected_power_kw)
        > 1.0
    );
    const std::vector<core99::t03::Point> overlapping{
        {0.0, 0.0},
        {0.0, 0.0}
    };
    assert(scenario1.evaluate(overlapping).constraint_violation > 0.0);
    std::cout << "core99_t03_cpp_pass\n";
    return 0;
}
