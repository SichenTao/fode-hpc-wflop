/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T27 C++/LibTorch evaluator, GAT, EDM and lifecycle smoke
Paper DOI: 10.1145/3711896.3737181
Public source: https://github.com/dbsxodud-11/layopt at 19ff389.
Missing facts and reconstruction decisions: include/core99/shin_t27.hpp.
Semantic IDs: shin2025_conditional_edm_gat_paper_profile_v1 and
shin2025_floris411_gch_rectangular_v1.
Contract: shared/contracts/core99_t27_shin_diffusion_2025.json.
Claim boundary: bounded reconstruction validation, not author numerical replay.
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "core99/shin_t27.hpp"

#include <ATen/Parallel.h>

#include <cmath>
#include <iostream>
#include <stdexcept>

int main() {
    try {
        at::set_num_threads(2);
        core99::t27::Floris411Gch evaluator(2);
        const core99::t27::Layout single{{0.5},{0.5}};
        const auto evaluation = evaluator.evaluate(
            single,
            3000.0,
            {8.0,60.0}
        );
        if (
            std::abs(evaluation.farm_power_w - 1753954.4591791716)
                > 0.03 * evaluation.farm_power_w
            || evaluation.turbine_power_w.size() != 1
        ) {
            throw std::runtime_error("T27 single-turbine FLORIS fixture");
        }

        torch::manual_seed(27);
        core99::t27::ModelConfig model_config;
        model_config.hidden_width = 16;
        model_config.layers = 3;
        model_config.time_width = 8;
        model_config.fourier_width = 4;
        auto model = core99::t27::ConditionalGatDenoiser(model_config);
        const auto layouts = torch::rand({2,4,2});
        const auto score = torch::ones({2,1});
        const auto wind = torch::tensor({{0.8,1.0/3.0},{0.8,1.0/3.0}});
        const auto denoised = model->forward(
            layouts,
            torch::ones({2}),
            score,
            wind
        );
        if (denoised.sizes() != layouts.sizes()) {
            throw std::runtime_error("T27 dense GAT shape");
        }
        core99::t27::ConditionalEdm edm(model);
        const auto loss = edm.training_loss(layouts, score, wind, 0.1);
        if (!std::isfinite(loss.item<double>())) {
            throw std::runtime_error("T27 EDM loss");
        }
        auto repaired = core99::t27::repair_spacing(
            torch::tensor(
                {{{0.5,0.5},{0.5,0.5},{0.1,0.1},{0.9,0.9}}},
                torch::kFloat32
            ),
            2000.0,
            252.0,
            8,
            0.01
        );
        if (
            repaired.min().item<double>() < 0.0
            || repaired.max().item<double>() > 1.0
        ) {
            throw std::runtime_error("T27 repair bounds");
        }
        core99::t27::ProtocolConfig protocol;
        protocol.turbine_count = 4;
        protocol.side_length_m = 2000.0;
        protocol.initial_layouts = 4;
        protocol.rounds = 1;
        protocol.generated_per_round = 2;
        protocol.training_steps_per_round = 1;
        protocol.batch_size = 2;
        protocol.repair_steps = 2;
        protocol.workers = 2;
        protocol.seed = 27;
        core99::t27::EdmConfig edm_config;
        edm_config.sample_steps = 2;
        const auto run = core99::t27::run(
            protocol,
            torch::Device(torch::kCPU),
            model_config,
            edm_config
        );
        if (
            run.completed_rounds != 1
            || run.optimizer_steps != 1
            || run.physical_layout_evaluations != 6
            || !(run.best_aep_mwh > 0.0)
        ) {
            throw std::runtime_error("T27 repaired protocol lifecycle");
        }
        std::cout << "t27_libtorch_test_pass best_aep_mwh="
                  << run.best_aep_mwh << "\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << "\n";
        return 1;
    }
}
