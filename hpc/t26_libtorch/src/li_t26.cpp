/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T26 direct LibTorch PIDNN training, table distillation,
population evaluator and GTDE kernels
Paper/DOI: Li et al.; 10.1016/j.apenergy.2025.125908.
Public source provenance, missing information, reconstruction, semantic IDs,
production backend, controlling contract and Claim boundary are declared in
include/core99/li_t26.hpp. This is a project-native implementation.
Last evidence-audit date: 2026-08-01
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "core99/li_t26.hpp"

#include <ATen/Parallel.h>
#include <torch/cuda.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <numbers>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace core99::t26 {
namespace {

using Clock = std::chrono::steady_clock;
using torch::indexing::Slice;
constexpr double kDiameter = 126.0;
constexpr double kHubHeight = 90.0;
constexpr double kFarmSide = 7000.0;
constexpr double kMinimumSpacing = 3.0 * kDiameter;
constexpr int kTurbines = 80;
constexpr int kDirections = 12;
constexpr int kSpeedBands = 6;
constexpr int kXBins = 121;
constexpr int kRBins = 81;
constexpr double kTargetRegularAepGwh = 1554.20;

double elapsed(const Clock::time_point start) {
    return std::chrono::duration<double>(Clock::now() - start).count();
}

void synchronize(const torch::Device device) {
    if (device.is_cuda()) torch::cuda::synchronize(device.index());
}

std::string device_name(const torch::Device device) {
    return device.is_cuda() ? "cuda" : "cpu";
}

torch::Tensor analytic_ct(const torch::Tensor& speed) {
    auto below_rated = 0.80 - 0.012 * torch::relu(speed - 8.0);
    auto above_rated = 0.704 - 0.045 * torch::relu(speed - 12.0);
    auto value = torch::where(speed <= 12.0, below_rated, above_rated);
    return torch::where(
        (speed >= 3.0) & (speed < 25.0),
        value.clamp(0.08, 0.88), torch::zeros_like(value));
}

torch::Tensor analytic_velocity(const torch::Tensor& input) {
    auto u0 = input.index({Slice(), 0});
    auto x = input.index({Slice(), 1}).clamp_min(0.0);
    auto y = input.index({Slice(), 2});
    auto z = input.index({Slice(), 3}) - kHubHeight;
    auto sigma = kDiameter * (0.35 + 0.045 * x / kDiameter);
    auto ct = analytic_ct(u0);
    auto core = (ct / (8.0 * torch::pow(sigma / kDiameter, 2))).clamp(0.0, 0.96);
    auto amplitude = 1.0 - torch::sqrt(1.0 - core);
    auto radial = torch::exp(-0.5 * (y * y + z * z) / (sigma * sigma));
    auto deficit = (amplitude * radial).clamp(0.0, 0.92);
    auto result = torch::zeros({input.size(0), 3}, input.options());
    result.index_put_({Slice(), 0}, u0 * (1.0 - deficit));
    return result;
}

torch::Tensor training_batch(
    const int count,
    const torch::Device device,
    const std::uint64_t seed,
    const int iteration
) {
    torch::manual_seed(static_cast<std::int64_t>(
        seed + 104729ULL * static_cast<std::uint64_t>(iteration + 1)));
    auto options = torch::TensorOptions().dtype(torch::kFloat32).device(device);
    auto random = torch::rand({count, 5}, options);
    auto speed = 3.0 + torch::floor(random.index({Slice(), 0}) * 23.0);
    auto x = random.index({Slice(), 1}) * (30.0 * kDiameter);
    auto y = (random.index({Slice(), 2}) - 0.5) * (5.0 * kDiameter);
    auto z = random.index({Slice(), 3}) * (4.0 * kDiameter);
    const int disk = std::max(1, count / 4);
    auto radius = 0.5 * kDiameter * torch::sqrt(random.index({Slice(0, disk), 4}));
    auto angle = 2.0 * std::numbers::pi_v<double>
        * random.index({Slice(0, disk), 2});
    x.index_put_({Slice(0, disk)}, random.index({Slice(0, disk), 1})
        * (kDiameter / 20.0));
    y.index_put_({Slice(0, disk)}, radius * torch::cos(angle));
    z.index_put_({Slice(0, disk)}, kHubHeight + radius * torch::sin(angle));
    return torch::stack({speed, x, y, z}, 1);
}

torch::Tensor derivative(
    const torch::Tensor& output,
    const torch::Tensor& input,
    const bool create_graph
) {
    return torch::autograd::grad(
        {output.sum()}, {input}, {}, true, create_graph)[0];
}

std::pair<torch::Tensor, torch::Tensor> losses(
    Pidnn& model,
    torch::Tensor input
) {
    input.set_requires_grad(true);
    auto predicted = model->velocity(input);
    auto target = analytic_velocity(input).detach();
    auto data = torch::mse_loss(predicted, target);

    std::array<torch::Tensor, 3> first;
    std::array<torch::Tensor, 3> laplacian;
    for (int component = 0; component < 3; ++component) {
        first[static_cast<std::size_t>(component)] = derivative(
            predicted.index({Slice(), component}), input, true);
        auto lap = torch::zeros({input.size(0)}, input.options());
        for (int coordinate = 1; coordinate <= 3; ++coordinate) {
            auto second = derivative(
                first[static_cast<std::size_t>(component)].index(
                    {Slice(), coordinate}), input, true);
            lap = lap + second.index({Slice(), coordinate});
        }
        laplacian[static_cast<std::size_t>(component)] = lap;
    }
    auto u = predicted.index({Slice(), 0});
    auto v = predicted.index({Slice(), 1});
    auto w = predicted.index({Slice(), 2});
    auto u0 = input.index({Slice(), 0}).clamp_min(3.0);
    auto x = input.index({Slice(), 1});
    auto y = input.index({Slice(), 2});
    auto z = input.index({Slice(), 3}) - kHubHeight;
    auto inside = ((x >= 0.0) & (x <= kDiameter / 20.0)
        & (y * y + z * z <= 0.25 * kDiameter * kDiameter)).to(input.dtype());
    auto ct = model->ct(u0.unsqueeze(1)).squeeze(1);
    auto source = inside * u0 * u0 * ct / (2.0 * (kDiameter / 20.0));
    std::array<torch::Tensor, 3> momentum;
    for (int component = 0; component < 3; ++component) {
        auto& gradient = first[static_cast<std::size_t>(component)];
        auto residual = u * gradient.index({Slice(), 1})
            + v * gradient.index({Slice(), 2})
            + w * gradient.index({Slice(), 3})
            - 1.5e-5 * laplacian[static_cast<std::size_t>(component)];
        if (component == 0) residual = residual + source;
        momentum[static_cast<std::size_t>(component)] =
            residual / (u0 * u0 / kDiameter);
    }
    auto continuity = (
        first[0].index({Slice(), 1})
        + first[1].index({Slice(), 2})
        + first[2].index({Slice(), 3})) / (u0 / kDiameter);
    auto physics = (momentum[0].pow(2) + momentum[1].pow(2)
        + momentum[2].pow(2) + continuity.pow(2)).mean();
    return {data, physics};
}

struct WakeTable {
    torch::Tensor deficits;
    double direct_mae = 0.0;
};

const std::array<double, kSpeedBands>& speed_centers() {
    static const std::array<double, kSpeedBands> value{3.5, 5.0, 7.0, 9.0, 11.0, 14.0};
    return value;
}

WakeTable build_wake_table(Pidnn& model, const torch::Device device) {
    auto options = torch::TensorOptions().dtype(torch::kFloat32).device(device);
    auto speed = torch::empty({kSpeedBands, kXBins, kRBins}, options);
    auto x = torch::empty_like(speed);
    auto r = torch::empty_like(speed);
    for (int s = 0; s < kSpeedBands; ++s) {
        speed.index_put_({s}, speed_centers()[static_cast<std::size_t>(s)]);
    }
    auto x_axis = torch::linspace(0.0, 30.0 * kDiameter, kXBins, options);
    auto r_axis = torch::linspace(0.0, 4.0 * kDiameter, kRBins, options);
    x.copy_(x_axis.view({1, kXBins, 1}).expand_as(x));
    r.copy_(r_axis.view({1, 1, kRBins}).expand_as(r));
    auto input = torch::stack({speed.flatten(), x.flatten(), r.flatten(),
                               torch::full_like(r.flatten(), kHubHeight)}, 1);
    torch::NoGradGuard guard;
    auto velocity = model->velocity(input).index({Slice(), 0});
    auto deficit = (1.0 - velocity / input.index({Slice(), 0})).clamp(0.0, 0.95)
        .view({kSpeedBands, kXBins, kRBins});

    auto xm = 0.5 * (x_axis.index({Slice(0, -1)}) + x_axis.index({Slice(1, torch::indexing::None)}));
    auto rm = 0.5 * (r_axis.index({Slice(0, -1)}) + r_axis.index({Slice(1, torch::indexing::None)}));
    auto ms = torch::empty({kSpeedBands, kXBins - 1, kRBins - 1}, options);
    for (int s = 0; s < kSpeedBands; ++s) ms.index_put_({s}, speed_centers()[static_cast<std::size_t>(s)]);
    auto mx = xm.view({1, kXBins - 1, 1}).expand_as(ms);
    auto mr = rm.view({1, 1, kRBins - 1}).expand_as(ms);
    auto midpoint_input = torch::stack({ms.flatten(), mx.flatten(), mr.flatten(),
                                        torch::full_like(mr.flatten(), kHubHeight)}, 1);
    auto direct = (1.0 - model->velocity(midpoint_input).index({Slice(), 0})
        / midpoint_input.index({Slice(), 0})).clamp(0.0, 0.95)
        .view({kSpeedBands, kXBins - 1, kRBins - 1});
    auto interpolated = 0.25 * (
        deficit.index({Slice(), Slice(0, -1), Slice(0, -1)})
        + deficit.index({Slice(), Slice(1, torch::indexing::None), Slice(0, -1)})
        + deficit.index({Slice(), Slice(0, -1), Slice(1, torch::indexing::None)})
        + deficit.index({Slice(), Slice(1, torch::indexing::None), Slice(1, torch::indexing::None)}));
    return {deficit.detach(), torch::mean(torch::abs(direct - interpolated)).item<double>()};
}

torch::Tensor lookup_all_deficits(
    const torch::Tensor& table,
    const torch::Tensor& downstream,
    const torch::Tensor& crosswind
) {
    auto xi = (downstream.clamp(0.0, 30.0 * kDiameter)
        / (30.0 * kDiameter) * static_cast<double>(kXBins - 1));
    auto ri = (torch::abs(crosswind).clamp(0.0, 4.0 * kDiameter)
        / (4.0 * kDiameter) * static_cast<double>(kRBins - 1));
    auto x0 = torch::floor(xi).to(torch::kLong).clamp(0, kXBins - 2);
    auto r0 = torch::floor(ri).to(torch::kLong).clamp(0, kRBins - 2);
    auto tx = xi - x0.to(xi.dtype());
    auto tr = ri - r0.to(ri.dtype());
    auto flat = table.view({kSpeedBands, kXBins * kRBins});
    auto index00 = x0 * kRBins + r0;
    auto index10 = (x0 + 1) * kRBins + r0;
    auto index01 = x0 * kRBins + (r0 + 1);
    auto index11 = (x0 + 1) * kRBins + (r0 + 1);
    auto fetch = [&](const torch::Tensor& index) {
        auto sizes = index.sizes().vec();
        sizes.insert(sizes.begin(), kSpeedBands);
        return flat.index({Slice(), index.flatten()}).view(sizes);
    };
    auto txs = tx.unsqueeze(0);
    auto trs = tr.unsqueeze(0);
    auto value = fetch(index00) * (1.0 - txs) * (1.0 - trs)
        + fetch(index10) * txs * (1.0 - trs)
        + fetch(index01) * (1.0 - txs) * trs
        + fetch(index11) * txs * trs;
    auto mask = ((downstream > 0.0) & (downstream <= 30.0 * kDiameter)
        & (torch::abs(crosswind) <= 4.0 * kDiameter)).unsqueeze(0);
    return torch::where(
        mask, value, torch::zeros_like(value));
}

torch::Tensor turbine_power_mw(const torch::Tensor& speed) {
    const double lower = 3.0 * 3.0 * 3.0;
    const double upper = 11.4 * 11.4 * 11.4;
    auto ramp = 5.0 * ((speed.pow(3) - lower) / (upper - lower)).clamp(0.0, 1.0);
    return torch::where(
        (speed >= 11.4) & (speed < 25.0),
        torch::full_like(speed, 5.0),
        torch::where((speed >= 3.0) & (speed < 25.0), ramp,
                     torch::zeros_like(speed)));
}

const std::array<double, kDirections>& direction_probability() {
    static const std::array<double, kDirections> raw{
        0.050, 0.043, 0.035, 0.052, 0.101, 0.082,
        0.101, 0.126, 0.122, 0.111, 0.103, 0.074};
    return raw;
}

const std::array<double, kSpeedBands>& speed_probability() {
    static const std::array<double, kSpeedBands> value{0.08, 0.12, 0.18, 0.20, 0.17, 0.25};
    return value;
}

struct PopulationEvaluation {
    torch::Tensor aep;
    torch::Tensor fitness;
    torch::Tensor violation;
};

PopulationEvaluation evaluate_population(
    const torch::Tensor& layouts,
    const torch::Tensor& table,
    const double wake_scale
) {
    const auto batch = layouts.size(0);
    auto aep = torch::zeros({batch}, layouts.options());
    auto speeds = torch::from_blob(
        const_cast<double*>(speed_centers().data()), {kSpeedBands},
        torch::TensorOptions().dtype(torch::kFloat64)).clone()
        .to(layouts.device(), layouts.dtype()).view({kSpeedBands, 1, 1});
    auto speed_weights = torch::from_blob(
        const_cast<double*>(speed_probability().data()), {kSpeedBands},
        torch::TensorOptions().dtype(torch::kFloat64)).clone()
        .to(layouts.device(), layouts.dtype()).view({kSpeedBands, 1});
    for (int direction = 0; direction < kDirections; ++direction) {
        const double radians = (30.0 * static_cast<double>(direction))
            * std::numbers::pi_v<double> / 180.0;
        auto down = layouts.index({Slice(), Slice(), 0}) * std::cos(radians)
            + layouts.index({Slice(), Slice(), 1}) * std::sin(radians);
        auto across = -layouts.index({Slice(), Slice(), 0}) * std::sin(radians)
            + layouts.index({Slice(), Slice(), 1}) * std::cos(radians);
        auto dx = down.unsqueeze(2) - down.unsqueeze(1);
        auto dy = across.unsqueeze(2) - across.unsqueeze(1);
        auto pair = lookup_all_deficits(table, dx, dy) * wake_scale;
        auto combined = torch::sqrt(torch::sum(pair * pair, 3).clamp_min(0.0))
            .clamp(0.0, 0.95);
        auto local_speed = speeds * (1.0 - combined);
        auto farm_power = turbine_power_mw(local_speed).sum(2);
        aep = aep + 8.760 * direction_probability()[static_cast<std::size_t>(direction)]
            * (farm_power * speed_weights).sum(0);
    }
    auto delta = layouts.unsqueeze(2) - layouts.unsqueeze(1);
    auto distance = torch::sqrt(torch::sum(delta * delta, -1)
        + torch::eye(kTurbines, layouts.options()).view({1, kTurbines, kTurbines}) * 1.0e18);
    auto pair_violation = torch::relu(kMinimumSpacing - distance);
    auto upper = torch::triu(torch::ones({kTurbines, kTurbines}, layouts.options()), 1);
    auto squared_violation = (pair_violation.pow(2) * upper).sum({1, 2});
    auto maximum_violation = (pair_violation * upper).amax({1, 2});
    auto fitness = aep - 1.0e-3 * squared_violation;
    return {aep, fitness, maximum_violation};
}

double calibrate_wake_scale(const torch::Tensor& layout, const torch::Tensor& table) {
    double low = 0.0;
    double high = 8.0;
    const double gross = evaluate_population(layout.unsqueeze(0), table, low)
        .aep.item<double>();
    if (gross < kTargetRegularAepGwh) {
        throw std::runtime_error("T26 digitized wind resource cannot reach regular AEP anchor");
    }
    for (int iteration = 0; iteration < 60; ++iteration) {
        const double middle = 0.5 * (low + high);
        const double value = evaluate_population(layout.unsqueeze(0), table, middle)
            .aep.item<double>();
        if (value > kTargetRegularAepGwh) low = middle;
        else high = middle;
    }
    return 0.5 * (low + high);
}

torch::Tensor reflect_bounds(torch::Tensor value) {
    value = torch::remainder(value, 2.0 * kFarmSide);
    value = torch::where(value < 0.0, value + 2.0 * kFarmSide, value);
    return torch::where(value > kFarmSide, 2.0 * kFarmSide - value, value);
}

double minimum_spacing(const torch::Tensor& layout) {
    auto cpu = layout.detach().to(torch::kCPU);
    double best = std::numeric_limits<double>::infinity();
    for (int i = 0; i < kTurbines; ++i) {
        for (int j = i + 1; j < kTurbines; ++j) {
            const double dx = cpu[i][0].item<double>() - cpu[j][0].item<double>();
            const double dy = cpu[i][1].item<double>() - cpu[j][1].item<double>();
            best = std::min(best, std::hypot(dx, dy));
        }
    }
    return best;
}

std::vector<double> flatten_layout(const torch::Tensor& layout) {
    auto cpu = layout.detach().to(torch::kCPU).contiguous();
    std::vector<double> result(static_cast<std::size_t>(cpu.numel()));
    auto converted = cpu.to(torch::kFloat64);
    std::copy(converted.data_ptr<double>(), converted.data_ptr<double>() + converted.numel(),
              result.begin());
    return result;
}

std::string json_escape(const std::string& value) {
    std::ostringstream output;
    for (const char character : value) {
        if (character == '\\' || character == '"') output << '\\';
        output << character;
    }
    return output.str();
}

}  // namespace

PidnnImpl::PidnnImpl() {
    velocity_encoder = register_module("velocity_encoder", torch::nn::Sequential(
        torch::nn::Linear(4, 32), torch::nn::ELU(),
        torch::nn::Linear(32, 16), torch::nn::ELU(),
        torch::nn::Linear(16, 8), torch::nn::ELU(),
        torch::nn::Linear(8, 3)));
    ct_encoder = register_module("ct_encoder", torch::nn::Sequential(
        torch::nn::Linear(1, 16), torch::nn::ELU(),
        torch::nn::Linear(16, 8), torch::nn::ELU(),
        torch::nn::Linear(8, 1)));
}

torch::Tensor PidnnImpl::velocity(const torch::Tensor& input) {
    auto normalized = torch::stack({
        input.index({Slice(), 0}) / 25.0,
        input.index({Slice(), 1}) / (30.0 * kDiameter),
        input.index({Slice(), 2}) / (5.0 * kDiameter),
        (input.index({Slice(), 3}) - kHubHeight) / (4.0 * kDiameter)}, 1);
    auto raw = velocity_encoder->forward(normalized);
    auto result = raw.clone();
    result.index_put_({Slice(), 0}, input.index({Slice(), 0})
        * torch::sigmoid(raw.index({Slice(), 0})));
    result.index_put_({Slice(), 1}, raw.index({Slice(), 1}));
    result.index_put_({Slice(), 2}, raw.index({Slice(), 2}));
    return result;
}

torch::Tensor PidnnImpl::ct(const torch::Tensor& inflow) {
    return 0.95 * torch::sigmoid(ct_encoder->forward(inflow / 25.0));
}

torch::Device resolve_device(const std::string& backend) {
    if (backend == "cpu") return torch::Device(torch::kCPU);
    if (backend == "cuda") {
        if (!torch::cuda::is_available()) throw std::runtime_error("T26 CUDA unavailable");
        return torch::Device(torch::kCUDA);
    }
    if (backend == "auto") {
        return torch::cuda::is_available() ? torch::Device(torch::kCUDA)
                                           : torch::Device(torch::kCPU);
    }
    throw std::invalid_argument("T26 backend must be auto, cpu, or cuda");
}

torch::Tensor regular_layout(const torch::Device device) {
    std::vector<float> values;
    values.reserve(2U * kTurbines);
    for (int row = 0; row < 10; ++row) {
        for (int column = 0; column < 8; ++column) {
            values.push_back(static_cast<float>(column) * 1000.0F);
            values.push_back(static_cast<float>(row) * (7000.0F / 9.0F));
        }
    }
    return torch::from_blob(values.data(), {kTurbines, 2},
        torch::TensorOptions().dtype(torch::kFloat32)).clone().to(device);
}

TrainingResult train_pidnn(const TrainingConfig& config) {
    const auto device = resolve_device(config.backend);
    if (!device.is_cuda()) at::set_num_threads(std::max(1, config.workers));
    torch::manual_seed(static_cast<std::int64_t>(config.seed));
    Pidnn model;
    model->to(device);
    model->train();
    torch::optim::Adam optimizer(model->parameters(), torch::optim::AdamOptions(5.0e-3));
    const int iterations = config.smoke ? std::min(config.iterations, 3) : config.iterations;
    const int batch = config.smoke ? std::min(config.batch_size, 96) : config.batch_size;
    double data_value = 0.0;
    double physics_value = 0.0;
    double total_value = 0.0;
    const auto started = Clock::now();
    for (int iteration = 0; iteration < iterations; ++iteration) {
        optimizer.zero_grad();
        auto input = training_batch(batch, device, config.seed, iteration);
        auto [data, physics] = losses(model, input);
        auto total = physics + 5.0 * data;
        total.backward();
        torch::nn::utils::clip_grad_norm_(model->parameters(), 100.0);
        optimizer.step();
        data_value = data.detach().item<double>();
        physics_value = physics.detach().item<double>();
        total_value = total.detach().item<double>();
    }
    synchronize(device);
    if (config.artifact.empty()) throw std::invalid_argument("T26 artifact path is required");
    const auto parent = std::filesystem::path(config.artifact).parent_path();
    if (!parent.empty()) std::filesystem::create_directories(parent);
    model->eval();
    torch::save(model, config.artifact);
    auto table = build_wake_table(model, device);
    synchronize(device);
    return {data_value, physics_value, total_value, table.direct_mae,
            elapsed(started), iterations, batch, device_name(device), config.artifact};
}

OptimizationResult run_gtde(const OptimizationConfig& config) {
    const auto device = resolve_device(config.backend);
    if (!device.is_cuda()) at::set_num_threads(std::max(1, config.workers));
    if (config.artifact.empty() || !std::filesystem::exists(config.artifact)) {
        throw std::invalid_argument("T26 trained artifact is absent");
    }
    Pidnn model;
    torch::load(model, config.artifact);
    model->to(device);
    model->eval();
    auto table_receipt = build_wake_table(model, device);
    auto table = table_receipt.deficits;
    auto regular = regular_layout(device);
    const double wake_scale = calibrate_wake_scale(regular, table);
    const int population_count = config.smoke ? std::min(config.population, 16)
                                               : config.population;
    int generations = config.smoke ? std::min(config.generations, 3)
                                   : config.generations;
    if (config.evaluation_limit > 0) {
        generations = std::min(generations,
            std::max(0, config.evaluation_limit / population_count - 1));
    }
    std::mt19937_64 random(config.seed);
    std::uniform_real_distribution<double> uniform(0.0, 1.0);
    std::normal_distribution<double> bottleneck_probability(0.01, 0.01);
    std::normal_distribution<double> amplification(0.5, 0.1);
    std::vector<float> initial(static_cast<std::size_t>(population_count * kTurbines * 2));
    auto regular_cpu = regular.to(torch::kCPU);
    for (int individual = 0; individual < population_count; ++individual) {
        for (int turbine = 0; turbine < kTurbines; ++turbine) {
            for (int coordinate = 0; coordinate < 2; ++coordinate) {
                double value = regular_cpu[turbine][coordinate].item<double>();
                if (individual != 0) value += 500.0 * (uniform(random) - 0.5);
                value = std::fmod(value, 2.0 * kFarmSide);
                if (value < 0.0) value += 2.0 * kFarmSide;
                if (value > kFarmSide) value = 2.0 * kFarmSide - value;
                initial[static_cast<std::size_t>((individual * kTurbines + turbine) * 2 + coordinate)]
                    = static_cast<float>(value);
            }
        }
    }
    auto population = torch::from_blob(initial.data(),
        {population_count, kTurbines, 2}, torch::TensorOptions().dtype(torch::kFloat32))
        .clone().to(device);
    synchronize(device);
    double evaluator_seconds = 0.0;
    const auto total_started = Clock::now();
    auto evaluation_started = Clock::now();
    auto current = evaluate_population(population, table, wake_scale);
    synchronize(device);
    evaluator_seconds += elapsed(evaluation_started);
    const double initial_aep = current.aep.index({0}).item<double>();
    std::uint64_t physical_fes = static_cast<std::uint64_t>(population_count);
    for (int generation = 0; generation < generations; ++generation) {
        const int best_index = current.fitness.argmax().item<int>();
        std::vector<std::int64_t> r1(static_cast<std::size_t>(population_count));
        std::vector<std::int64_t> r2(static_cast<std::size_t>(population_count));
        std::vector<float> factors(static_cast<std::size_t>(population_count));
        std::vector<float> masks(static_cast<std::size_t>(population_count * kTurbines * 2));
        std::vector<float> best_masks(static_cast<std::size_t>(kTurbines * 2));
        std::vector<float> target_random_values(static_cast<std::size_t>(kTurbines * 2));
        for (int i = 0; i < population_count; ++i) {
            do r1[static_cast<std::size_t>(i)] = static_cast<std::int64_t>(random() % population_count);
            while (r1[static_cast<std::size_t>(i)] == i);
            do r2[static_cast<std::size_t>(i)] = static_cast<std::int64_t>(random() % population_count);
            while (r2[static_cast<std::size_t>(i)] == i
                   || r2[static_cast<std::size_t>(i)] == r1[static_cast<std::size_t>(i)]);
            factors[static_cast<std::size_t>(i)] = static_cast<float>(
                std::clamp(amplification(random), 0.05, 1.0));
            const int forced = static_cast<int>(random() % (kTurbines * 2));
            for (int dimension = 0; dimension < kTurbines * 2; ++dimension) {
                masks[static_cast<std::size_t>(i * kTurbines * 2 + dimension)] =
                    (dimension == forced || uniform(random) < 0.9) ? 1.0F : 0.0F;
                const double probability = std::clamp(bottleneck_probability(random), 0.0, 1.0);
                if (i == best_index) {
                    best_masks[static_cast<std::size_t>(dimension)] =
                        uniform(random) < probability ? 1.0F : 0.0F;
                }
            }
        }
        for (float& value : target_random_values) {
            value = static_cast<float>(kFarmSide * uniform(random));
        }
        auto index_options = torch::TensorOptions().dtype(torch::kLong);
        auto float_options = torch::TensorOptions().dtype(torch::kFloat32);
        auto r1_tensor = torch::from_blob(r1.data(), {population_count}, index_options).clone().to(device);
        auto r2_tensor = torch::from_blob(r2.data(), {population_count}, index_options).clone().to(device);
        auto factor = torch::from_blob(factors.data(), {population_count, 1, 1}, float_options).clone().to(device);
        auto mask = torch::from_blob(masks.data(), {population_count, kTurbines, 2}, float_options).clone().to(device);
        auto best = population.index({best_index}).unsqueeze(0);
        auto mutant = population + factor * (best - population)
            + factor * (population.index({r1_tensor}) - population.index({r2_tensor}));
        auto trial = mask * mutant + (1.0 - mask) * population;
        const bool random_target = uniform(random) < 0.01;
        auto target_random = torch::from_blob(
            target_random_values.data(), {kTurbines, 2}, float_options).clone().to(device);
        auto target_mutant = best.squeeze(0) + factors[static_cast<std::size_t>(best_index)]
            * (population.index({r1[static_cast<std::size_t>(best_index)]})
               - (random_target
                    ? target_random
                    : population.index({r2[static_cast<std::size_t>(best_index)]})));
        auto target_mask = torch::from_blob(best_masks.data(), {kTurbines, 2}, float_options).clone().to(device);
        trial.index_put_({best_index}, target_mask * target_mutant
            + (1.0 - target_mask) * population.index({best_index}));
        trial = reflect_bounds(trial);
        evaluation_started = Clock::now();
        auto candidate = evaluate_population(trial, table, wake_scale);
        synchronize(device);
        evaluator_seconds += elapsed(evaluation_started);
        physical_fes += static_cast<std::uint64_t>(population_count);
        auto accept = candidate.fitness >= current.fitness;
        population = torch::where(accept.view({population_count, 1, 1}), trial, population);
        current.aep = torch::where(accept, candidate.aep, current.aep);
        current.fitness = torch::where(accept, candidate.fitness, current.fitness);
        current.violation = torch::where(accept, candidate.violation, current.violation);
    }
    synchronize(device);
    const double total_seconds = elapsed(total_started);
    const int best_index = current.fitness.argmax().item<int>();
    auto best_layout = population.index({best_index});
    const double spacing = minimum_spacing(best_layout);
    return {initial_aep, current.aep.index({best_index}).item<double>(),
            current.fitness.index({best_index}).item<double>(), spacing,
            std::max(0.0, kMinimumSpacing - spacing), evaluator_seconds,
            std::max(0.0, total_seconds - evaluator_seconds), total_seconds,
            physical_fes, generations, population_count, device_name(device),
            flatten_layout(best_layout)};
}

std::string training_json(const TrainingResult& result,
                          const TrainingConfig& config) {
    std::ostringstream output;
    output << std::setprecision(17)
        << "{\"corpus_id\":\"T26\",\"action\":\"train\""
        << ",\"problem_semantic_id\":\"t26_pidnn_hornsrev80_declared_v1\""
        << ",\"training_semantic_id\":\"t26_pidnn_rans_proxy_physics_dual_libtorch_v1\""
        << ",\"backend\":\"" << result.backend << "\""
        << ",\"workers\":" << config.workers
        << ",\"seed\":" << config.seed
        << ",\"iterations\":" << result.iterations
        << ",\"samples_per_iteration\":" << result.samples_per_iteration
        << ",\"data_loss\":" << result.data_loss
        << ",\"physics_loss\":" << result.physics_loss
        << ",\"total_loss\":" << result.total_loss
        << ",\"table_direct_mae\":" << result.table_direct_mae
        << ",\"seconds\":" << result.seconds
        << ",\"artifact\":\"" << json_escape(result.artifact) << "\"}";
    return output.str();
}

std::string optimization_json(const OptimizationResult& result,
                              const OptimizationConfig& config) {
    std::ostringstream output;
    output << std::setprecision(17)
        << "{\"corpus_id\":\"T26\",\"action\":\"optimize\""
        << ",\"problem_semantic_id\":\"t26_pidnn_hornsrev80_declared_v1\""
        << ",\"method_semantic_id\":\"t26_gtde_80_vector_genes_declared_v1\""
        << ",\"protocol_semantic_id\":\"t26_native_single_plus_25_seed_robustness_v1\""
        << ",\"backend\":\"" << result.backend << "\""
        << ",\"workers\":" << config.workers
        << ",\"seed\":" << config.seed
        << ",\"population\":" << result.population
        << ",\"generations\":" << result.generations
        << ",\"physical_fes\":" << result.physical_fes
        << ",\"initial_aep_gwh\":" << result.initial_aep_gwh
        << ",\"final_aep_gwh\":" << result.final_aep_gwh
        << ",\"final_fitness\":" << result.final_fitness
        << ",\"minimum_spacing_m\":" << result.minimum_spacing_m
        << ",\"maximum_spacing_violation_m\":" << result.maximum_spacing_violation_m
        << ",\"evaluator_seconds\":" << result.evaluator_seconds
        << ",\"algorithm_seconds\":" << result.algorithm_seconds
        << ",\"seconds\":" << result.seconds
        << ",\"layout_xy_m\":[";
    for (std::size_t index = 0; index < result.layout_xy_m.size(); ++index) {
        if (index) output << ',';
        output << result.layout_xy_m[index];
    }
    output << "]}";
    return output.str();
}

}  // namespace core99::t26
