/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: PPGA Nantong-structured declared reconstruction determinism, FES, and receipt test
Paper title: Advanced 3D Wind Farm Layout Optimization Framework via Power-Law Perturbation-Based Genetic Algorithm
DOI: 10.1109/JAS.2025.125351
Public asset/source: no author implementation or original Nantong arrays found; evidence dossier docs/source-dossiers/T43.json
Missing information: author source, full original transitions, and original Nantong numerical inputs
Reconstruction: verifies ppga_nantong_structured_3d_declared_reconstruction_v2
Method semantic ID: ppga_nantong_structured_3d_declared_reconstruction_v2
Problem semantic ID: ppga_nantong_structured_3d_declared_proxy_v1
Evidence tiers: M3_DECLARED_COMPLETION on P3_DECLARED_PROXY
Controlling contract: shared/contracts/ppga_nantong_structured_3d_declared_reconstruction_contract.json
Claim boundary: verifies the declared reconstruction state machine only
Last evidence-audit date: 2026-07-30
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
        && left.work.perturbation_gate_draws
            == right.work.perturbation_gate_draws
        && left.work.perturbed_individuals
            == right.work.perturbed_individuals
        && left.work.power_law_gene_steps
            == right.work.power_law_gene_steps
        && left.work.stagnation_parent_offspring_comparisons
            == right.work.stagnation_parent_offspring_comparisons
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
        require(
            !ppga::mechanism::perturbation_gate(0.0, 0.0, 0.0),
            "theta equal to threshold must not perturb"
        );
        require(
            ppga::mechanism::perturbation_gate(-0.25, 0.0, 0.249),
            "probability gate pass"
        );
        require(
            !ppga::mechanism::perturbation_gate(-0.25, 0.0, 0.25),
            "probability gate strict boundary"
        );
        require(
            ppga::mechanism::finite_support_power_law_step(431, 0.0) == 1,
            "finite-support inverse-CDF lower endpoint"
        );
        require(
            ppga::mechanism::finite_support_power_law_step(431, 1.0) == 431,
            "finite-support inverse-CDF upper endpoint"
        );
        const std::vector<int> raw_layout = {1, 100, 432};
        const std::vector<int> raw_perturbed =
            ppga::mechanism::perturb_every_dimension_unrepaired(
                raw_layout,
                432,
                {0.0, 0.5, 1.0},
                {0.25, 0.75, 0.25}
            );
        require(raw_perturbed.size() == 3, "per-dimension perturbation size");
        for (std::size_t coordinate = 0;
             coordinate < raw_perturbed.size();
             ++coordinate) {
            require(
                raw_perturbed[coordinate]
                    != raw_layout[coordinate],
                "every selected-individual dimension changes before repair"
            );
        }
        require(
            ppga::mechanism::offspring_parent_stagnant_proportion(
                {0.3, 0.9, 0.6},
                {0.4, 0.8, 0.6}
            ) == 2.0 / 3.0,
            "offspring-row versus parent-row stagnation"
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
        require(serial.work.mutation_gene_trials == 65U,
                "mutation trials receipt");
        require(
            serial.work.perturbation_gate_draws
                >= serial.work.perturbed_individuals,
            "probability-gate work receipt"
        );
        require(
            serial.work.power_law_gene_steps
                == serial.work.perturbed_individuals * 20U,
            "per-dimension power-law work receipt"
        );
        require(serial.work.stagnation_parent_offspring_comparisons == 60U,
                "pre-selection offspring-parent stagnation receipt");
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
                "\"ppga_nantong_structured_3d_declared_reconstruction_v2\""
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
