/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: TAAE deterministic paper-scale pretraining CLI
Paper title: Transformer Autoencoder-Assisted Evolutionary Framework for Constrained Multiobjective 3D Wind Farm Layout Optimization
DOI: 10.1109/JAS.2026.126233
Paper equations/tables/pseudocode used: paper model dimensions, 100000-layout
corpus, 500 pretraining epochs, batch size 64, and Adam schedule.
Public author code/data/model URL: unavailable; see docs/source-dossiers/Y36.json.
What public assets provide: the published equations and paper-visible training
counts only.
What remains missing or conflicts: author corpus, checkpoint, seed, optimizer
details, and training code.
Reconstruction performed here: it executes the deterministic
M3 training contract frozen in
shared/contracts/taae_transformer_declared_reconstruction_contract.json and
writes a hash-verifiable checkpoint for the separate evolution executable.
Method evidence tier: M3_DECLARED_COMPLETION
Problem evidence tier: P3_DECLARED_PROXY
Method semantic ID: taae_transformer_declared_reconstruction_v1
Problem semantic ID: taae_zhangbei_structured_declared_proxy_v1
Controlling contracts: shared/contracts/taae_transformer_declared_reconstruction_contract.json and shared/contracts/taae_transformer_evolution_declared_reconstruction_contract.json
Claim boundary: deterministic paper-scale declared reconstruction, not an
author checkpoint or reproduction of author-reported numerical results.
Last evidence-audit date: 2026-07-30
END WFLOP IMPLEMENTATION FACT DECLARATION
*/

#include "fode/executor.hpp"
#include "taae/model.hpp"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

struct Arguments {
    std::string output;
    int workers = 0;
    std::uint64_t seed = 0x5441414550524554ULL;
    std::uint64_t corpus_layouts = 100000;
    int epochs = 500;
    int batch_size = 64;
};

Arguments parse(int argc, char** argv) {
    Arguments result;
    for (int index = 1; index < argc; ++index) {
        const std::string option = argv[index];
        if (option == "--help") {
            std::cout
                << "usage: taae_pretrain_hpc --output FILE "
                << "[--workers N] [--seed N] [--corpus-layouts N] "
                << "[--epochs N] [--batch-size N]\n";
            std::exit(0);
        }
        if (index + 1 >= argc) {
            throw std::invalid_argument("missing value for " + option);
        }
        const std::string value = argv[++index];
        if (option == "--output") {
            result.output = value;
        } else if (option == "--workers") {
            result.workers = std::stoi(value);
        } else if (option == "--seed") {
            result.seed = std::stoull(value);
        } else if (option == "--corpus-layouts") {
            result.corpus_layouts = std::stoull(value);
        } else if (option == "--epochs") {
            result.epochs = std::stoi(value);
        } else if (option == "--batch-size") {
            result.batch_size = std::stoi(value);
        } else {
            throw std::invalid_argument("unknown option " + option);
        }
    }
    if (result.output.empty()) {
        throw std::invalid_argument("--output is required");
    }
    if (
        result.corpus_layouts == 0
        || result.epochs <= 0
        || result.batch_size <= 0
    ) {
        throw std::invalid_argument("training dimensions must be positive");
    }
    return result;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const Arguments arguments = parse(argc, argv);
        taae::TrainingProfile profile =
            taae::paper_scale_training_profile();
        profile.corpus_layouts = arguments.corpus_layouts;
        profile.train_layouts = arguments.corpus_layouts;
        profile.test_layouts = 0;
        profile.pretraining_epochs = arguments.epochs;
        profile.pretraining_batch_size = arguments.batch_size;
        profile.id = (
            arguments.corpus_layouts == 100000
            && arguments.epochs >= 500
            && arguments.batch_size == 64
        )
            ? "paper_scale_declared_reconstruction_v1"
            : "bounded_pretraining_probe_v1";

        fode::PersistentExecutor executor(arguments.workers);
        const auto started = std::chrono::steady_clock::now();
        taae::TransformerAutoencoder model(
            taae::ModelConfig{},
            arguments.seed
        );
        taae::TrainingWork work =
            taae::pretrain(model, profile, executor);
        const double measured_wall_seconds = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - started
        ).count();
        // Elapsed time belongs to the external run receipt, not the immutable
        // scientific artifact. Keeping it out of the checkpoint makes fresh
        // deterministic training byte-reproducible across repetitions.
        work.wall_seconds = 0.0;
        const taae::CheckpointMetadata metadata = model.save_checkpoint(
            arguments.output,
            profile.id,
            arguments.seed,
            work
        );
        std::cout
            << "{"
            << "\"profile_id\":\"" << profile.id << "\","
            << "\"corpus_samples\":" << work.corpus_samples << ","
            << "\"pretraining_epochs\":" << work.pretraining_epochs << ","
            << "\"optimizer_steps\":" << work.optimizer_steps << ","
            << "\"training_physical_fes\":"
            << work.training_physical_fes << ","
            << "\"requested_workers\":" << arguments.workers << ","
            << "\"resolved_workers\":" << executor.thread_count() << ","
            << "\"wall_seconds\":" << measured_wall_seconds << ","
            << "\"parameter_hash\":\""
            << metadata.parameter_fnv1a64 << "\","
            << "\"checkpoint_sha256\":\""
            << metadata.file_sha256 << "\""
            << "}\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n';
        return 1;
    }
}
