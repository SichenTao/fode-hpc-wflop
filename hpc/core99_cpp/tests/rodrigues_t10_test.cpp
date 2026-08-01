/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T10 geometry, evaluator, CHT, worker-identity and four-
algorithm smoke tests
Paper/DOI: 10.1016/j.rser.2016.07.021
Public source: no paper-linked author executable source was found.
Missing implementation details and declared reconstruction completions:
Full declaration: hpc/core99_cpp/include/core99/rodrigues_t10.hpp
Method semantic IDs: t10_mogomea_online_mi_v1;
t10_omogomea_offline_geographic_v1; t10_nsgaii_archive_growth_v1;
t10_clustered_nsgaii_archive_growth_v1
Claim boundary: academic flexible reproduction tests, not an author-result or
author-random-stream replay.
Controlling contract: shared/contracts/core99_t10_rodrigues_2016.json
Last evidence-audit date: 2026-08-01
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "core99/rodrigues_t10.hpp"

#include "fode/executor.hpp"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <bit>
#include <cassert>
#include <cstdint>
#include <iostream>

int main() {
    using core99::t10::Algorithm;
    using core99::t10::ConstraintHandling;
    using core99::t10::Problem;

    const auto cases = core99::t10::paper_case_ids();
    assert(cases.size() == 12U);
    assert(core99::t10::paper_role_ids(true).size() == 196U);
    for (const std::string& case_id : cases) {
        const Problem problem(case_id);
        const auto layout = problem.initial_layout(1001ULL, 0);
        assert(problem.feasible(layout));
        assert(problem.paper_case().variables
               == problem.paper_case().side_points
                    * problem.paper_case().side_points);
        if (problem.paper_case().grid_step_diameters == 8) {
            assert(problem.conflicts().empty());
        } else {
            assert(!problem.conflicts().empty());
        }
    }

    const Problem problem("t10_B_4");
    const auto layout = problem.initial_layout(2016ULL, 3);
    fode::PersistentExecutor one(1);
    fode::PersistentExecutor four(4);
    const auto serial = problem.evaluate(layout, ConstraintHandling::repair, one);
    const auto parallel = problem.evaluate(layout, ConstraintHandling::repair, four);
    assert(std::bit_cast<std::uint64_t>(serial.normalized_energy)
           == std::bit_cast<std::uint64_t>(parallel.normalized_energy));
    assert(std::bit_cast<std::uint64_t>(serial.efficiency)
           == std::bit_cast<std::uint64_t>(parallel.efficiency));

    auto infeasible = layout;
    infeasible.assign(static_cast<std::size_t>(problem.word_count()), ~0ULL);
    assert(problem.violation_count(infeasible) > 0);
    const auto repaired = problem.repair(infeasible, 301ULL, 0);
    assert(problem.feasible(repaired));
    const auto penalty = problem.evaluate_direct(
        infeasible, ConstraintHandling::penalty
    );
    assert(penalty.violating_pairs > 0);

    for (const Algorithm algorithm : {
            Algorithm::mogomea,
            Algorithm::offline_mogomea,
            Algorithm::nsgaii,
            Algorithm::clustered_nsgaii,
        }) {
        core99::t10::RunConfig config;
        config.algorithm = algorithm;
        config.constraint = ConstraintHandling::repair;
        config.seed = 7001ULL;
        config.workers = 4;
        config.maximum_physical_fes = 180;
        config.maximum_generations = 8;
        const auto receipt = core99::t10::optimize(problem, config);
        assert(receipt.physical_fes >= 20U);
        assert(receipt.physical_fes <= config.maximum_physical_fes);
        assert(receipt.archive_size > 0);
        assert(receipt.hypervolume > 0.0);
        assert(receipt.scientific_hash != 0U);
    }

    core99::t10::RunConfig identity;
    identity.algorithm = Algorithm::offline_mogomea;
    identity.constraint = ConstraintHandling::repair;
    identity.seed = 8001ULL;
    identity.maximum_physical_fes = 260;
    identity.maximum_generations = 8;
    identity.workers = 1;
    const auto one_receipt = core99::t10::optimize(problem, identity);
    identity.workers = 4;
    const auto four_receipt = core99::t10::optimize(problem, identity);
    assert(one_receipt.scientific_hash == four_receipt.scientific_hash);
    assert(one_receipt.physical_fes == four_receipt.physical_fes);

    std::cout << "T10 MOWFLOP and four-MOEA tests passed\n";
    return 0;
}
