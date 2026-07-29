/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: BDE WS5/WS6 P3 scalar, layout, transition, exact-FES,
and one/all-visible scientific-equivalence regression
Paper title: Discrete Bi-Population Differential Evolution for Optimizing
Complex Wind Farm Layouts in Diverse Terrains
DOI: 10.1016/j.energy.2025.137885
Paper provides: scenario cardinalities, spacing, algorithm equations,
population 50, FES 10000, and Imax 400.
Public author code URL: https://raw.githubusercontent.com/toyamaailab/toyamaailab.github.io/main/resource/BDE-WindFarm_code.zip
Public author code revision or archive hash: sha256:f4a317d4d727a9d452f76376373e2c8ad5546e35ff19530eda0ba328682dd140
Public code/assets provide: mask cross-check, scalar/evaluator behavior, and
source-resolved fusion behavior.
Known missing information: original WS5/WS6 arrays and author conflict
adjudications.
Reconstruction performed here: frozen scalar/layout oracles, 75-FES state
fixture, exact FES, work receipts, fail-closed modes, and 1/all equivalence.
Method evidence tier: M2_CITATION_PREDECESSOR subtype
paper_equation_direct_source_resolved.
Problem evidence tier: P3_DECLARED_PROXY subtype composite_proxy.
Method semantic ID: bde_paper_equations_imax400_exact_fes_v1
Problem semantic ID: bde2025_ws5_paper250_declared_proxy_v1 and
bde2025_ws6_paper250_declared_proxy_v1
Controlling contracts: shared/contracts/bde_ws56_declared_proxy_contract.json
and shared/contracts/bde_ws56_transition_parity_audit.json
Claim boundary: validates only the declared composite profile; never the
paper's unavailable original cases or results.
Last evidence audit date: 2026-07-29
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "bde_ws56/evolution.hpp"

#include "fode/case.hpp"

#include <cmath>
#include <iostream>
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
    const bde_ws56::Result& left,
    const bde_ws56::Result& right
) {
    return left.method_semantic_id == right.method_semantic_id
        && left.execution_profile_id == right.execution_profile_id
        && left.problem_semantic_id == right.problem_semantic_id
        && left.case_id == right.case_id
        && left.objective_semantics_hash
            == right.objective_semantics_hash
        && left.feasible_set_hash == right.feasible_set_hash
        && left.seed == right.seed
        && left.physical_fes == right.physical_fes
        && left.generations == right.generations
        && left.schedule_imax == right.schedule_imax
        && left.population_size == right.population_size
        && left.requested_execution_mode
            == right.requested_execution_mode
        && left.resolved_execution_mode
            == right.resolved_execution_mode
        && left.work.complete_layout_evaluations
            == right.work.complete_layout_evaluations
        && left.work.ranked_individuals == right.work.ranked_individuals
        && left.work.fusion_memberships == right.work.fusion_memberships
        && left.work.mutation_vectors == right.work.mutation_vectors
        && left.work.crossover_gene_trials
            == right.work.crossover_gene_trials
        && left.work.forced_crossover_genes
            == right.work.forced_crossover_genes
        && left.work.repair_random_draws
            == right.work.repair_random_draws
        && left.work.accepted_replacements
            == right.work.accepted_replacements
        && left.best_layout_1based == right.best_layout_1based
        && left.best_expected_power_kw == right.best_expected_power_kw
        && left.no_wake_expected_power_kw
            == right.no_wake_expected_power_kw
        && left.conversion_efficiency_percent
            == right.conversion_efficiency_percent
        && left.best_layout_hash == right.best_layout_hash
        && left.population_layout_hash == right.population_layout_hash;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        if (argc != 2) {
            throw std::runtime_error("usage: evolution_test CASE_MANIFEST");
        }
        const fode::CaseData ws5 = fode::load_case(
            argv[1], "BDEWS5P3DAEtn30"
        );
        const fode::CaseData ws6 = fode::load_case(
            argv[1], "BDEWS6P3STDtn30"
        );
        require(
            bde_ws56::problem_semantic_id(ws5)
                == bde_ws56::kWs5ProblemSemanticId,
            "WS5 problem identity"
        );
        require(
            bde_ws56::problem_semantic_id(ws6)
                == bde_ws56::kWs6ProblemSemanticId,
            "WS6 problem identity"
        );
        require(
            bde_ws56::turbine_power_kw(1.999) == 0.0,
            "cut-in scalar oracle"
        );
        require(
            bde_ws56::turbine_power_kw(8.0) == 153.6,
            "cubic scalar oracle"
        );
        require(
            bde_ws56::turbine_power_kw(12.8) == 629.1,
            "rated scalar oracle"
        );
        require(
            bde_ws56::turbine_power_kw(17.999) == 629.1,
            "just below cut-out scalar oracle"
        );
        require(
            bde_ws56::turbine_power_kw(18.0) == 0.0,
            "cut-out boundary scalar oracle"
        );
        require(
            bde_ws56::turbine_power_kw(18.001) == 0.0,
            "above cut-out scalar oracle"
        );

        const std::vector<int> fixture = {
            11, 12, 13, 14, 15, 40, 41, 42, 43, 44,
            45, 69, 70, 71, 72, 73, 98, 99, 100, 101,
            102, 127, 128, 129, 130, 131, 155, 156, 157, 158,
        };
        require(
            std::abs(
                bde_ws56::evaluate_layout(ws5, fixture, 1)
                - 6625.9281689497684
            ) <= 1.0e-10 * 6625.9281689497684,
            "complete-layout scalar oracle"
        );
        require(
            bde_ws56::objective_semantics_hash(ws5)
                == "fnv1a64:0681d279ff856425",
            "full objective hash"
        );
        require(
            std::abs(
                bde_ws56::no_wake_expected_power_kw(ws5) - 10663.83
            ) <= 1.0e-12 * 10663.83,
            "no-wake expected-power scalar oracle"
        );
        require(
            bde_ws56::feasible_set_hash(ws5)
                == "fnv1a64:833b9504f906384f",
            "full feasible-set hash"
        );

        bde_ws56::Config one_config;
        one_config.seed = 20260729;
        one_config.physical_fes = 75;
        one_config.workers = 1;
        const bde_ws56::Result one = bde_ws56::run(one_config, ws5);
        bde_ws56::Config all_config = one_config;
        all_config.workers = 0;
        const bde_ws56::Result all = bde_ws56::run(all_config, ws5);
        require(same_science(one, all), "one/all scientific equivalence");
        require(one.physical_fes == 75, "exact selected-case FES");
        require(one.generations == 1, "one half-generation");
        require(one.schedule_imax == 400, "paper Imax schedule");
        require(
            one.method_semantic_id == bde_ws56::kMethodSemanticId,
            "distinct method identity"
        );
        require(
            one.work.complete_layout_evaluations == 75,
            "physical work receipt"
        );
        require(one.work.ranked_individuals == 50, "ranking receipt");
        require(one.work.fusion_memberships == 50, "fusion receipt");
        require(one.work.mutation_vectors == 25, "mutation receipt");
        require(
            one.work.crossover_gene_trials == 750,
            "crossover receipt"
        );
        require(
            one.work.forced_crossover_genes == 25,
            "forced crossover receipt"
        );
        require(
            one.initialization_stage.parallel_regions == 1,
            "initialization stage receipt"
        );
        require(
            one.fusion_variation_repair_stage.parallel_regions == 0
                && one.fusion_variation_repair_stage.task_items == 25,
            "granularity-aware fused variation-repair stage receipt"
        );
        require(
            one.evaluator_stage.parallel_regions > 0,
            "evaluator stage receipt"
        );
        require(
            std::abs(
                one.best_expected_power_kw - 10042.342862330535
            ) <= 1.0e-10 * 10042.342862330535,
            "transition objective fixture"
        );
        require(
            std::abs(one.no_wake_expected_power_kw - 10663.83)
                <= 1.0e-12 * 10663.83,
            "no-wake result oracle"
        );
        require(
            std::abs(
                one.conversion_efficiency_percent
                - 94.17200820278019
            ) <= 1.0e-10,
            "conversion-efficiency result oracle"
        );
        require(
            one.best_layout_hash == "fnv1a64:e756087b66420fd9",
            "transition best-layout fixture"
        );
        require(
            one.population_layout_hash == "fnv1a64:3497b72377aab6a6",
            "transition population-layout fixture"
        );

        for (const std::string mode : {"hybrid", "gpu"}) {
            bde_ws56::Config unsupported = one_config;
            unsupported.execution_mode = mode;
            bool failed_closed = false;
            try {
                static_cast<void>(bde_ws56::run(unsupported, ws5));
            } catch (const std::exception& error) {
                failed_closed =
                    std::string(error.what()).find(mode)
                    != std::string::npos;
            }
            require(
                failed_closed,
                "unsupported " + mode + " mode fails explicitly"
            );
        }
        std::cout
            << "BDE WS5/WS6 P3 exact science passed workers=1,"
            << all.resolved_workers << " physical_fes=75\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
