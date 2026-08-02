/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T19 mathematical identity, official SRMP and CPU-HPC
tests
Paper DOI: 10.1016/j.energy.2021.120035.
Public source: pinned official SRMP v1.01.
Missing fields, declared Reconstruction, semantic IDs, Claim boundary and GPL
optional-target license: include/core99/dhoot_t19.hpp.
Contract: shared/contracts/core99_t19_dhoot_2021.json.
Last evidence-audit date: 2026-08-01
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "core99/dhoot_t19.hpp"

#include <cassert>
#include <cmath>
#include <iostream>

int main() {
    using namespace core99::t19;
    assert(paper_roles().size() == 112U);

    const Problem historical({
        ProblemFamily::historical, WindRegime::wr1, 100, 26
    });
    assert(historical.cells().size() == 100U);
    assert(historical.wind_states().size() == 1U);
    assert(std::abs(historical.minimum_spacing_requirement_m() - 100.0) < 1e-12);
    std::vector<int> regular(26);
    for (int index = 0; index < 26; ++index) regular[index] = index;
    const Evaluation historical_evaluation = historical.evaluate(regular);
    assert(historical_evaluation.exact_cardinality);
    assert(historical_evaluation.spacing_feasible);
    assert(historical_evaluation.expected_power_kw > 0.0);

    SolveConfig serial_config;
    serial_config.workers = 1;
    serial_config.maximum_iterations = 40;
    serial_config.time_limit_seconds = 30.0;
    serial_config.requested_triplets = 24;
    const SolveReceipt serial = historical.solve(serial_config);
    assert(serial.repaired_cardinality == 26);
    assert(serial.evaluation.exact_cardinality);
    assert(serial.evaluation.spacing_feasible);
    assert(serial.generated_triplets == 24);
    assert(serial.evaluation.physical_fes == 1U);

    SolveConfig parallel_config = serial_config;
    parallel_config.workers = 4;
    const SolveReceipt parallel = historical.solve(parallel_config);
    assert(parallel.layout == serial.layout);
    assert(parallel.evaluation.aep_gwh == serial.evaluation.aep_gwh);
    assert(parallel.evaluation.qip_wake_objective
        == serial.evaluation.qip_wake_objective);
    assert(parallel.scientific_hash == serial.scientific_hash);
    assert(parallel.observed_workers >= 2);

    const Problem fine({
        ProblemFamily::realistic, WindRegime::wr1, 2500, 2
    });
    assert(std::abs(fine.minimum_spacing_requirement_m() - 315.0) < 1e-12);
    const Evaluation adjacent = fine.evaluate({0, 1});
    assert(!adjacent.spacing_feasible);
    const Evaluation separated = fine.evaluate({0, 3});
    assert(separated.spacing_feasible);

    std::cout << "T19 C++ flexible-reproduction tests passed\n";
    return 0;
}
