/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: bounded semantic tests for T01/T02 historical grid
Authority and claim boundary: shared/contracts/core99_mosetti_grady_cases.json;
tests do not upgrade the declared reconstruction to author-original identity
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "core99/historical_grid.hpp"

#include <cassert>
#include <cmath>
#include <iostream>

int main() {
    const auto one = core99::layout_from_cells({1});
    const auto duplicate_free = core99::layout_cells(one);
    assert(duplicate_free.size() == 1);
    assert(duplicate_free.front() == 1);

    const core99::HistoricalGridProblem case_a("t01_mosetti_case_a");
    const auto isolated = case_a.evaluate(one);
    assert(isolated.turbine_count == 1);
    assert(std::abs(isolated.expected_power_kw - 518.4) < 1.0e-10);
    assert(isolated.objective > 0.0);

    const auto two_crosswind = core99::layout_from_cells({1, 2});
    const auto crosswind = case_a.evaluate(two_crosswind);
    assert(std::abs(crosswind.expected_power_kw - 1036.8) < 1.0e-9);

    const auto two_downwind = core99::layout_from_cells({1, 11});
    const auto downwind = case_a.evaluate(two_downwind);
    assert(downwind.expected_power_kw < crosswind.expected_power_kw);

    const core99::HistoricalGridProblem case_b("t02_grady_case_b");
    const auto rotational = case_b.evaluate(two_crosswind);
    assert(rotational.expected_power_kw > 0.0);
    assert(rotational.expected_power_kw < crosswind.expected_power_kw);

    const core99::HistoricalGridProblem body(
        "t02_grady_case_c_body1000"
    );
    const core99::HistoricalGridProblem abstract(
        "t02_grady_case_c_abstract2500"
    );
    const auto body_value = body.evaluate(two_downwind);
    const auto abstract_value = abstract.evaluate(two_downwind);
    assert(
        std::abs(
            body_value.expected_power_kw - abstract_value.expected_power_kw
        ) < 1.0e-12
    );

    const auto t01 = core99::historical_profile("t01_mosetti_ga");
    const auto t02 = core99::historical_profile("t02_grady_island_ga");
    assert(core99::default_physical_fes(t01, case_a.id()) == 80200);
    assert(
        core99::default_physical_fes(t02, "t02_grady_case_a") == 1800600
    );
    assert(
        core99::default_physical_fes(
            t02,
            "t02_grady_case_c_body1000"
        ) == 600600
    );
    assert(
        core99::default_physical_fes(
            t02,
            "t02_grady_case_c_abstract2500"
        ) == 1500600
    );
    std::cout << "core99_historical_grid_cpp_pass\n";
    return 0;
}
