/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: PPGA Nantong-structured declared reconstruction determinism, FES, and receipt test
Method semantic ID: ppga_nantong_structured_3d_declared_reconstruction_v1
Problem semantic ID: ppga_nantong_structured_3d_declared_proxy_v1
Evidence tiers: M3_DECLARED_COMPLETION on P3_DECLARED_PROXY
Claim boundary: verifies the declared reconstruction state machine only
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "ppga/evolution.hpp"
#include "ppga/problem.hpp"

#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

bool same_science(
    const ppga::EvolutionResult& left,
    const ppga::EvolutionResult& right
) {
    return left.problem_semantic_hash == right.problem_semantic_hash
        && left.physical_fes == right.physical_fes
        && left.generations == right.generations
        && left.best_layout_1based == right.best_layout_1based
        && left.best_evaluation.expected_power_kw
            == right.best_evaluation.expected_power_kw
        && left.best_evaluation.ideal_expected_power_kw
            == right.best_evaluation.ideal_expected_power_kw
        && left.best_evaluation.conversion_efficiency
            == right.best_evaluation.conversion_efficiency
        && left.best_evaluation.cost_per_expected_power
            == right.best_evaluation.cost_per_expected_power
        && left.population_layout_hash == right.population_layout_hash
        && left.best_layout_hash == right.best_layout_hash
        && left.work.pairwise_layout_distances
            == right.work.pairwise_layout_distances
        && left.work.crossover_gene_choices
            == right.work.crossover_gene_choices
        && left.work.mutation_gene_trials
            == right.work.mutation_gene_trials
        && left.work.mutation_events == right.work.mutation_events
        && left.work.perturbed_individuals
            == right.work.perturbed_individuals
        && left.work.power_law_gene_steps
            == right.work.power_law_gene_steps
        && left.work.duplicate_repairs == right.work.duplicate_repairs;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        if (argc != 2) {
            throw std::runtime_error("usage: evolution_test CASE_MANIFEST");
        }
        const ppga::Problem problem = ppga::load_problem(
            argv[1], "PPGA_NantongStructured_WS1_tn20"
        );
        ppga::EvolutionConfig serial_config;
        serial_config.seed = 20260729;
        serial_config.physical_fes = 95;
        serial_config.workers = 1;
        const ppga::EvolutionResult serial =
            ppga::run(serial_config, problem);
        require(serial.physical_fes == 95, "exact partial terminal FES");
        require(serial.generations == 3, "generation count");
        require(serial.best_layout_1based.size() == 20, "complete best layout");
        require(serial.work.pairwise_layout_distances == 3U * 435U,
                "pairwise work receipt");
        require(serial.work.mutation_gene_trials == 90U,
                "mutation trials receipt");
        require(serial.work.power_law_gene_steps
                    == serial.work.perturbed_individuals,
                "power-law work receipt");
        require(serial.initialization_stage.parallel_regions == 1,
                "initialization region");
        require(serial.evaluator_stage.parallel_regions == 4,
                "evaluator regions include partial batch");
        require(serial.diversity_adaptation_stage.parallel_regions == 6,
                "diversity and adaptation regions");
        require(serial.variation_repair_stage.parallel_regions == 3,
                "variation regions");

        ppga::EvolutionConfig parallel_config = serial_config;
        parallel_config.workers = 0;
        const ppga::EvolutionResult parallel =
            ppga::run(parallel_config, problem);
        require(same_science(serial, parallel),
                "one-vs-all-visible exact scientific equivalence");
        require(parallel.initialization_stage.distinct_participants >= 1,
                "initialization participant receipt");
        require(parallel.evaluator_stage.task_items == 95,
                "evaluator exact task receipt");
        require(
            ppga::result_to_json(parallel).find(
                "\"method_semantic_id\": "
                "\"ppga_nantong_structured_3d_declared_reconstruction_v1\""
            ) != std::string::npos,
            "JSON semantic identity"
        );

        bool rejected_backend = false;
        try {
            ppga::EvolutionConfig invalid = serial_config;
            invalid.backend = "gpu";
            (void)ppga::run(invalid, problem);
        } catch (const std::invalid_argument&) {
            rejected_backend = true;
        }
        require(rejected_backend, "GPU backend must fail closed");
        std::cout << "PPGA evolution equivalence and receipts passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
