/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: bounded T04 semantic tests
Authority/claim: include/core99/uwflo_t04.hpp; tests preserve the declared
reproduction boundary and do not claim author-exact values
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "core99/uwflo_t04.hpp"

#include <cassert>
#include <iostream>
#include <vector>

int main() {
    const core99::t04::Problem case1("t04_uwflo_case1_n9");
    const core99::t04::Problem case2("t04_uwflo_case2_n9");
    const core99::t04::Problem study("t04_uwflo_case3_ii_f5");
    assert(case1.dimension() == 18);
    assert(case2.dimension() == 27);
    assert(study.turbine_count() == 18);
    assert(core99::t04::paper_physical_fes(case1) == 15000);
    assert(core99::t04::paper_physical_fes(case2) == 25000);

    std::vector<double> layout{
        0.0, 0.84, 1.68, 0.0, 0.84, 1.68, 0.0, 0.84, 1.68,
        0.0, 0.0, 0.0, 0.36, 0.36, 0.36, 0.72, 0.72, 0.72
    };
    const auto value = case1.evaluate(layout);
    assert(value.farm_power_w > 0.0);
    assert(value.farm_efficiency > 0.0);
    assert(value.constraint_violation == 0.0);

    layout[1] = layout[0];
    layout[10] = layout[9];
    assert(case1.evaluate(layout).constraint_violation > 0.0);
    std::cout << "core99_t04_cpp_pass\n";
    return 0;
}
