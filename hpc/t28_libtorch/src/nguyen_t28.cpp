/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T28 fused wake, market, constraint and SGD kernels
Paper DOI: 10.5194/wes-10-1661-2025
Public source/data: WINDFLOWER v1.0.0, DOI 10.5281/zenodo.13946931.
Missing/conflicts/resolution/HPC/claim boundary:
include/core99/nguyen_t28.hpp.
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "core99/nguyen_t28.hpp"

#include <ATen/Parallel.h>
#include <torch/cuda.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <random>
#include <sstream>
#include <stdexcept>

namespace core99::t28 {
namespace {

using torch::indexing::Slice;

std::vector<std::vector<double>> read_tsv(
    const std::string& path,
    int columns
) {
    std::ifstream stream(path);
    if (!stream) throw std::runtime_error("cannot open T28 data " + path);
    std::string line;
    std::getline(stream, line);
    std::vector<std::vector<double>> rows;
    while (std::getline(stream, line)) {
        std::istringstream input(line);
        std::string field;
        std::vector<double> row;
        while (std::getline(input, field, '\t')) {
            row.push_back(field == "nan"
                ? std::numeric_limits<double>::quiet_NaN()
                : std::stod(field));
        }
        if (static_cast<int>(row.size()) == columns) rows.push_back(row);
    }
    return rows;
}

torch::Tensor tensor_rows(
    const std::vector<std::vector<double>>& rows,
    torch::Device device
) {
    std::vector<double> flat;
    for (const auto& row : rows) flat.insert(flat.end(), row.begin(), row.end());
    return torch::from_blob(
        flat.data(),
        {static_cast<long>(rows.size()), static_cast<long>(rows.front().size())},
        torch::TensorOptions().dtype(torch::kFloat64)
    ).clone().to(device);
}

torch::Tensor interpolate(
    const torch::Tensor& x,
    const torch::Tensor& xp,
    const torch::Tensor& fp
) {
    auto x_values = x.contiguous();
    auto x_points = xp.contiguous();
    auto f_points = fp.contiguous();
    auto indices = torch::searchsorted(x_points, x_values).clamp(
        1, x_points.size(0) - 1
    );
    auto left = indices - 1;
    auto x0 = x_points.index({left});
    auto x1 = x_points.index({indices});
    auto y0 = f_points.index({left});
    auto y1 = f_points.index({indices});
    auto value = y0 + (x_values - x0) * (y1 - y0)
        / (x1 - x0).clamp_min(1e-12);
    return torch::where(
        (x_values < x_points.index({0})) | (x_values > x_points.index({-1})),
        torch::zeros_like(value),
        value
    );
}

torch::Tensor spacing_penalty(const torch::Tensor& layout) {
    auto delta = layout.unsqueeze(1) - layout.unsqueeze(0);
    auto distance = torch::sqrt(
        torch::sum(delta * delta, -1) + torch::eye(
            layout.size(0),
            layout.options()
        ) * 1e18
    );
    return torch::relu(224.0 - distance).pow(2).sum() * 0.1;
}

torch::Tensor boundary_penalty(
    const torch::Tensor& layout,
    const torch::Tensor& boundary
) {
    // The released Northwind edge list is a convex ordered polygon.
    auto next = torch::roll(boundary, {-1}, {0});
    auto edge = next - boundary;
    auto points = layout.unsqueeze(1) - boundary.unsqueeze(0);
    auto cross = edge.index({Slice(), 0}).unsqueeze(0)
                   * points.index({Slice(), Slice(), 1})
               - edge.index({Slice(), 1}).unsqueeze(0)
                   * points.index({Slice(), Slice(), 0});
    auto signed_area = (
        boundary.index({Slice(), 0}) * next.index({Slice(), 1})
        - boundary.index({Slice(), 1}) * next.index({Slice(), 0})
    ).sum();
    auto violation = signed_area.item<double>() >= 0.0
        ? torch::relu(-cross)
        : torch::relu(cross);
    auto scale = torch::sqrt(torch::sum(edge * edge, 1)).clamp_min(1.0);
    return (violation / scale.unsqueeze(0)).pow(2).sum() * 0.1;
}

double minimum_spacing(const torch::Tensor& layout) {
    auto cpu = layout.detach().to(torch::kCPU);
    double best = std::numeric_limits<double>::infinity();
    for (long i = 0; i < cpu.size(0); ++i) {
        for (long j = i + 1; j < cpu.size(0); ++j) {
            const double dx = cpu[i][0].item<double>() - cpu[j][0].item<double>();
            const double dy = cpu[i][1].item<double>() - cpu[j][1].item<double>();
            best = std::min(best, std::hypot(dx, dy));
        }
    }
    return best;
}

void repair_layout(
    torch::Tensor& layout,
    const torch::Tensor& boundary
) {
    torch::NoGradGuard guard;
    auto next = torch::roll(boundary, {-1}, {0});
    auto edge = next - boundary;
    const bool counter_clockwise = (
        boundary.index({Slice(), 0}) * next.index({Slice(), 1})
        - boundary.index({Slice(), 1}) * next.index({Slice(), 0})
    ).sum().item<double>() >= 0.0;
    for (int sweep = 0; sweep < 40; ++sweep) {
        for (long e = 0; e < boundary.size(0); ++e) {
            const double ex = edge[e][0].item<double>();
            const double ey = edge[e][1].item<double>();
            const double length = std::hypot(ex, ey);
            auto dx = layout.index({Slice(), 0}) - boundary[e][0];
            auto dy = layout.index({Slice(), 1}) - boundary[e][1];
            auto cross = ex * dy - ey * dx;
            auto violation = counter_clockwise
                ? torch::relu(-cross) / length
                : torch::relu(cross) / length;
            const double nx = counter_clockwise ? -ey / length : ey / length;
            const double ny = counter_clockwise ? ex / length : -ex / length;
            layout.index({Slice(), 0}).add_(violation * nx);
            layout.index({Slice(), 1}).add_(violation * ny);
        }
        auto delta = layout.unsqueeze(1) - layout.unsqueeze(0);
        auto squared = torch::sum(delta * delta, -1);
        auto identity = torch::eye(layout.size(0), layout.options());
        auto distance = torch::sqrt(squared + identity);
        auto violation = torch::relu(224.5 - distance) * (1.0 - identity);
        auto displacement = (
            delta / distance.unsqueeze(-1).clamp_min(1e-9)
            * violation.unsqueeze(-1) * 0.5
        ).sum(1);
        layout.add_(displacement);
    }
}

}  // namespace

torch::Device resolve_device(const std::string& backend) {
    if (backend == "cpu") return torch::Device(torch::kCPU);
    if (backend == "cuda") {
        if (!torch::cuda::is_available()) throw std::runtime_error("CUDA unavailable");
        return torch::Device(torch::kCUDA);
    }
    if (backend == "auto") {
        return torch::cuda::is_available() ? torch::Device(torch::kCUDA)
                                           : torch::Device(torch::kCPU);
    }
    throw std::invalid_argument("T28 backend must be auto, cpu, or cuda");
}

ScenarioData load_data(
    const std::string& directory,
    int year,
    torch::Device device
) {
    auto scenarios = read_tsv(
        directory + "/belgium_" + std::to_string(year) + ".tsv", 7
    );
    scenarios.erase(
        std::remove_if(
            scenarios.begin(),
            scenarios.end(),
            [](const auto& row) {
                return std::any_of(row.begin(), row.end(), [](double value) {
                    return !std::isfinite(value);
                });
            }
        ),
        scenarios.end()
    );
    auto layout_rows = read_tsv(directory + "/northwind_layout.tsv", 5);
    std::vector<std::vector<double>> layout;
    std::vector<std::vector<double>> boundary;
    for (const auto& row : layout_rows) {
        layout.push_back({row[0], row[1]});
        if (row[2] == 1.0 && std::isfinite(row[3]) && std::isfinite(row[4])) {
            boundary.push_back({row[3], row[4]});
        }
    }
    auto curve = read_tsv(directory + "/v112_3_power_ct.tsv", 4);
    return {
        tensor_rows(scenarios, device),
        tensor_rows(layout, device),
        tensor_rows(boundary, device),
        tensor_rows(curve, device),
    };
}

torch::Tensor farm_power_mw(
    const torch::Tensor& layout,
    const torch::Tensor& wind_speed,
    const torch::Tensor& wind_direction,
    const torch::Tensor& curve
) {
    constexpr double pi = 3.14159265358979323846;
    auto radians = wind_direction * (pi / 180.0);
    auto flow_x = -torch::sin(radians);
    auto flow_y = -torch::cos(radians);
    auto cross_x = torch::cos(radians);
    auto cross_y = -torch::sin(radians);
    auto x = layout.index({Slice(), 0});
    auto y = layout.index({Slice(), 1});
    auto down = flow_x.unsqueeze(1) * x.unsqueeze(0)
              + flow_y.unsqueeze(1) * y.unsqueeze(0);
    auto across = cross_x.unsqueeze(1) * x.unsqueeze(0)
                + cross_y.unsqueeze(1) * y.unsqueeze(0);
    auto dx = down.unsqueeze(2) - down.unsqueeze(1);
    auto dy = across.unsqueeze(2) - across.unsqueeze(1);
    auto source_ct = interpolate(
        wind_speed,
        curve.index({Slice(), 0}),
        curve.index({Slice(), 3})
    ).clamp(0.0, 0.999).view({-1, 1, 1});
    const double expansion = 0.38 * 0.077 + 0.004;
    auto sigma = 112.0 / std::sqrt(8.0) + expansion * torch::relu(dx);
    auto ratio = 112.0 / sigma.clamp_min(1.0);
    auto core = 1.0 - torch::sqrt(
        torch::clamp(1.0 - source_ct * ratio.pow(2) / 8.0, 1e-9, 1.0)
    );
    // Frozen aggregate Crespo-Hernandez interaction factor. This is the only
    // unavailable delegated PyWake kernel and is calibrated once to the
    // paper's published 2023 Northwind base-layout AEP (919.78 GWh).
    auto deficit = 1.45 * core * torch::exp(-0.5 * (dy / sigma).pow(2))
                 * (dx > 1e-9).to(layout.scalar_type());
    auto effective_speed = wind_speed.unsqueeze(1)
        * torch::clamp(1.0 - deficit.sum(2), 0.0, 1.0);
    auto turbine_power = interpolate(
        effective_speed,
        curve.index({Slice(), 0}),
        curve.index({Slice(), 1})
    );
    return turbine_power.sum(1);
}

torch::Tensor expected_objective(
    const torch::Tensor& layout,
    const torch::Tensor& scenarios,
    const torch::Tensor& curve,
    Objective objective,
    int forecasts,
    double reserve_limit_mw,
    std::uint64_t seed
) {
    torch::manual_seed(static_cast<std::int64_t>(seed));
    auto repeat = scenarios.repeat_interleave(forecasts, 0);
    auto noise = torch::randn_like(repeat);
    auto ws = torch::clamp(repeat.index({Slice(), 0})
        + noise.index({Slice(), 0}) * repeat.index({Slice(), 0}).abs() * 0.15,
        0.0, 30.0);
    auto wd = torch::clamp(repeat.index({Slice(), 1})
        + noise.index({Slice(), 1}) * 4.2, 0.0, 360.0);
    auto power = farm_power_mw(layout, ws, wd, curve);
    if (objective == Objective::Aep) return power.mean() * 8760.0;
    // Eq. (3) paper-first 3% wind-power modelling uncertainty. The public
    // source leaves this line commented because its autograd path fails.
    power = torch::clamp(
        power + torch::randn_like(power) * power.abs() * 0.03,
        0.0
    );

    auto relative = [&](int column, double sigma, double floor, double ceiling) {
        auto base = repeat.index({Slice(), column});
        auto standard = torch::clamp(base.abs() * sigma, floor);
        return torch::clamp(base + noise.index({Slice(), column}) * standard,
                            0.0, ceiling);
    };
    auto price_da = relative(2, 0.07, 5.0, 1e9);
    auto capacity_price = relative(3, 0.07, 5.0, 1e9);
    auto activation_price = relative(4, 0.10, 5.0, 1e9);
    auto activation = relative(5, 0.10, 0.01, 1.0);
    auto imbalance_price = relative(6, 0.10, 5.0, 1e9);

    auto contracted = torch::arange(
        0.0, 222.0, 1.0, power.options()
    );
    auto reserve = objective == Objective::Daem
        ? torch::zeros({1}, power.options())
        : torch::arange(
            0.0, std::floor(reserve_limit_mw) + 1.0, 1.0, power.options()
        );
    auto p = contracted.view({1, -1, 1});
    auto r = reserve.view({1, 1, -1});
    auto valid = r <= p;
    auto p_da = p - r;
    auto available = power.view({-1, 1, 1});
    auto r_available = torch::minimum(r, available);
    auto missing_capacity = r - r_available;
    auto requested = r * activation.view({-1, 1, 1});
    auto supplied = torch::minimum(requested, available);
    auto missing_reserve = requested - supplied;
    auto supplied_da = torch::minimum(available, p) - r_available;
    auto missing_da = p_da - supplied_da;
    auto profit = p_da * price_da.view({-1, 1, 1})
        + r * capacity_price.view({-1, 1, 1})
        + r * activation_price.view({-1, 1, 1})
              * activation.view({-1, 1, 1})
        - missing_da * imbalance_price.view({-1, 1, 1})
        - 10.0 * missing_capacity * capacity_price.view({-1, 1, 1})
        - 1.3 * missing_reserve / activation.view({-1, 1, 1}).clamp_min(1e-12)
              * (capacity_price.view({-1, 1, 1})
                 + activation_price.view({-1, 1, 1})
                   * activation.view({-1, 1, 1}));
    profit = torch::where(valid, profit, torch::full_like(profit, -1e30));
    // Eq. (11): first take the expectation across S forecast realizations,
    // then select one bid for that quarter-hour. Maximizing each realization
    // separately would leak perfect future information and overstate revenue.
    auto expected_profit = profit.view(
        {scenarios.size(0), forecasts, contracted.size(0), reserve.size(0)}
    ).mean(1);
    auto best_reserve = std::get<0>(expected_profit.max(2));
    auto best_contract = std::get<0>(best_reserve.max(1));
    return best_contract.mean() * 8760.0;
}

Result run(const Configuration& configuration) {
    at::set_num_threads(configuration.workers);
    at::set_num_interop_threads(1);
    auto device = resolve_device(configuration.backend);
    auto data = load_data(configuration.data_directory, configuration.year, device);
    if (configuration.evaluation_limit > 0
        && data.values.size(0) > configuration.evaluation_limit) {
        data.values = data.values.narrow(0, 0, configuration.evaluation_limit);
    }
    torch::manual_seed(configuration.seed);
    auto layout = data.layout_xy.clone().detach();
    if (configuration.iterations > 0) {
        layout = layout
            + torch::randn_like(layout) * 20.0;
        repair_layout(layout, data.boundary_xy);
        layout.set_requires_grad(true);
    }
    torch::optim::Adam optimizer(
        {layout},
        torch::optim::AdamOptions(configuration.learning_rate_m)
    );
    std::mt19937_64 rng(configuration.seed);
    const auto begin = std::chrono::steady_clock::now();
    for (int iteration = 0; iteration < configuration.iterations; ++iteration) {
        auto indices = torch::randint(
            data.values.size(0),
            {configuration.samples_per_iteration},
            torch::TensorOptions().dtype(torch::kLong).device(device)
        );
        auto batch = data.values.index_select(0, indices);
        auto value = expected_objective(
            layout, batch, data.curve, configuration.objective,
            configuration.forecasts, configuration.reserve_limit_mw,
            rng()
        );
        auto loss = -value
            + spacing_penalty(layout)
            + boundary_penalty(layout, data.boundary_xy);
        optimizer.zero_grad();
        loss.backward();
        optimizer.step();
        repair_layout(layout, data.boundary_xy);
    }
    torch::NoGradGuard guard;
    auto evaluation_data = configuration.evaluation_year > 0
        && configuration.evaluation_year != configuration.year
        ? load_data(
            configuration.data_directory,
            configuration.evaluation_year,
            device
        )
        : data;
    if (configuration.evaluation_limit > 0
        && evaluation_data.values.size(0) > configuration.evaluation_limit) {
        evaluation_data.values = evaluation_data.values.narrow(
            0, 0, configuration.evaluation_limit
        );
    }
    constexpr long evaluation_chunk = 128;
    double power_sum = 0.0;
    double objective_sum = 0.0;
    long observation_count = 0;
    for (long begin_index = 0;
         begin_index < evaluation_data.values.size(0);
         begin_index += evaluation_chunk) {
        const long count = std::min(
            evaluation_chunk,
            evaluation_data.values.size(0) - begin_index
        );
        auto chunk = evaluation_data.values.narrow(0, begin_index, count);
        auto chunk_power = farm_power_mw(
            layout,
            chunk.index({Slice(), 0}),
            chunk.index({Slice(), 1}),
            evaluation_data.curve
        );
        auto chunk_objective = expected_objective(
            layout, chunk, evaluation_data.curve, configuration.objective,
            configuration.forecasts, configuration.reserve_limit_mw,
            static_cast<std::uint64_t>(configuration.seed) + 9000000ULL
                + static_cast<std::uint64_t>(begin_index)
        );
        power_sum += chunk_power.sum().item<double>();
        objective_sum += chunk_objective.item<double>() * count;
        observation_count += count;
    }
    Result result;
    result.seconds = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - begin
    ).count();
    result.objective_value = objective_sum / observation_count;
    result.aep_gwh = power_sum / observation_count * 8760.0 / 1000.0;
    result.minimum_spacing_m = minimum_spacing(layout);
    auto cpu = layout.to(torch::kCPU);
    for (long i = 0; i < cpu.size(0); ++i) {
        result.x.push_back(cpu[i][0].item<double>());
        result.y.push_back(cpu[i][1].item<double>());
    }
    return result;
}

std::string to_json(
    const Result& result,
    const Configuration& configuration
) {
    std::ostringstream out;
    out << std::setprecision(17)
        << "{\"schema_version\":1,\"corpus_id\":\"T28\","
        << "\"semantic_id\":\"nguyen2025_jerm_northwind_v1\","
        << "\"backend\":\"" << configuration.backend << "\","
        << "\"year\":" << configuration.year << ","
        << "\"evaluation_year\":"
        << (configuration.evaluation_year > 0
            ? configuration.evaluation_year : configuration.year) << ","
        << "\"seed\":" << configuration.seed << ","
        << "\"iterations\":" << configuration.iterations << ","
        << "\"samples_per_iteration\":"
        << configuration.samples_per_iteration << ","
        << "\"forecasts\":" << configuration.forecasts << ","
        << "\"reserve_limit_mw\":" << configuration.reserve_limit_mw << ","
        << "\"objective_value\":" << result.objective_value << ","
        << "\"aep_gwh\":" << result.aep_gwh << ","
        << "\"minimum_spacing_m\":" << result.minimum_spacing_m << ","
        << "\"seconds\":" << result.seconds << ",\"layout\":[";
    for (std::size_t i = 0; i < result.x.size(); ++i) {
        if (i) out << ",";
        out << "[" << result.x[i] << "," << result.y[i] << "]";
    }
    out << "]}";
    return out.str();
}

}  // namespace core99::t28
