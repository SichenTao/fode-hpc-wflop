/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: TAAE declared-reconstruction Transformer mathematical-kernel tests
Paper title: Transformer Autoencoder-Assisted Evolutionary Framework for Constrained Multiobjective 3D Wind Farm Layout Optimization
DOI: 10.1109/JAS.2026.126233
Public author method source/checkpoint: unavailable as recorded in docs/source-dossiers/Y36.json
Missing choices completed here: FFN width 256, post-norm, zero dropout, Xavier-uniform initialization, mean encoder pooling, separate encoder/decoder embeddings, deterministic metric-pair seed, per-parameter Adam age, and checkpoint format
Reconstruction status: engineering reconstruction with declared completion choices
Method evidence tier: M3_DECLARED_COMPLETION
Method semantic ID: taae_transformer_declared_reconstruction_v1
Controlling contract: shared/contracts/taae_transformer_declared_reconstruction_contract.json
Claim boundary: mathematical-kernel verification only; the distinct evolution reconstruction is governed separately, original taae remains blocked, and no author-result, optimizer, performance, or GPU claim is made
END WFLOP IMPLEMENTATION FACT DECLARATION
*/

#include "taae/model.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <string>
#include <vector>

namespace taae::fode_compat {
class PersistentExecutor {};
}

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

taae::ModelConfig tiny_config() {
    taae::ModelConfig config;
    config.vocabulary = 7;
    config.sequence_length = 4;
    config.model_dimension = 8;
    config.latent_dimension = 8;
    config.heads = 4;
    config.encoder_layers = 1;
    config.decoder_layers = 1;
    config.ffn_width = 16;
    config.regression_hidden_width = 8;
    return config;
}

void test_paper_profile_and_layout_corpus() {
    const taae::TrainingProfile profile =
        taae::paper_scale_training_profile();
    require(
        profile.corpus_layouts == 100000 &&
        profile.train_layouts == 100000 &&
        profile.pretraining_epochs == 500 &&
        profile.pretraining_batch_size == 64 &&
        profile.learning_rate == 1.0e-3,
        "paper-scale pretraining profile mismatch"
    );
    const taae::ModelConfig config = tiny_config();
    const auto first =
        taae::deterministic_layout_corpus(20, config, 8844);
    const auto second =
        taae::deterministic_layout_corpus(20, config, 8844);
    require(first == second, "layout corpus deterministic replay failed");
    for (const auto& layout : first) {
        require(
            layout.size() ==
                static_cast<std::size_t>(config.sequence_length),
            "layout corpus length mismatch"
        );
        require(
            std::is_sorted(layout.begin(), layout.end()),
            "layout corpus is not canonical sorted"
        );
        require(
            std::adjacent_find(layout.begin(), layout.end()) ==
                layout.end(),
            "layout corpus contains duplicate grid cells"
        );
    }
    std::cout
        << "paper_profile_and_corpus_pass layouts=100000 epochs=500 "
        << "batch=64 unique_sorted_layouts=pass "
        << "problem_monitor_binding=deferred_to_end_to_end_M3\n";
}

void test_assembled_total_loss_gradients() {
    const taae::ModelConfig config = tiny_config();
    const auto corpus =
        taae::deterministic_layout_corpus(4, config, 777);
    const std::vector<double> fitness = {0.15, 0.65, -0.25, 0.35};
    taae::TransformerAutoencoder model(config, 9090);
    taae::LossWeights weights;
    static_cast<void>(
        model.gradient_only(corpus, fitness, weights, false)
    );
    const std::vector<std::string> names = {
        "embedding.token",
        "encoder.layer.0.self.attention.query_weight",
        "encoder.latent_weight",
        "decoder.memory_weight",
        "decoder.embedding.token",
        "decoder.layer.0.cross.attention.key_weight",
        "decoder.layer.0.ffn.first_weight",
        "decoder.output_weight",
        "regression.hidden_weight",
        "regression.hidden_bias",
        "regression.output_weight",
        "regression.output_bias",
    };
    constexpr double step = 1.0e-5;
    constexpr double absolute_tolerance = 8.0e-5;
    constexpr double relative_tolerance = 8.0e-4;
    double maximum_error = 0.0;
    for (const std::string& name : names) {
        std::size_t selected = 0;
        double analytic = 0.0;
        for (std::size_t index = 0;
             index < model.parameter_size(name);
             ++index) {
            const double candidate =
                model.parameter_gradient(name, index);
            if (std::abs(candidate) > std::abs(analytic)) {
                selected = index;
                analytic = candidate;
            }
        }
        require(
            std::abs(analytic) > 1.0e-12,
            "assembled gradient is zero for " + name
        );
        const double original =
            model.parameter_value(name, selected);
        model.set_parameter_value(name, selected, original + step);
        const double plus =
            model.gradient_only(corpus, fitness, weights, false).total;
        model.set_parameter_value(name, selected, original - step);
        const double minus =
            model.gradient_only(corpus, fitness, weights, false).total;
        model.set_parameter_value(name, selected, original);
        const double numerical = (plus - minus) / (2.0 * step);
        const double error = std::abs(analytic - numerical);
        maximum_error = std::max(maximum_error, error);
        const double scale =
            std::max({1.0, std::abs(analytic), std::abs(numerical)});
        require(
            error <= absolute_tolerance ||
                error / scale <= relative_tolerance,
            "assembled finite difference failed for " + name
        );
        static_cast<void>(
            model.gradient_only(corpus, fitness, weights, false)
        );
    }
    std::cout
        << "assembled_total_loss_gradient_pass components="
        << names.size() << " max_abs_error=" << maximum_error
        << " abs_tol=" << absolute_tolerance
        << " rel_tol=" << relative_tolerance << '\n';
}

void test_raw_latent_public_boundary() {
    const taae::ModelConfig config = tiny_config();
    const auto corpus =
        taae::deterministic_layout_corpus(1, config, 2468);
    taae::TransformerAutoencoder model(config, 13579);
    const std::vector<double> raw_latent = model.encode(corpus.front());
    double squared_norm = 0.0;
    for (double value : raw_latent) {
        squared_norm += value * value;
    }
    const double norm = std::sqrt(squared_norm);
    require(
        std::abs(norm - 1.0) > 1.0e-3,
        "public encode output was forcibly L2-normalized"
    );
    require(
        model.decode_argmax(raw_latent).size() ==
            static_cast<std::size_t>(config.sequence_length),
        "decoder rejected public raw latent"
    );
    std::cout
        << "raw_latent_boundary_pass encode_norm=" << norm
        << " decoder_input=raw_h regression_metric_input=normalized_hbar\n";
}

void test_default_architecture() {
    taae::TransformerAutoencoder model(taae::ModelConfig{}, 1001);
    require(model.shape_fixture(), "default 6+6 architecture smoke failed");
    require(model.causal_mask_fixture(), "public causal fixture failed");
    std::cout
        << "default_architecture_pass encoder_layers=6 decoder_layers=6 "
        << "heads=4 d_model=64 ffn=256 latent=64 bos=present "
        << "causal=pass post_norm=pass dropout=0\n";
}

void test_training_freeze_and_checkpoint() {
    const taae::ModelConfig config = tiny_config();
    const auto corpus =
        taae::deterministic_layout_corpus(4, config, 991);
    const std::vector<double> fitness = {0.1, 0.7, -0.2, 0.4};
    taae::TransformerAutoencoder model(config, 12345);

    taae::LossWeights combined;
    require(
        combined.regression == 30.0 &&
        combined.metric_smoothness == 1.0,
        "combined fine-tune defaults mismatch"
    );
    static_cast<void>(
        model.gradient_only(corpus, fitness, combined, false)
    );
    const std::vector<std::string> trainable_representatives = {
        "encoder.layer.0.self.attention.query_weight",
        "encoder.layer.0.ffn.first_weight",
        "encoder.layer.0.ffn.norm_scale",
        "encoder.latent_weight",
        "decoder.memory_weight",
        "decoder.layer.0.self.attention.query_weight",
        "decoder.layer.0.cross.attention.key_weight",
        "decoder.layer.0.ffn.first_weight",
        "decoder.output_weight",
        "regression.hidden_weight",
        "regression.output_weight",
    };
    for (const std::string& name : trainable_representatives) {
        double gradient_norm = 0.0;
        for (std::size_t index = 0;
             index < model.parameter_size(name);
             ++index) {
            gradient_norm +=
                std::abs(model.parameter_gradient(name, index));
        }
        require(
            gradient_norm > 1.0e-12,
            "missing analytic gradient for " + name
        );
    }

    static_cast<void>(
        model.gradient_only(corpus, fitness, combined, true)
    );
    bool encoder_gradient_seen = false;
    for (const std::string& name : model.parameter_names()) {
        double norm = 0.0;
        for (std::size_t index = 0;
             index < model.parameter_size(name);
             ++index) {
            norm += std::abs(model.parameter_gradient(name, index));
        }
        if (name.rfind("decoder.", 0) == 0) {
            require(norm == 0.0, "decoder freeze gradient leak: " + name);
        } else if (name.rfind("encoder.", 0) == 0) {
            encoder_gradient_seen = encoder_gradient_seen || norm > 0.0;
        }
    }
    require(encoder_gradient_seen, "freeze also removed encoder gradients");
    require(
        model.parameter_update_step("decoder.output_weight") == 0,
        "gradient-only call advanced decoder Adam age"
    );

    const std::string frozen_checkpoint =
        "taae_kernel_frozen_checkpoint_test.bin";
    static_cast<void>(model.train_batch(
        corpus,
        fitness,
        combined,
        1.0e-3,
        0.9,
        0.999,
        1.0e-8,
        true
    ));
    require(
        model.parameter_update_step("decoder.output_weight") == 0 &&
        model.parameter_update_step("decoder.memory_weight") == 0,
        "frozen decoder Adam age advanced"
    );
    taae::TrainingWork frozen_work;
    static_cast<void>(model.save_checkpoint(
        frozen_checkpoint,
        "freeze_unfreeze_test",
        12345,
        frozen_work
    ));
    taae::CheckpointMetadata frozen_metadata_a;
    taae::CheckpointMetadata frozen_metadata_b;
    taae::TransformerAutoencoder unfreeze_a =
        taae::TransformerAutoencoder::load_checkpoint(
            frozen_checkpoint,
            frozen_metadata_a
        );
    taae::TransformerAutoencoder unfreeze_b =
        taae::TransformerAutoencoder::load_checkpoint(
            frozen_checkpoint,
            frozen_metadata_b
        );
    std::remove(frozen_checkpoint.c_str());
    static_cast<void>(unfreeze_a.train_batch(
        corpus,
        fitness,
        combined,
        1.0e-3,
        0.9,
        0.999,
        1.0e-8,
        false
    ));
    static_cast<void>(unfreeze_b.train_batch(
        corpus,
        fitness,
        combined,
        1.0e-3,
        0.9,
        0.999,
        1.0e-8,
        false
    ));
    require(
        unfreeze_a.parameter_update_step("decoder.output_weight") == 1 &&
        unfreeze_a.parameter_hash() == unfreeze_b.parameter_hash(),
        "freeze-unfreeze Adam replay mismatch"
    );

    taae::LossWeights reconstruction_only;
    reconstruction_only.reconstruction = 1.0;
    reconstruction_only.regression = 0.0;
    reconstruction_only.metric_smoothness = 0.0;
    const double initial_loss = model.reconstruction_loss(corpus);
    const std::string initial_hash = model.parameter_hash();
    for (int step = 0; step < 60; ++step) {
        static_cast<void>(model.train_batch(
            corpus,
            fitness,
            reconstruction_only,
            3.0e-3,
            0.9,
            0.999,
            1.0e-8,
            false
        ));
    }
    const double final_loss = model.reconstruction_loss(corpus);
    require(
        final_loss < initial_loss * 0.8,
        "tiny deterministic corpus reconstruction loss did not decrease"
    );
    require(
        model.parameter_hash() != initial_hash,
        "Adam did not update parameter state"
    );

    taae::TrainingProfile profile =
        taae::bounded_smoke_training_profile();
    profile.pretraining_epochs = 2;
    profile.corpus_layouts = 5;
    profile.train_layouts = 5;
    profile.pretraining_batch_size = 2;
    taae::TransformerAutoencoder ledger_model(config, 77);
    taae::fode_compat::PersistentExecutor executor;
    const taae::TrainingWork ledger =
        taae::pretrain(ledger_model, profile, executor);
    require(
        ledger.training_physical_fes == 0,
        "training ledger counted physical FES"
    );
    require(
        ledger.corpus_samples == 5 &&
        ledger.optimizer_steps == 6 &&
        ledger.pretraining_epochs == 2 &&
        ledger.token_operations == 40,
        "training ledger counts mismatch"
    );

    const std::string checkpoint_path =
        "taae_kernel_checkpoint_test.bin";
    const std::vector<double> latent_before = model.encode(corpus.front());
    const double replay_loss_before = model.reconstruction_loss(corpus);
    taae::CheckpointMetadata written = model.save_checkpoint(
        checkpoint_path,
        "tiny_deterministic_test",
        12345,
        ledger
    );
    taae::CheckpointMetadata loaded_metadata;
    taae::TransformerAutoencoder loaded =
        taae::TransformerAutoencoder::load_checkpoint(
            checkpoint_path,
            loaded_metadata
        );
    std::remove(checkpoint_path.c_str());
    require(
        loaded.parameter_hash() == model.parameter_hash() &&
        loaded.parameter_hash() == written.parameter_fnv1a64 &&
        written.file_sha256 == loaded_metadata.file_sha256,
        "checkpoint hash replay mismatch"
    );
    require(
        loaded.encode(corpus.front()) == latent_before &&
        loaded.reconstruction_loss(corpus) == replay_loss_before,
        "checkpoint output replay mismatch"
    );
    require(
        loaded_metadata.method_semantic_id ==
            "taae_transformer_declared_reconstruction_v1" &&
        loaded_metadata.work.training_physical_fes == 0,
        "checkpoint semantic metadata mismatch"
    );
    std::cout
        << "training_pass initial_reconstruction_loss=" << initial_loss
        << " final_reconstruction_loss=" << final_loss
        << " analytic_components=11 adam=pass decoder_freeze=pass "
        << "freeze_unfreeze_age_replay=pass minibatch_ledger=pass "
        << "checkpoint_hash_replay=pass training_physical_fes=0\n";
}

}  // namespace

int main() {
    try {
        std::string report;
        require(
            taae::run_model_gradient_checks(report),
            report
        );
        std::cout << report << '\n';
        test_paper_profile_and_layout_corpus();
        test_raw_latent_public_boundary();
        test_assembled_total_loss_gradients();
        test_default_architecture();
        test_training_freeze_and_checkpoint();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
