/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T82 structural, evaluator, constraint, and deterministic
parallel regression tests
Paper/DOI: 10.1016/j.apenergy.2022.119599
Public source, missing assets, conflicts, reconstruction, semantic IDs, and
claim boundary: hpc/core99_cpp/include/core99/cao_t82.hpp
Controlling contract: shared/contracts/core99_t82_cao_2022.json
Independent equation oracle: scripts/validate_core99_t82.py
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "core99/cao_t82.hpp"

#include "fode/executor.hpp"

#include <cassert>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <vector>

int main() {
    using core99::t82::CaseId;
    using core99::t82::Problem;
    const Problem ideal_one(CaseId::ideal_single);
    const Problem ideal_two(CaseId::ideal_multi);
    const Problem zhuanghe(CaseId::zhuanghe);
    assert(ideal_one.id() == "t82_ideal_case_i_n30");
    assert(ideal_two.id() == "t82_ideal_case_ii_n39");
    assert(zhuanghe.id() == "t82_zhuanghe_n72");
    assert(ideal_one.turbine_count() == 30);
    assert(ideal_two.turbine_count() == 39);
    assert(zhuanghe.turbine_count() == 72);
    assert(ideal_one.wind_state_count() == 1);
    assert(ideal_two.wind_state_count() == 108);
    assert(zhuanghe.wind_state_count() == 96);
    assert(ideal_one.paper_population() == 100);
    assert(ideal_one.paper_generations() == 20);
    assert(ideal_one.paper_repeats() == 25);

    for (const Problem* problem : {&ideal_one, &ideal_two, &zhuanghe}) {
        const auto layout = problem->reference_layout();
        assert(
            layout.size()
            == static_cast<std::size_t>(problem->turbine_count())
        );
        for (const auto& turbine : layout) {
            assert(problem->inside(turbine.x_m, turbine.y_m));
        }
        const auto evaluation = problem->evaluate(layout);
        assert(evaluation.feasible);
        assert(evaluation.expected_power_kw > 0.0);
        assert(std::isfinite(evaluation.expected_power_kw));
        assert(
            evaluation.maximum_comprehensive_turbulence >= 0.1
            || problem->case_id() == CaseId::zhuanghe
        );
        assert(
            std::isfinite(
                evaluation.maximum_comprehensive_turbulence
            )
        );
    }

    auto invalid = ideal_one.reference_layout();
    invalid[1].x_m = invalid[0].x_m;
    invalid[1].y_m = invalid[0].y_m;
    const auto invalid_evaluation = ideal_one.evaluate(invalid);
    assert(!invalid_evaluation.feasible);
    assert(invalid_evaluation.spacing_violation_m > 0.0);

    const auto base = ideal_one.reference_layout();
    std::vector<std::vector<core99::t82::Turbine>> layouts(40, base);
    fode::PersistentExecutor serial_executor(1);
    fode::PersistentExecutor parallel_executor(4);
    parallel_executor.reset_work_receipt();
    const auto serial_values =
        ideal_one.evaluate_population(layouts, serial_executor);
    const auto parallel_values =
        ideal_one.evaluate_population(layouts, parallel_executor);
    assert(serial_values.size() == parallel_values.size());
    for (std::size_t index = 0; index < serial_values.size(); ++index) {
        assert(
            serial_values[index].expected_power_kw
            == parallel_values[index].expected_power_kw
        );
        assert(
            serial_values[index].maximum_comprehensive_turbulence
            == parallel_values[index].maximum_comprehensive_turbulence
        );
    }
    assert(parallel_executor.work_receipt().distinct_participants >= 2);

    core99::t82::RunConfig config;
    config.seed = 82119599;
    config.population = 8;
    config.generations = 1;
    config.workers = 1;
    const auto serial = core99::t82::run(ideal_one, config);
    config.workers = 4;
    const auto parallel = core99::t82::run(ideal_one, config);
    assert(serial.physical_fes == 16U);
    assert(parallel.physical_fes == 16U);
    assert(serial.scientific_hash == parallel.scientific_hash);
    assert(serial.front.size() == parallel.front.size());
    assert(!parallel.front.empty());
    assert(parallel.observed_workers >= 2);
    assert(parallel.evaluator_seconds > 0.0);
    assert(parallel.end_to_end_seconds > 0.0);
    std::cout << "T82 C++ reconstruction checks passed\n";
    return EXIT_SUCCESS;
}
