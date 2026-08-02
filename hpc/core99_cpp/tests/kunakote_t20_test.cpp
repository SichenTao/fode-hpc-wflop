/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: bounded T20 semantic tests
Paper title and DOI: Comparative Performance of Twelve Metaheuristics for
Wind Farm Layout Optimisation, 10.1007/s11831-021-09586-7.
Public source: no paper-linked author code or data archive was located.
Missing fields and Reconstruction: include/core99/kunakote_t20.hpp.
Semantic IDs and Contract: shared/contracts/core99_t20_kunakote_2022.json.
Authority and Claim boundary: tests enforce paper problem
semantics and do not claim author-exact optimizer results
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "core99/kunakote_t20.hpp"

#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>

int main() {
    const core99::t20::Problem case1("t20_case1_variable_hub");
    const core99::t20::Problem case2("t20_case2_variable_partial");
    const core99::t20::Problem case3("t20_case3_fixed39_hub");
    const core99::t20::Problem case4("t20_case4_fixed39_partial");
    assert(case1.dimension() == 100);
    assert(case2.uses_partial_overlap());
    assert(case3.dimension() == 39);
    assert(case4.has_fixed_turbine_count());

    std::vector<double> binary(100, 0.0);
    binary[0] = 1.0;
    binary[99] = 1.0;
    const auto decoded_binary = case1.decode(binary);
    assert(decoded_binary.size() == 2U);
    assert(decoded_binary[0].x_m == 100.0);
    assert(decoded_binary[0].y_m == 100.0);
    assert(decoded_binary[1].x_m == 1900.0);
    assert(decoded_binary[1].y_m == 1900.0);

    std::vector<double> fixed(39, 1.0);
    const auto decoded_fixed = case3.decode(fixed);
    assert(decoded_fixed.size() == 39U);
    for (std::size_t index = 1; index < decoded_fixed.size(); ++index) {
        assert(
            decoded_fixed[index].x_m != decoded_fixed[index - 1].x_m
            || decoded_fixed[index].y_m != decoded_fixed[index - 1].y_m
        );
    }

    const auto hub = case1.evaluate_layout(
        core99::t20::paper_figure_5_layout()
    );
    const auto partial = case2.evaluate_layout(
        core99::t20::paper_figure_5_layout()
    );
    assert(hub.turbine_count == 54);
    assert(std::abs(hub.cost - 36.11265015433061) < 1.0e-12);
    assert(hub.constraint_violation == 0.0);
    assert(partial.average_power_kw > hub.average_power_kw);
    assert(std::isfinite(hub.objective));
    std::cout << "core99_t20_cpp_pass\n";
    return 0;
}
