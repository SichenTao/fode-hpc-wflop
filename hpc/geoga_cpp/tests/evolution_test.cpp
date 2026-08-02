/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: GeoGA Anholt-structured declared mechanism, exact-FES, replay, and one-versus-all-visible science test
Paper title: A Geometric Mutation-Based Genetic Algorithm for Irregular Large-Scale Offshore Wind Farm Layout Optimization
DOI: 10.1109/CBD69312.2025.00059
Public asset/source: no author implementation or numerical Anholt data found; evidence dossier docs/source-dossiers/L0726.json
Missing information: original Anholt inputs and author test oracle
Reconstruction: verifies the frozen geoga_declared_reconstruction_v1 execution semantics
Method semantic ID: geoga_declared_reconstruction_v1
Execution profile ID: geoga_anholt_structured_p3_execution_v1
Problem semantic ID: geoga_anholt_structured_declared_proxy_v1
Evidence tiers: admitted M3 method on P3 declared proxy
Controlling contract: shared/contracts/geoga_anholt_structured_execution_contract.json
Claim boundary: verifies only the declared execution profile and not the original Anholt experiment
Last evidence-audit date: 2026-07-30
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "geoga/evolution.hpp"
#include "geoga/problem.hpp"

#include <algorithm>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

bool same_science(
    const geoga::EvolutionResult& left,
    const geoga::EvolutionResult& right
) {
    return left.method_semantic_id == right.method_semantic_id
        && left.execution_profile_id == right.execution_profile_id
        && left.problem_semantic_id == right.problem_semantic_id
        && left.problem_semantic_hash == right.problem_semantic_hash
        && left.case_id == right.case_id
        && left.seed == right.seed
        && left.physical_fes == right.physical_fes
        && left.generations == right.generations
        && left.best_layout_0based == right.best_layout_0based
        && left.best_evaluation.aep_kwh == right.best_evaluation.aep_kwh
        && left.best_evaluation.no_wake_aep_kwh
            == right.best_evaluation.no_wake_aep_kwh
        && left.best_evaluation.capacity_factor
            == right.best_evaluation.capacity_factor
        && left.best_layout_hash == right.best_layout_hash
        && left.population_layout_hash == right.population_layout_hash
        && left.work.roulette_parent_draws
            == right.work.roulette_parent_draws
        && left.work.crossover_gene_copies
            == right.work.crossover_gene_copies
        && left.work.duplicate_repairs
            == right.work.duplicate_repairs
        && left.work.geometry_distance_checks
            == right.work.geometry_distance_checks
        && left.work.geometry_mutations
            == right.work.geometry_mutations
        && left.work.survivor_candidates_ranked
            == right.work.survivor_candidates_ranked;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        if (argc != 2) {
            throw std::runtime_error("usage: evolution_test CASE_MANIFEST");
        }
        const geoga::Problem problem = geoga::load_problem(argv[1]);
        std::vector<int> fixture(111);
        std::iota(fixture.begin(), fixture.end(), 0);
        const int first_choice = geoga::mechanism::nearest_free_replacement(
            problem, fixture, 0, 0.0
        );
        const int fifth_choice = geoga::mechanism::nearest_free_replacement(
            problem, fixture, 0, 0.999999999999
        );
        require(first_choice != fifth_choice, "top-five selection fixture");
        require(
            std::find(fixture.begin(), fixture.end(), first_choice)
                == fixture.end(),
            "replacement must be free"
        );

        geoga::EvolutionConfig one_config;
        one_config.seed = 20260729;
        one_config.physical_fes = 95;
        one_config.workers = 1;
        const geoga::EvolutionResult one =
            geoga::run(one_config, problem);
        geoga::EvolutionConfig all_config = one_config;
        all_config.workers = 0;
        const geoga::EvolutionResult all =
            geoga::run(all_config, problem);
        require(same_science(one, all), "one/all scientific equivalence");
        require(one.physical_fes == 95, "exact partial terminal FES");
        require(one.generations == 1, "partial generation count");
        require(one.method_semantic_id == geoga::kMethodSemanticId,
                "method semantic identity");
        require(one.execution_profile_id == geoga::kExecutionProfileId,
                "execution profile identity");
        require(one.best_layout_0based.size() == 111, "best layout size");
        require(
            std::adjacent_find(
                one.best_layout_0based.begin(),
                one.best_layout_0based.end()
            ) == one.best_layout_0based.end(),
            "best layout uniqueness"
        );
        const geoga::LayoutEvaluation replay =
            geoga::evaluate_layout(problem, one.best_layout_0based);
        require(replay.aep_kwh == one.best_evaluation.aep_kwh,
                "best AEP replay");
        require(
            replay.no_wake_aep_kwh
                == one.best_evaluation.no_wake_aep_kwh,
            "best no-wake replay"
        );
        require(
            replay.capacity_factor
                == one.best_evaluation.capacity_factor,
            "best capacity-factor replay"
        );
        require(one.work.roulette_parent_draws == 90,
                "roulette work count");
        require(one.work.geometry_mutations == 45,
                "geometry mutation work count");
        require(one.work.duplicate_repairs == 38,
                "admitted repair trajectory fixture");
        require(
            one.work.geometry_distance_checks == 45ULL * 69ULL,
            "geometry distance work count"
        );
        require(one.initialization_stage.parallel_regions == 1,
                "initialization stage receipt");
        require(one.variation_repair_stage.parallel_regions == 1,
                "variation stage receipt");
        require(one.evaluator_stage.parallel_regions == 2,
                "evaluator stage receipt");
        require(one.best_layout_hash == "38586c5be1c4592c",
                "admitted transition best-layout fixture");
        require(one.population_layout_hash == "ac49817b9dfc98cf",
                "admitted transition population fixture");
        require(
            one.best_evaluation.aep_kwh == 1975430052.9163921,
            "admitted transition objective fixture"
        );

        geoga::EvolutionConfig unsupported = one_config;
        unsupported.backend = "gpu";
        bool failed_closed = false;
        try {
            static_cast<void>(geoga::run(unsupported, problem));
        } catch (const std::exception&) {
            failed_closed = true;
        }
        require(failed_closed, "unsupported GPU backend must fail closed");
        std::cout
            << "GeoGA Anholt structured exact science passed workers=1,"
            << all.resolved_workers << " physical_fes=95\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
