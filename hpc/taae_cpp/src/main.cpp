/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: TAAE end-to-end declared-reconstruction CPU CLI
Paper title: Transformer Autoencoder-Assisted Evolutionary Framework for Constrained Multiobjective 3D Wind Farm Layout Optimization
DOI: 10.1109/JAS.2026.126233
Public author method source/checkpoint: unavailable as recorded in docs/source-dossiers/Y36.json
Missing choices completed here: explicit P3 case selection, bounded versus checkpoint-gated paper-scale state, pre-repair decoded-solution filtering, no-feasible front labeling, checkpoint SHA-256 admission, terminal partial FES, all-visible CPU default, and unsupported backend rejection
Reconstruction status: bounded executable M3 engineering reconstruction on the declared P3 problem proxy
Method evidence tier: M3_DECLARED_COMPLETION
Method semantic ID: taae_transformer_evolution_declared_reconstruction_v1
Kernel semantic ID: taae_transformer_declared_reconstruction_v1
Problem semantic ID: taae_zhangbei_structured_declared_proxy_v1
Controlling contract: shared/contracts/taae_transformer_evolution_declared_reconstruction_contract.json
Claim boundary: distinct bounded end-to-end reconstruction only; original taae remains blocked, paper-scale state requires an immutable checkpoint, and no Zhangbei, reported-front, formal, performance, or GPU claim is made
END WFLOP IMPLEMENTATION FACT DECLARATION
*/

#include "fode/case.hpp"
#include "taae/evolution.hpp"

#include <cstdlib>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

struct Arguments {
    std::string cases;
    std::string case_id;
    std::uint64_t seed = 1;
    std::uint64_t physical_fes = 10000;
    int workers = 0;
    std::string profile = "bounded";
    std::string checkpoint_input;
    std::string checkpoint_sha256;
    std::string checkpoint_output;
    std::string backend = "cpu";
};

Arguments parse_arguments(int argc, char** argv) {
    Arguments result;
    for (int index = 1; index < argc; ++index) {
        const std::string option = argv[index];
        if (option == "--help") {
            std::cout
                << "usage: taae_evolution_hpc --cases FILE --case ID "
                << "[--seed N] [--physical-fes N] "
                << "[--workers N (default: all visible CPUs)] "
                << "[--profile bounded|paper-scale] "
                << "[--checkpoint-in FILE --checkpoint-sha256 SHA256] "
                << "[--checkpoint-out FILE] [--backend cpu]\n";
            std::exit(0);
        }
        if (index + 1 >= argc) {
            throw std::invalid_argument("missing value for " + option);
        }
        const std::string value = argv[++index];
        if (option == "--cases") {
            result.cases = value;
        } else if (option == "--case") {
            result.case_id = value;
        } else if (option == "--seed") {
            result.seed = std::stoull(value);
        } else if (option == "--physical-fes") {
            result.physical_fes = std::stoull(value);
        } else if (option == "--workers") {
            result.workers = std::stoi(value);
        } else if (option == "--profile") {
            result.profile = value;
        } else if (option == "--checkpoint-in") {
            result.checkpoint_input = value;
        } else if (option == "--checkpoint-sha256") {
            result.checkpoint_sha256 = value;
        } else if (option == "--checkpoint-out") {
            result.checkpoint_output = value;
        } else if (option == "--backend") {
            result.backend = value;
        } else {
            throw std::invalid_argument("unknown option " + option);
        }
    }
    if (result.cases.empty() || result.case_id.empty()) {
        throw std::invalid_argument("--cases and --case are required");
    }
    if (result.profile != "bounded" &&
        result.profile != "paper-scale") {
        throw std::invalid_argument("unknown training profile");
    }
    return result;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const Arguments arguments = parse_arguments(argc, argv);
        taae::evolution::EvolutionConfig config;
        config.seed = arguments.seed;
        config.maximum_physical_fes = arguments.physical_fes;
        config.workers = arguments.workers;
        config.training_profile =
            arguments.profile == "bounded"
                ? taae::evolution::TrainingStateProfile::bounded_smoke
                : taae::evolution::TrainingStateProfile::
                      paper_scale_checkpoint;
        config.checkpoint_input = arguments.checkpoint_input;
        config.checkpoint_sha256 = arguments.checkpoint_sha256;
        config.checkpoint_output = arguments.checkpoint_output;
        config.backend = arguments.backend;
        const fode::CaseData problem =
            fode::load_case(arguments.cases, arguments.case_id);
        const auto result =
            taae::evolution::run_declared_reconstruction(
                config,
                problem
            );
        std::cout << taae::evolution::result_to_json(result) << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n';
        return 1;
    }
}
