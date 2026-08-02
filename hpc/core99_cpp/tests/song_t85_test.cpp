/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T85 structural, equation, constraint, case, and
deterministic-parallel regression tests
Paper/DOI: 10.1016/j.renene.2023.02.058
Public source, cited predecessor, missing assets, reconstruction decisions,
semantic IDs, and claim boundary:
hpc/core99_cpp/include/core99/song_t85.hpp
Independent equation oracle: scripts/validate_core99_t85.py
Controlling contract: shared/contracts/core99_t85_song_2023.json
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#ifdef NDEBUG
#undef NDEBUG
#endif

#include "core99/song_t85.hpp"

#include "fode/executor.hpp"

#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>

int main() {
    using core99::t85::CaseId;
    using core99::t85::Problem;

    const Problem wf1(CaseId::wf1);
    const Problem wf1_u6(CaseId::wf1_u6);
    const Problem wf1_v112(CaseId::wf1_v112);
    const Problem wf2(CaseId::wf2);
    const Problem wf3(CaseId::wf3);
    const Problem wf4(CaseId::wf4);

    assert(wf1.id() == "t85_wf1_v80_u8_n25");
    assert(wf1_u6.id() == "t85_wf1_v80_u6_n25");
    assert(wf1_v112.id() == "t85_wf1_v112_u8_n25");
    assert(wf2.id() == "t85_wf2_v80_u8_n25");
    assert(wf3.id() == "t85_wf3_v80_u8_n36");
    assert(wf4.id() == "t85_wf4_v80_uneven_n25");

    for (const Problem* problem :
         {&wf1, &wf1_u6, &wf1_v112, &wf2, &wf3, &wf4}) {
        assert(problem->wind_state_count() == 8);
        assert(
            problem->decision_dimension()
            == problem->turbine_count() * 10
        );
        assert(problem->declared_population() == 500);
        assert(problem->declared_physical_fes() == 10000U);
        assert(problem->declared_repeats() == 25);
        const auto layout = problem->reference_layout();
        const auto evaluation = problem->evaluate(layout);
        assert(evaluation.feasible);
        assert(evaluation.aep_gwh > 0.0);
        assert(std::isfinite(evaluation.aep_gwh));
    }

    const auto wf1_evaluation = wf1.evaluate(wf1.reference_layout());
    const auto u6_evaluation = wf1_u6.evaluate(wf1_u6.reference_layout());
    const auto v112_evaluation =
        wf1_v112.evaluate(wf1_v112.reference_layout());
    const auto wf2_evaluation = wf2.evaluate(wf2.reference_layout());
    const auto wf3_evaluation = wf3.evaluate(wf3.reference_layout());
    const auto wf4_evaluation = wf4.evaluate(wf4.reference_layout());
    assert(wf1_evaluation.aep_gwh > u6_evaluation.aep_gwh);
    assert(v112_evaluation.aep_gwh > wf1_evaluation.aep_gwh);
    assert(wf2_evaluation.aep_gwh > wf1_evaluation.aep_gwh);
    assert(wf3_evaluation.aep_gwh > wf2_evaluation.aep_gwh);
    assert(wf4_evaluation.aep_gwh > wf1_evaluation.aep_gwh);

    auto infeasible = wf1.reference_layout();
    infeasible[1].x_m = infeasible[0].x_m;
    infeasible[1].y_m = infeasible[0].y_m;
    const auto infeasible_evaluation = wf1.evaluate(infeasible);
    assert(!infeasible_evaluation.feasible);
    assert(infeasible_evaluation.spacing_violation_m > 0.0);
    wf1.repair(infeasible, 1234, 0, 0);
    assert(wf1.evaluate(infeasible).feasible);

    const auto reference = wf1.reference_layout();
    const std::vector<std::vector<core99::t85::TurbineDecision>> layouts = {
        reference, reference, reference, reference,
    };
    fode::PersistentExecutor one_executor(1);
    fode::PersistentExecutor four_executor(4);
    const auto one = wf1.evaluate_population(layouts, one_executor);
    const auto four = wf1.evaluate_population(layouts, four_executor);
    assert(one.size() == four.size());
    for (std::size_t index = 0; index < one.size(); ++index) {
        assert(one[index].aep_gwh == four[index].aep_gwh);
        assert(one[index].feasible == four[index].feasible);
    }

    core99::t85::RunConfig config;
    config.seed = 20260731;
    config.population = 40;
    config.physical_fes_limit = 80;
    config.workers = 1;
    const auto serial = core99::t85::run(wf1, config);
    config.workers = 4;
    const auto parallel = core99::t85::run(wf1, config);
    assert(serial.physical_fes == 80U);
    assert(parallel.physical_fes == 80U);
    assert(serial.scientific_hash == parallel.scientific_hash);
    assert(serial.best_aep_gwh == parallel.best_aep_gwh);
    assert(serial.best_aep_gwh >= serial.initial_best_aep_gwh);
    assert(serial.observed_workers == 1);
    assert(parallel.observed_workers >= 2);

    std::cout << "T85 C++ structural tests passed\n";
    return 0;
}
