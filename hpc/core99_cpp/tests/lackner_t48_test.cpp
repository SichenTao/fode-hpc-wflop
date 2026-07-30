/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: bounded T48 semantic tests
Authority/claim: include/core99/lackner_t48.hpp; anchors are flexible academic
reproduction checks, not claims of author-exact hidden data or search history
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "core99/lackner_t48.hpp"

#include <cassert>
#include <cmath>
#include <iostream>

int main() {
    const core99::t48::Problem problem;
    assert(problem.dimension() == 4);
    const auto initial = problem.evaluate(
        core99::t48::paper_initial_layout()
    );
    const auto final = problem.evaluate(
        core99::t48::paper_reported_final_layout()
    );
    assert(initial.constraint_violation == 0.0);
    assert(final.constraint_violation == 0.0);
    assert(initial.lcoe_dollars_per_kwh > final.lcoe_dollars_per_kwh);
    assert(std::abs(initial.lcoe_dollars_per_kwh - 0.105) < 0.025);
    assert(std::abs(final.lcoe_dollars_per_kwh - 0.051) < 0.015);
    assert(std::abs(final.capital_cost_dollars - 4.5e6) < 0.75e6);
    std::cout << "core99_t48_cpp_pass\n";
    return 0;
}
