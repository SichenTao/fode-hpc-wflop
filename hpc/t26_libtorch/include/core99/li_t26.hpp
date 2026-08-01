/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T26 PIDNN plus GTDE paper-problem C++/LibTorch package
Paper title/DOI: Li et al., A Data-Physics Hybrid-Driven Layout Optimization
Framework for Large-Scale Wind Farms; 10.1016/j.apenergy.2025.125908.
Primary paper: local 11-page PDF SHA-256
96c3e1f7a737090d6428a68e56dc8fcfdf2e4d7169acec25e8c0657e97f8c36f.
Related public source: the target paper publishes no URL and says data are
available on request. The cited GTDE author resource page
https://zhanapollo.github.io/zhanzhh/resources.htm states that source for
10.1109/TEVC.2022.3185665 must be requested by email; no publicly
downloadable target implementation was found in the 2026-08-01 audit.
Public source provenance: the author resource page is an availability oracle
only and no target or GTDE executable source is redistributed.
Paper-provided facts: Eqs. 1-21; a dual MLP with velocity and Ct encoders;
velocity hidden widths 32,16,8 with ELU; Adam, batch 1024, learning rate
0.005, 10000 iterations and regularization 5; a 30D by 5D by 4D single-NREL-
5MW CFD domain; 23 speeds from 3-25 m/s and 120000 samples per speed; a 7 km
square, 80 turbines, 3D spacing, 12-direction Horns-Rev resource; and GTDE
population 300, 1000 generations, Pn~N(0.01,0.01), F~N(0.5,0.1), Pm=0.01.
Missing information: ANSYS Fluent mesh, fields, boundary values, pressure,
sample coordinates, Ct observations, code/model; numeric wind-rose and power
curve arrays; encoder-2 widths; objective penalty coefficient; GTDE crossover
rate, bounds handling, complete initialization, exact mutation/crossover
implementation, repeats, seeds, layouts and convergence histories.
Paper/source conflict: Eq. 12 contains x/y coordinates for 80 turbines (160
scalars), while Section 6.3 calls the dimension 80. This package preserves 80
two-component turbine genes, represented computationally by 160 scalars.
Reconstruction: deterministic actuator-disk/Gaussian single-wake fields stand
in for unavailable RANS data; the paper residuals train the dual network with
zero pressure-gradient closure because no pressure network/data are defined.
Figure 6 is digitized into 12 directions by six speed bands, Figure 5 into an
NREL-5MW curve, and the regular 8 by 10 layout is exact from Figure 7. The
PIDNN wake table is calibrated only to the reported regular-layout 1554.20
GWh AEP. Missing GTDE choices use binomial CR=0.9, reflection at the square,
pairwise summed 3D violation, and a disclosed feasibility-dominant penalty.
Semantic IDs: problem t26_pidnn_hornsrev80_declared_v1; training
t26_pidnn_rans_proxy_physics_dual_libtorch_v1; method
t26_gtde_80_vector_genes_declared_v1; protocol
t26_native_single_plus_25_seed_robustness_v1.
Production backend: direct C++/LibTorch CPU or CUDA training and tensor
inference. The trained PIDNN is distilled on a fixed downstream/crosswind
table; GTDE evaluates complete populations in bounded tensor chunks. CPU
uses all selected LibTorch intra-op threads and CUDA uses batched kernels.
No Python participates in training, inference, optimization, or timing.
Controlling contract: shared/contracts/core99_t26_li_pidnn_gtde_2025.json.
Claim boundary: flexible equation-level academic reproduction of the target
algorithm and paper problem; not author CFD data, PIDNN artifact, GTDE source,
random trajectory, layout or per-number replay.
Last evidence-audit date: 2026-08-01
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#pragma once

#include <torch/torch.h>

#include <cstdint>
#include <string>
#include <vector>

namespace core99::t26 {

struct PidnnImpl : torch::nn::Module {
    PidnnImpl();
    torch::Tensor velocity(const torch::Tensor& input);
    torch::Tensor ct(const torch::Tensor& inflow);
    torch::nn::Sequential velocity_encoder{nullptr};
    torch::nn::Sequential ct_encoder{nullptr};
};
TORCH_MODULE(Pidnn);

struct TrainingConfig {
    std::string backend = "auto";
    std::string artifact;
    int iterations = 10000;
    int batch_size = 1024;
    int workers = 20;
    std::uint64_t seed = 26001;
    bool smoke = false;
};

struct TrainingResult {
    double data_loss = 0.0;
    double physics_loss = 0.0;
    double total_loss = 0.0;
    double table_direct_mae = 0.0;
    double seconds = 0.0;
    int iterations = 0;
    int samples_per_iteration = 0;
    std::string backend;
    std::string artifact;
};

struct OptimizationConfig {
    std::string backend = "auto";
    std::string artifact;
    int generations = 1000;
    int population = 300;
    int workers = 20;
    std::uint64_t seed = 26001;
    int evaluation_limit = 0;
    bool smoke = false;
};

struct OptimizationResult {
    double initial_aep_gwh = 0.0;
    double final_aep_gwh = 0.0;
    double final_fitness = 0.0;
    double minimum_spacing_m = 0.0;
    double maximum_spacing_violation_m = 0.0;
    double evaluator_seconds = 0.0;
    double algorithm_seconds = 0.0;
    double seconds = 0.0;
    std::uint64_t physical_fes = 0;
    int generations = 0;
    int population = 0;
    std::string backend;
    std::vector<double> layout_xy_m;
};

torch::Device resolve_device(const std::string& backend);
torch::Tensor regular_layout(torch::Device device);
TrainingResult train_pidnn(const TrainingConfig& config);
OptimizationResult run_gtde(const OptimizationConfig& config);
std::string training_json(const TrainingResult& result,
                          const TrainingConfig& config);
std::string optimization_json(const OptimizationResult& result,
                              const OptimizationConfig& config);

}  // namespace core99::t26
