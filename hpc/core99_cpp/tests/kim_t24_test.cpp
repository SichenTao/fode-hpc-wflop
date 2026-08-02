/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T24 structural, evaluator, constraint, NSGA-III, and
deterministic parallel regression tests
Paper/DOI: Optimization of a Wind Farm Layout to Mitigate the Wind Power
Intermittency; 10.1016/j.apenergy.2024.123383
Public source, missing assets, paper-internal data conflict, reconstruction
completion, semantic IDs, production backend, and claim boundary:
hpc/core99_cpp/include/core99/kim_t24.hpp
Controlling contract: shared/contracts/core99_t24_kim_2024.json
Independent equation oracle: scripts/validate_core99_t24.py
Claim boundary: academic flexible reconstruction, not author numerical replay
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "core99/kim_t24.hpp"

#include "fode/executor.hpp"

#include <cassert>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <vector>

int main() {
    using core99::t24::CaseId;
    using core99::t24::Problem;
    const std::vector<Problem> problems = {
        Problem(CaseId::uniform_p0),
        Problem(CaseId::uniform_p007),
        Problem(CaseId::uniform_p015),
        Problem(CaseId::real_p0),
        Problem(CaseId::real_p007),
        Problem(CaseId::real_p015),
    };
    const std::vector<std::string> ids = {
        "t24_uniform_threshold_000_n25",
        "t24_uniform_threshold_007_n25",
        "t24_uniform_threshold_015_n25",
        "t24_real_threshold_000_n25",
        "t24_real_threshold_007_n25",
        "t24_real_threshold_015_n25",
    };
    for (std::size_t index = 0; index < problems.size(); ++index) {
        const auto& problem = problems[index];
        assert(problem.id() == ids[index]);
        assert(problem.turbine_count() == 25);
        assert(problem.wind_state_count() == 144);
        assert(problem.paper_population() == 92);
        assert(problem.paper_reference_intervals() == 91);
        assert(problem.paper_minimum_generations() == 1000);
        assert(problem.declared_maximum_generations() == 2000);
        assert(problem.declared_repeats() == 25);
        const auto layout = problem.reference_layout();
        assert(layout.size() == 25U);
        const auto evaluation = problem.evaluate(layout);
        assert(evaluation.feasible);
        assert(evaluation.mean_power_mw > 0.0);
        assert(evaluation.intermittency_mw >= 0.0);
        assert(std::isfinite(evaluation.mean_power_mw));
        assert(std::isfinite(evaluation.intermittency_mw));
    }
    assert(
        problems[1].evaluate(problems[1].reference_layout())
            .intermittency_mw
        <= problems[0].evaluate(problems[0].reference_layout())
            .intermittency_mw
    );
    assert(
        problems[2].evaluate(problems[2].reference_layout())
            .intermittency_mw
        <= problems[1].evaluate(problems[1].reference_layout())
            .intermittency_mw
    );

    auto invalid = problems[0].reference_layout();
    invalid[1] = invalid[0];
    const auto invalid_evaluation = problems[0].evaluate(invalid);
    assert(!invalid_evaluation.feasible);
    assert(invalid_evaluation.spacing_violation_m > 0.0);

    const auto base = problems[0].reference_layout();
    std::vector<std::vector<core99::t24::Turbine>> layouts(40, base);
    fode::PersistentExecutor serial_executor(1);
    fode::PersistentExecutor parallel_executor(4);
    parallel_executor.reset_work_receipt();
    const auto serial_values =
        problems[0].evaluate_population(layouts, serial_executor);
    const auto parallel_values =
        problems[0].evaluate_population(layouts, parallel_executor);
    for (std::size_t index = 0; index < layouts.size(); ++index) {
        assert(
            serial_values[index].mean_power_mw
            == parallel_values[index].mean_power_mw
        );
        assert(
            serial_values[index].intermittency_mw
            == parallel_values[index].intermittency_mw
        );
    }
    assert(parallel_executor.work_receipt().distinct_participants >= 2);

    assert(
        std::abs(
            problems[0].model_problem_power_mw(5.0, 9.0, 0.0)
            - 3.2624
        ) < 0.8
    );

    core99::t24::RunConfig config;
    config.seed = 24123383;
    config.population = 20;
    config.generations = 1;
    config.workers = 1;
    const auto serial = core99::t24::run(problems[0], config);
    config.workers = 4;
    const auto parallel = core99::t24::run(problems[0], config);
    assert(serial.physical_fes == 40U);
    assert(parallel.physical_fes == 40U);
    assert(serial.scientific_hash == parallel.scientific_hash);
    assert(serial.front.size() == parallel.front.size());
    assert(!parallel.front.empty());
    assert(parallel.observed_workers >= 2);
    assert(parallel.evaluator_seconds > 0.0);
    assert(parallel.end_to_end_seconds > 0.0);
    std::cout << "T24 C++ reconstruction checks passed\n";
    return EXIT_SUCCESS;
}
