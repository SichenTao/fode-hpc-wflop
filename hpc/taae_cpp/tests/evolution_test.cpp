/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: TAAE end-to-end declared-reconstruction scientific tests
Paper title: Transformer Autoencoder-Assisted Evolutionary Framework for Constrained Multiobjective 3D Wind Farm Layout Optimization
DOI: 10.1109/JAS.2026.126233
Public author method source/checkpoint: unavailable as recorded in docs/source-dossiers/Y36.json
Missing choices completed here: scalar ranking fixtures, latent and repair fixtures, exact terminal FES, bounded checkpoint replay, training-FES separation, front validity, and worker-count equality
Reconstruction status: bounded executable M3 engineering reconstruction on the declared P3 problem proxy
Method evidence tier: M3_DECLARED_COMPLETION
Method semantic ID: taae_transformer_evolution_declared_reconstruction_v1
Kernel semantic ID: taae_transformer_declared_reconstruction_v1
Problem semantic ID: taae_zhangbei_structured_declared_proxy_v1
Controlling contract: shared/contracts/taae_transformer_evolution_declared_reconstruction_contract.json
Claim boundary: bounded scientific fixtures only; original taae remains blocked, paper-scale state requires an immutable checkpoint, and no Zhangbei, reported-front, formal, performance, or GPU claim is made
END WFLOP IMPLEMENTATION FACT DECLARATION
*/

#include "fode/case.hpp"
#include "taae/evolution.hpp"

#include <cstdio>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

bool dominates(
    const taae::evolution::IndividualRecord& left,
    const taae::evolution::IndividualRecord& right
) {
    const bool no_worse =
        left.evaluation.reciprocal_expected_power_per_kw <=
            right.evaluation.reciprocal_expected_power_per_kw &&
        left.evaluation.average_a_weighted_noise_dba <=
            right.evaluation.average_a_weighted_noise_dba;
    const bool strict =
        left.evaluation.reciprocal_expected_power_per_kw <
            right.evaluation.reciprocal_expected_power_per_kw ||
        left.evaluation.average_a_weighted_noise_dba <
            right.evaluation.average_a_weighted_noise_dba;
    return no_worse && strict;
}

taae::ModelConfig tiny_model_config() {
    taae::ModelConfig config;
    config.vocabulary = 400;
    config.sequence_length = 15;
    config.model_dimension = 8;
    config.latent_dimension = 8;
    config.heads = 4;
    config.encoder_layers = 1;
    config.decoder_layers = 1;
    config.ffn_width = 16;
    config.regression_hidden_width = 8;
    return config;
}

void check_front(
    const taae::evolution::EvolutionResult& result
) {
    require(!result.front.empty(), "front is empty");
    for (std::size_t left = 0; left < result.front.size(); ++left) {
        require(
            result.front[left]
                    .evaluation.normalized_constraint_violation == 0.0,
            "front contains infeasible record"
        );
        for (std::size_t right = 0;
             right < result.front.size();
             ++right) {
            if (left != right) {
                require(
                    !dominates(result.front[right], result.front[left]),
                    "front contains dominated record"
                );
            }
        }
    }
}

}  // namespace

int main(int argc, char** argv) {
    try {
        if (argc != 2) {
            throw std::invalid_argument(
                "usage: taae_evolution_test PROXY_CASES"
            );
        }
        std::string report;
        require(
            taae::evolution::run_scalar_selection_fixtures(report),
            report
        );
        std::cout << report << '\n';
        require(
            taae::evolution::run_latent_operator_fixtures(report),
            report
        );
        std::cout << report << '\n';
        const fode::CaseData problem = fode::load_case(
            argv[1],
            "TAAE_Proxy_NC1_Budget800k_tn15"
        );
        require(
            taae::evolution::run_repair_and_duplicate_fixtures(
                problem,
                report
            ),
            report
        );
        std::cout << report << '\n';

        const std::string checkpoint =
            "taae_evolution_initial_checkpoint_test.bin";
        taae::evolution::EvolutionConfig preparation;
        preparation.seed = 20260729;
        preparation.maximum_physical_fes = 100;
        preparation.workers = 1;
        preparation.model_config = tiny_model_config();
        preparation.checkpoint_output = checkpoint;
        const auto prepared =
            taae::evolution::run_declared_reconstruction(
                preparation,
                problem
            );
        require(
            prepared.physical_fes == 100 &&
            prepared.training_work.training_physical_fes == 0 &&
            !prepared.checkpoint.file_sha256.empty(),
            "bounded checkpoint preparation failed"
        );
        const std::string prepared_json =
            taae::evolution::result_to_json(prepared);
        require(
            prepared.training_state_profile_id ==
                "taae_evolution_bounded_smoke_v1" &&
            prepared_json.find(
                "\"training_state_profile_id\":"
                "\"taae_evolution_bounded_smoke_v1\""
            ) != std::string::npos &&
            prepared_json.find("\"encoder_layers\":1") !=
                std::string::npos &&
            prepared_json.find("\"checkpoint\":{") !=
                std::string::npos &&
            prepared_json.find(prepared.checkpoint.file_sha256) !=
                std::string::npos,
            "bounded profile, architecture, or checkpoint JSON missing"
        );

        taae::evolution::EvolutionConfig serial = preparation;
        serial.maximum_physical_fes = 350;
        serial.checkpoint_output.clear();
        serial.checkpoint_input = checkpoint;
        serial.checkpoint_sha256 =
            prepared.checkpoint.file_sha256;
        const auto serial_result =
            taae::evolution::run_declared_reconstruction(
                serial,
                problem
            );
        taae::evolution::EvolutionConfig parallel = serial;
        parallel.workers = 20;
        const auto parallel_result =
            taae::evolution::run_declared_reconstruction(
                parallel,
                problem
            );
        require(
            serial_result.physical_fes == 350 &&
            serial_result.generations == 3,
            "exact terminal partial physical FES failed"
        );
        require(
            serial_result.training_work.training_physical_fes == 0 &&
            serial_result.training_work.fine_tuning_epochs == 30,
            "hidden training FES or epoch mismatch"
        );
        require(
            serial_result.model_hash == parallel_result.model_hash &&
            serial_result.population_layout_hash ==
                parallel_result.population_layout_hash &&
            serial_result.front_hash == parallel_result.front_hash &&
            serial_result.physical_fes == parallel_result.physical_fes &&
            serial_result.generations == parallel_result.generations,
            "1-worker/20-worker scientific equality failed"
        );
        check_front(serial_result);
        check_front(parallel_result);

        bool paper_scale_rejected = false;
        try {
            taae::evolution::EvolutionConfig blocked = preparation;
            blocked.training_profile =
                taae::evolution::TrainingStateProfile::
                    paper_scale_checkpoint;
            blocked.checkpoint_output.clear();
            static_cast<void>(
                taae::evolution::run_declared_reconstruction(
                    blocked,
                    problem
                )
            );
        } catch (const std::invalid_argument&) {
            paper_scale_rejected = true;
        }
        require(
            paper_scale_rejected,
            "paper-scale execution did not fail closed"
        );
        bool bounded_checkpoint_as_paper_rejected = false;
        try {
            taae::evolution::EvolutionConfig blocked = preparation;
            blocked.training_profile =
                taae::evolution::TrainingStateProfile::
                    paper_scale_checkpoint;
            blocked.checkpoint_input = checkpoint;
            blocked.checkpoint_sha256 =
                prepared.checkpoint.file_sha256;
            blocked.checkpoint_output.clear();
            static_cast<void>(
                taae::evolution::run_declared_reconstruction(
                    blocked,
                    problem
                )
            );
        } catch (const std::invalid_argument&) {
            bounded_checkpoint_as_paper_rejected = true;
        }
        require(
            bounded_checkpoint_as_paper_rejected,
            "bounded checkpoint was admitted as paper-scale state"
        );
        for (const std::string backend : {"gpu", "hybrid"}) {
            bool rejected = false;
            try {
                taae::evolution::EvolutionConfig blocked = preparation;
                blocked.backend = backend;
                blocked.checkpoint_output.clear();
                static_cast<void>(
                    taae::evolution::run_declared_reconstruction(
                        blocked,
                        problem
                    )
                );
            } catch (const std::invalid_argument&) {
                rejected = true;
            }
            require(rejected, backend + " backend did not fail closed");
        }
        std::remove(checkpoint.c_str());
        std::cout
            << "taae_evolution_smoke_pass population=100 physical_fes=350 "
            << "partial_batch=50 fine_tune_epochs=30 "
            << "training_physical_fes=0 checkpoint_replay=pass "
            << "workers=1,20 exact=pass front=feasible_nondominated "
            << "paper_scale_without_or_with_bounded_checkpoint=rejected "
            << "gpu_hybrid=rejected\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
