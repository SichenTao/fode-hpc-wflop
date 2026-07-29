/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: deterministic FQFODE Q-table training-artifact generator
Paper title: A Reinforcement Learning-Assisted Fractional-Order Differential Evolution for Solving Wind Farm Layout Optimization Problems
DOI: 10.3390/math13182935
Paper provides: M=5 agents, R=4 local rounds, 101 state interactions, one element-wise aggregated Qinit copied into four formal stage tables, alpha=0.1, gamma=0.9, epsilon=0.2
Public author code URL: no direct FQFODE code or pretrained-table archive was found by the bounded 2026-07-29 search recorded in docs/source-dossiers/S04.json
Public author code revision or archive hash: unavailable; target paper sha256:0b388fb055956837876f5cfffd8320ab88c0bf4ef32d386c2143a0e7b498c9a0
Known missing information: author training seeds, exact pretraining environment/reward and physical-evaluation ledger, and shared-Qinit versus independent-stage-pretraining adjudication
Reconstruction performed here: five fixed seeds, four inherited rounds, 101 complete FODE-generation interactions per round on declared fixed case WS2tn50, one aggregated Qinit copied into four stage tables, exact physical-FES accounting, and immutable table hashing
Method evidence tier: M3_DECLARED_COMPLETION
Method semantic ID: fqfode_seeded_training_declared_reconstruction_v1
Controlling contract: shared/contracts/fqfode_seeded_training_reconstruction_contract.json
Claim boundary: generates a declared reconstruction artifact; it does not recover or impersonate the unavailable author-pretrained policy
END WFLOP IMPLEMENTATION FACT DECLARATION
*/

#include "wflop/rlfode_reconstruction.hpp"

#include "fode/case.hpp"

#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

struct Arguments {
    std::string cases = "shared/contracts/benchmark_cases.json";
    std::string case_id =
        wflop::rlfode_reconstruction::kSharedTrainingCaseId;
    std::string output =
        std::string("shared/models/fqfode_seeded/")
        + wflop::rlfode_reconstruction::kSharedArtifactFilename;
    int workers = 20;
};

Arguments parse_arguments(int argc, char** argv) {
    Arguments result;
    for (int index = 1; index < argc; ++index) {
        const std::string flag = argv[index];
        auto next = [&]() {
            if (index + 1 >= argc) {
                throw std::runtime_error("missing value after " + flag);
            }
            return std::string(argv[++index]);
        };
        if (flag == "--cases") {
            result.cases = next();
        } else if (flag == "--case") {
            result.case_id = next();
        } else if (flag == "--output") {
            result.output = next();
        } else if (flag == "--workers") {
            result.workers = std::stoi(next());
        } else if (flag == "--help" || flag == "-h") {
            std::cout
                << "Usage: rlfode_train_qtable [options]\n"
                << "  --cases PATH    benchmark case contract\n"
                << "  --case ID       one training environment\n"
                << "  --output PATH   frozen Q-table artifact\n"
                << "  --workers N     persistent CPU thread-team size\n";
            std::exit(0);
        } else {
            throw std::runtime_error("unknown argument: " + flag);
        }
    }
    return result;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const Arguments arguments = parse_arguments(argc, argv);
        const auto data = fode::load_case(
            arguments.cases,
            arguments.case_id
        );
        const auto artifact =
            wflop::rlfode_reconstruction::train_artifact(
                data,
                arguments.workers
            );
        wflop::rlfode_reconstruction::save_artifact(
            artifact,
            arguments.output
        );
        std::cout
            << "{\"training_case_id\":\"" << artifact.training_case_id
            << "\",\"training_physical_fes\":"
            << artifact.training_physical_fes
            << ",\"seed_manifest_id\":\""
            << artifact.seed_manifest_id
            << "\",\"qtable_hash\":\""
            << artifact.table_hash
            << "\"}\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << "\n";
        return 1;
    }
}
