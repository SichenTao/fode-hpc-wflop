/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T28 native C++/LibTorch CPU/CUDA scientific contract
Paper DOI: 10.5194/wes-10-1661-2025
Public source: https://github.com/Twitwi96/WINDFLOWER/tree/v1.0.0
Public data DOI: 10.5281/zenodo.13946931
Source revision: 427ee84dfdb9fb275b229e8eecf4503071d8fed4
License of author source/data: AGPL-3.0.
Missing facts: the paper does not publish the five random seeds or complete
optimized-layout recorders. The source does not seed NumPy and does not retain
a reproducible random-start corpus.
Paper/source conflicts: the prose reports 216 MW, while the turbine curve,
source calculation, and later paper tables use 72 * 3.075 = 221.4 MW.
The paper includes 3% wind-power modelling error, while the public source
comments it out because that expression breaks its autograd path.
Resolution: preserve all published equations and source parameters, use
declared seeds 0..4, follow the source-consistent 221.4 MW capacity, and retain
the paper's 3% error in the LibTorch-differentiable path.
HPC resolution: fuse all scenario, forecast, wake-pair and bidding-candidate
dimensions into LibTorch tensor kernels; CPU uses all requested intra-op
threads, CUDA uses batched device kernels and autograd. No Python participates
in production optimization or timing.
Wake boundary: the production evaluator reconstructs differentiable
Bastankhah/Niayifar Gaussian deficit with linear superposition and the released
V112 power/Ct curve. Because the public source delegates Crespo-Hernandez
added turbulence to PyWake but does not expose that kernel, its aggregate
interaction factor is calibrated once against the paper-published 2023 base
layout AEP (919.78 GWh) and then frozen; the resulting independent error is
below 1%. The author PyWake/TopFarm stack is used only for independent H5
reference receipts when available.
Claim boundary: paper-first academic reconstruction using exact released data,
not author software and not a claim of per-number reproduction.
Semantic IDs: nguyen2025_jerm_northwind_v1,
nguyen2025_batched_sgd_libtorch_v1.
Contract: shared/contracts/core99_t28_nguyen_jerm_2025.json.
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#pragma once

#include <torch/torch.h>

#include <cstdint>
#include <string>
#include <vector>

namespace core99::t28 {

enum class Objective { Aep, Daem, Jerm };

struct Configuration {
    std::string data_directory;
    std::string backend = "auto";
    Objective objective = Objective::Jerm;
    int year = 2023;
    int evaluation_year = 0;
    int samples_per_iteration = 20;
    int forecasts = 10;
    int iterations = 2000;
    int workers = 20;
    int seed = 0;
    int evaluation_limit = 0;
    double reserve_limit_mw = 50.0;
    double learning_rate_m = 112.0;
};

struct ScenarioData {
    torch::Tensor values;
    torch::Tensor layout_xy;
    torch::Tensor boundary_xy;
    torch::Tensor curve;
};

struct Result {
    double objective_value = 0.0;
    double aep_gwh = 0.0;
    double minimum_spacing_m = 0.0;
    double seconds = 0.0;
    std::vector<double> x;
    std::vector<double> y;
};

torch::Device resolve_device(const std::string& backend);
ScenarioData load_data(const std::string& directory, int year, torch::Device device);
torch::Tensor farm_power_mw(
    const torch::Tensor& layout_xy,
    const torch::Tensor& wind_speed,
    const torch::Tensor& wind_direction,
    const torch::Tensor& curve
);
torch::Tensor expected_objective(
    const torch::Tensor& layout_xy,
    const torch::Tensor& scenarios,
    const torch::Tensor& curve,
    Objective objective,
    int forecasts,
    double reserve_limit_mw,
    std::uint64_t seed
);
Result run(const Configuration& configuration);
std::string to_json(const Result& result, const Configuration& configuration);

}  // namespace core99::t28
