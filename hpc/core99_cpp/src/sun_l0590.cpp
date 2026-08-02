/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: L0590 pure-C++ teacher, ANN training/inference, evaluator
and real-coded GA
Paper: Sun and Yang, 10.1016/j.apenergy.2023.121554.
Public source: no target code, Shiren array or trained weights located; direct
model predecessors 10.1016/j.apenergy.2018.06.027 and
10.1016/j.renene.2019.08.122 were legally recovered.
Missing fields: target calibration, private samples/weights, exact GA and
machine-readable cost curve.
Reconstruction: equation-backed deterministic 0.01D teacher samples,
from-scratch 3-5-6-1 ANN, declared real-coded GA and cost extrapolation.
Semantic IDs: l0590_shiren_3d_ann_layout_height_v1;
l0590_real_ga_completed_v1; l0590_mlp_3_5_6_1_from_scratch_v1.
Contract: shared/contracts/core99_l0590_sun_ann_height_2023.json.
Independent validator: scripts/validate_core99_l0590.py
Claim boundary: academic declared reproduction, not author source, data,
weights, exact GA/cost curve or numerical replay.
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "core99/sun_l0590.hpp"

#include "fode/executor.hpp"
#include "fode/rng.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <limits>
#include <numbers>
#include <numeric>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace core99::l0590 {
namespace {

using Clock = std::chrono::steady_clock;
constexpr double rotor_diameter_m = 77.0;
constexpr double rotor_radius_m = rotor_diameter_m / 2.0;
constexpr double reference_hub_m = 65.0;
constexpr double reference_speed_mps = 12.7;
constexpr double roughness_m = 0.03;
constexpr double wake_expansion = 0.075;
constexpr double gaussian_c = 5.15;
constexpr double minimum_spacing = 5.0 * rotor_diameter_m;
constexpr double field_extent_m = 2000.0;
constexpr int turbine_count = 30;
constexpr int population_size = 64;
constexpr int parameter_count = 63;
constexpr int deterministic_chunks = 64;

double elapsed_seconds(const Clock::time_point start) {
    return std::chrono::duration<double>(Clock::now() - start).count();
}

double sigmoid(const double value) {
    if (value >= 0.0) {
        const double exp_value = std::exp(-value);
        return 1.0 / (1.0 + exp_value);
    }
    const double exp_value = std::exp(value);
    return exp_value / (1.0 + exp_value);
}

double ambient_speed(const double height_m) {
    const double safe_height = std::max(0.5, height_m);
    return reference_speed_mps
        * std::log(safe_height / roughness_m)
        / std::log(reference_hub_m / roughness_m);
}

double turbine_power(const double speed_mps) {
    if (speed_mps < 3.0 || speed_mps >= 25.0) return 0.0;
    if (speed_mps >= 11.0) return 1513.0;
    const double numerator = speed_mps * speed_mps * speed_mps - 27.0;
    const double denominator = 1331.0 - 27.0;
    return 1513.0 * std::max(0.0, numerator / denominator);
}

double installed_cost_per_kw(const double height_m) {
    // Fig. 22 is machine-read as a monotone curve only from 60 m upward.
    // The first segment is deliberately extrapolated to the paper's 45 m
    // lower bound; the 65 m knot is calibrated to the paper's E1 total cost.
    constexpr std::array<std::array<double, 2>, 7> knots{{
        {45.0, 940.0},
        {60.0, 990.0},
        {65.0, 1006.83},
        {70.0, 1025.0},
        {75.0, 1045.0},
        {80.0, 1068.0},
        {85.0, 1093.0},
    }};
    const double value = std::clamp(height_m, 45.0, 85.0);
    for (std::size_t index = 1; index < knots.size(); ++index) {
        if (value <= knots[index][0]) {
            const double fraction = (
                value - knots[index - 1][0]
            ) / (
                knots[index][0] - knots[index - 1][0]
            );
            return knots[index - 1][1] + fraction * (
                knots[index][1] - knots[index - 1][1]
            );
        }
    }
    return knots.back()[1];
}

std::uint64_t mix_hash(std::uint64_t hash, const std::uint64_t value) {
    hash ^= value + 0x9e3779b97f4a7c15ULL + (hash << 6U) + (hash >> 2U);
    return hash;
}

std::uint64_t quantized_hash(
    const std::vector<double>& values,
    const double scale
) {
    std::uint64_t hash = 0xcbf29ce484222325ULL;
    for (const double value : values) {
        const auto quantized = static_cast<std::int64_t>(
            std::llround(value * scale)
        );
        hash = mix_hash(hash, std::bit_cast<std::uint64_t>(quantized));
    }
    return hash;
}

struct Sample {
    std::array<double, 3> input{};
    double target = 0.0;
};

struct Forward {
    std::array<double, 5> hidden1{};
    std::array<double, 6> hidden2{};
    double output = 0.0;
};

Forward forward(
    const std::array<double, parameter_count>& parameters,
    const std::array<double, 3>& input
) {
    Forward result;
    int offset = 0;
    for (int row = 0; row < 5; ++row) {
        double value = 0.0;
        for (int column = 0; column < 3; ++column) {
            value += parameters[static_cast<std::size_t>(offset++)]
                * input[static_cast<std::size_t>(column)];
        }
        value += parameters[static_cast<std::size_t>(45 + row)];
        result.hidden1[static_cast<std::size_t>(row)] = sigmoid(value);
    }
    offset = 15;
    for (int row = 0; row < 6; ++row) {
        double value = 0.0;
        for (int column = 0; column < 5; ++column) {
            value += parameters[static_cast<std::size_t>(offset++)]
                * result.hidden1[static_cast<std::size_t>(column)];
        }
        value += parameters[static_cast<std::size_t>(50 + row)];
        result.hidden2[static_cast<std::size_t>(row)] = sigmoid(value);
    }
    for (int column = 0; column < 6; ++column) {
        result.output += parameters[static_cast<std::size_t>(56 + column)]
            * result.hidden2[static_cast<std::size_t>(column)];
    }
    result.output += parameters[62];
    return result;
}

void accumulate_gradient(
    const std::array<double, parameter_count>& parameters,
    const Sample& sample,
    std::array<double, parameter_count>& gradient,
    double& squared_error
) {
    const Forward values = forward(parameters, sample.input);
    const double residual = values.output - sample.target;
    squared_error += residual * residual;
    const double output_delta = 2.0 * residual;
    std::array<double, 6> second_delta{};
    for (int unit = 0; unit < 6; ++unit) {
        const double activation = values.hidden2[static_cast<std::size_t>(unit)];
        gradient[static_cast<std::size_t>(56 + unit)] +=
            output_delta * activation;
        second_delta[static_cast<std::size_t>(unit)] =
            output_delta * parameters[static_cast<std::size_t>(56 + unit)]
            * activation * (1.0 - activation);
    }
    gradient[62] += output_delta;
    std::array<double, 5> first_delta{};
    for (int row = 0; row < 6; ++row) {
        for (int column = 0; column < 5; ++column) {
            const std::size_t parameter = static_cast<std::size_t>(
                15 + row * 5 + column
            );
            gradient[parameter] +=
                second_delta[static_cast<std::size_t>(row)]
                * values.hidden1[static_cast<std::size_t>(column)];
            first_delta[static_cast<std::size_t>(column)] +=
                second_delta[static_cast<std::size_t>(row)]
                * parameters[parameter];
        }
        gradient[static_cast<std::size_t>(50 + row)] +=
            second_delta[static_cast<std::size_t>(row)];
    }
    for (int row = 0; row < 5; ++row) {
        const double activation = values.hidden1[static_cast<std::size_t>(row)];
        const double delta = first_delta[static_cast<std::size_t>(row)]
            * activation * (1.0 - activation);
        for (int column = 0; column < 3; ++column) {
            gradient[static_cast<std::size_t>(row * 3 + column)] +=
                delta * sample.input[static_cast<std::size_t>(column)];
        }
        gradient[static_cast<std::size_t>(45 + row)] += delta;
    }
}

double minimum_layout_spacing(const std::vector<Turbine>& layout) {
    double result = std::numeric_limits<double>::infinity();
    for (std::size_t first = 0; first < layout.size(); ++first) {
        for (std::size_t second = first + 1; second < layout.size(); ++second) {
            result = std::min(result, std::hypot(
                layout[first].x_m - layout[second].x_m,
                layout[first].y_m - layout[second].y_m
            ));
        }
    }
    return result;
}

void canonicalize(std::vector<Turbine>& layout) {
    std::stable_sort(
        layout.begin(),
        layout.end(),
        [](const Turbine& left, const Turbine& right) {
            if (left.y_m != right.y_m) return left.y_m > right.y_m;
            if (left.x_m != right.x_m) return left.x_m < right.x_m;
            return left.hub_height_m < right.hub_height_m;
        }
    );
}

bool valid_layout(const std::vector<Turbine>& layout) {
    if (layout.size() != turbine_count) return false;
    for (const auto& turbine : layout) {
        if (
            turbine.x_m < 0.0 || turbine.x_m > field_extent_m
            || turbine.y_m < 0.0 || turbine.y_m > field_extent_m
            || turbine.hub_height_m < 45.0 || turbine.hub_height_m > 85.0
        ) {
            return false;
        }
    }
    return minimum_layout_spacing(layout) + 1.0e-9 >= minimum_spacing;
}

}  // namespace

struct WakeSurrogate::Impl {
    std::array<double, parameter_count> parameters{};
    bool trained = false;

    static std::array<double, 3> normalize(
        const double downstream_m,
        const double crosswind_m,
        const double vertical_offset_m
    ) {
        return {
            downstream_m / (13.0 * rotor_diameter_m) - 1.0,
            crosswind_m / (2.5 * rotor_diameter_m),
            vertical_offset_m / (2.5 * rotor_diameter_m),
        };
    }

    static double teacher(
        const double downstream_m,
        const double crosswind_m,
        const double vertical_offset_m
    ) {
        if (downstream_m < rotor_diameter_m) return 0.0;
        const double wake_radius =
            rotor_radius_m + wake_expansion * downstream_m;
        const double radial2 = crosswind_m * crosswind_m
            + vertical_offset_m * vertical_offset_m;
        if (radial2 >= wake_radius * wake_radius) return 0.0;
        const double sigma = wake_radius / gaussian_c;
        const double exponential = std::exp(
            -radial2 / (2.0 * sigma * sigma)
        );
        const double boundary = std::exp(
            -0.5 * gaussian_c * gaussian_c
        );
        // This is Eq. (12) of the recovered 2018 source, evaluated by
        // deterministic midpoint integration of the logarithmic profile.
        constexpr int integration_bins = 96;
        auto circle_flux = [&](const double radius) {
            double flux = 0.0;
            const double width = 2.0 * radius / integration_bins;
            for (int bin = 0; bin < integration_bins; ++bin) {
                const double relative_z =
                    -radius + (static_cast<double>(bin) + 0.5) * width;
                const double chord = 2.0 * std::sqrt(std::max(
                    0.0, radius * radius - relative_z * relative_z
                ));
                flux += ambient_speed(reference_hub_m + relative_z)
                    * chord * width;
            }
            return flux;
        };
        const double wake_ambient_flux = circle_flux(wake_radius);
        const double rotor_ambient_flux = circle_flux(rotor_radius_m);
        const double behind_rotor_flux =
            std::numbers::pi * rotor_radius_m * rotor_radius_m
            * (reference_speed_mps / 3.0);
        const double q = behind_rotor_flux
            + wake_ambient_flux - rotor_ambient_flux;
        const double denominator = 1.0 - boundary
            - 0.5 * gaussian_c * gaussian_c * boundary;
        const double a = (q - wake_ambient_flux) / denominator;
        const double b = -a * gaussian_c * gaussian_c
            * boundary / (2.0 * std::numbers::pi * wake_radius * wake_radius);
        const double deficit = -(
            a * exponential / (2.0 * std::numbers::pi * sigma * sigma) + b
        );
        return std::clamp(
            deficit / ambient_speed(reference_hub_m + vertical_offset_m),
            0.0,
            0.95
        );
    }
};

WakeSurrogate::WakeSurrogate() : impl_(std::make_unique<Impl>()) {}
WakeSurrogate::~WakeSurrogate() = default;
WakeSurrogate::WakeSurrogate(WakeSurrogate&&) noexcept = default;
WakeSurrogate& WakeSurrogate::operator=(WakeSurrogate&&) noexcept = default;

TrainingResult WakeSurrogate::train(const TrainingConfig& config) {
    if (
        config.workers <= 0 || config.maximum_epochs <= 0
        || config.sample_count < 1000 || !(config.target_mse > 0.0)
    ) {
        throw std::invalid_argument("invalid L0590 training configuration");
    }
    const auto started = Clock::now();
    const fode::CounterRng random(config.seed);
    std::vector<Sample> samples(static_cast<std::size_t>(config.sample_count));
    fode::PersistentExecutor executor(config.workers);
    executor.reset_work_receipt();
    executor.parallel_for(0, config.sample_count, [&](const int index) {
        const int downstream_step = 1 + random.integer(
            0, 26, 0, 1, static_cast<std::uint64_t>(index)
        );
        const double downstream = downstream_step * rotor_diameter_m;
        const double wake_radius =
            rotor_radius_m + wake_expansion * downstream;
        const double radial_fraction = std::sqrt(random.uniform(
            0, 2, static_cast<std::uint64_t>(index)
        ));
        const double angle = 2.0 * std::numbers::pi * random.uniform(
            0, 3, static_cast<std::uint64_t>(index)
        );
        // Quantization preserves the paper's 0.01D Y/Z resolution. Ten
        // percent of samples lie exactly near the boundary; the remainder
        // are area-uniform inside the only region queried at inference.
        const double radius = wake_radius * (
            index % 10 == 0 ? 0.995 : 0.98 * radial_fraction
        );
        const int crosswind_step = static_cast<int>(std::llround(
            radius * std::cos(angle) / (0.01 * rotor_diameter_m)
        ));
        const int vertical_step = static_cast<int>(std::llround(
            radius * std::sin(angle) / (0.01 * rotor_diameter_m)
        ));
        const double crosswind = crosswind_step * 0.01 * rotor_diameter_m;
        const double vertical = vertical_step * 0.01 * rotor_diameter_m;
        samples[static_cast<std::size_t>(index)] = {
            Impl::normalize(downstream, crosswind, vertical),
            Impl::teacher(downstream, crosswind, vertical),
        };
    });
    const int train_count = config.sample_count * 70 / 100;
    const int validation_count = config.sample_count * 15 / 100;
    const int test_begin = train_count + validation_count;
    for (int index = 0; index < parameter_count; ++index) {
        impl_->parameters[static_cast<std::size_t>(index)] =
            0.35 * random.normal(0, 10, static_cast<std::uint64_t>(index));
    }
    std::array<double, parameter_count> first_moment{};
    std::array<double, parameter_count> second_moment{};
    auto mse_range = [&](const int begin, const int end) {
        std::array<double, deterministic_chunks> partial{};
        executor.parallel_for(0, deterministic_chunks, [&](const int chunk) {
            const int chunk_begin = begin + (
                (end - begin) * chunk / deterministic_chunks
            );
            const int chunk_end = begin + (
                (end - begin) * (chunk + 1) / deterministic_chunks
            );
            double sum = 0.0;
            for (int index = chunk_begin; index < chunk_end; ++index) {
                const auto& sample = samples[static_cast<std::size_t>(index)];
                const double residual =
                    forward(impl_->parameters, sample.input).output
                    - sample.target;
                sum += residual * residual;
            }
            partial[static_cast<std::size_t>(chunk)] = sum;
        });
        return std::accumulate(partial.begin(), partial.end(), 0.0)
            / static_cast<double>(end - begin);
    };
    TrainingResult result;
    result.requested_workers = config.workers;
    result.train_count = train_count;
    result.validation_count = validation_count;
    result.test_count = config.sample_count - test_begin;
    double best_validation = std::numeric_limits<double>::infinity();
    std::array<double, parameter_count> best_parameters = impl_->parameters;
    int stale_epochs = 0;
    std::uint64_t update_step = 0;
    constexpr int batches_per_epoch = 8;
    for (int epoch = 1; epoch <= config.maximum_epochs; ++epoch) {
        for (int batch = 0; batch < batches_per_epoch; ++batch) {
            const int batch_begin =
                train_count * batch / batches_per_epoch;
            const int batch_end =
                train_count * (batch + 1) / batches_per_epoch;
            const int batch_count = batch_end - batch_begin;
            std::array<std::array<double, parameter_count>,
                       deterministic_chunks> chunk_gradients{};
            std::array<double, deterministic_chunks> chunk_errors{};
            executor.parallel_for(
                0,
                deterministic_chunks,
                [&](const int chunk) {
                    const int begin = batch_begin + (
                        batch_count * chunk / deterministic_chunks
                    );
                    const int end = batch_begin + (
                        batch_count * (chunk + 1) / deterministic_chunks
                    );
                    for (int index = begin; index < end; ++index) {
                        accumulate_gradient(
                            impl_->parameters,
                            samples[static_cast<std::size_t>(index)],
                            chunk_gradients[static_cast<std::size_t>(chunk)],
                            chunk_errors[static_cast<std::size_t>(chunk)]
                        );
                    }
                }
            );
            std::array<double, parameter_count> gradient{};
            for (int chunk = 0; chunk < deterministic_chunks; ++chunk) {
                for (
                    int parameter = 0;
                    parameter < parameter_count;
                    ++parameter
                ) {
                    gradient[static_cast<std::size_t>(parameter)] +=
                        chunk_gradients[static_cast<std::size_t>(chunk)]
                                       [static_cast<std::size_t>(parameter)];
                }
            }
            ++update_step;
            const double learning_rate = 0.025
                * std::max(
                    0.2,
                    1.0 - 0.0006 * static_cast<double>(epoch)
                );
            const double correction1 =
                1.0 - std::pow(0.9, static_cast<double>(update_step));
            const double correction2 =
                1.0 - std::pow(0.999, static_cast<double>(update_step));
            for (int parameter = 0; parameter < parameter_count; ++parameter) {
                const std::size_t slot = static_cast<std::size_t>(parameter);
                const double value = gradient[slot] / batch_count;
                first_moment[slot] =
                    0.9 * first_moment[slot] + 0.1 * value;
                second_moment[slot] = 0.999 * second_moment[slot]
                    + 0.001 * value * value;
                impl_->parameters[slot] -= learning_rate
                    * (first_moment[slot] / correction1)
                    / (
                        std::sqrt(second_moment[slot] / correction2)
                        + 1.0e-8
                    );
            }
        }
        const double validation = mse_range(train_count, test_begin);
        if (validation + 1.0e-12 < best_validation) {
            best_validation = validation;
            best_parameters = impl_->parameters;
            stale_epochs = 0;
        } else {
            ++stale_epochs;
        }
        result.epochs = epoch;
        if (
            validation <= config.target_mse
            || (epoch >= 350 && stale_epochs >= 250)
        ) {
            break;
        }
    }
    impl_->parameters = best_parameters;
    impl_->trained = true;
    result.train_mse = mse_range(0, train_count);
    result.validation_mse = mse_range(train_count, test_begin);
    result.test_mse = mse_range(test_begin, config.sample_count);
    result.seconds = elapsed_seconds(started);
    const auto receipt = executor.work_receipt();
    result.observed_workers = receipt.distinct_participants;
    result.scientific_hash = quantized_hash(
        std::vector<double>(impl_->parameters.begin(), impl_->parameters.end()),
        1.0e10
    );
    return result;
}

double WakeSurrogate::teacher_deficit_ratio(
    const double downstream_m,
    const double crosswind_m,
    const double vertical_offset_m
) const {
    return Impl::teacher(downstream_m, crosswind_m, vertical_offset_m);
}

double WakeSurrogate::predict_deficit_ratio(
    const double downstream_m,
    const double crosswind_m,
    const double vertical_offset_m
) const {
    if (!impl_->trained) {
        throw std::runtime_error("L0590 surrogate is not trained or loaded");
    }
    if (downstream_m < rotor_diameter_m) return 0.0;
    const double wake_radius =
        rotor_radius_m + wake_expansion * downstream_m;
    if (
        crosswind_m * crosswind_m
        + vertical_offset_m * vertical_offset_m
        >= wake_radius * wake_radius
    ) {
        return 0.0;
    }
    return std::clamp(
        forward(
            impl_->parameters,
            Impl::normalize(downstream_m, crosswind_m, vertical_offset_m)
        ).output,
        0.0,
        0.95
    );
}

void WakeSurrogate::save(const std::string& path) const {
    if (!impl_->trained) {
        throw std::runtime_error("cannot save an untrained L0590 surrogate");
    }
    std::ofstream stream(path, std::ios::binary);
    if (!stream) throw std::runtime_error("cannot open L0590 weight output");
    constexpr std::array<char, 8> magic{'L','0','5','9','0','W','1','\0'};
    stream.write(magic.data(), static_cast<std::streamsize>(magic.size()));
    stream.write(
        reinterpret_cast<const char*>(impl_->parameters.data()),
        static_cast<std::streamsize>(
            impl_->parameters.size() * sizeof(double)
        )
    );
    if (!stream) throw std::runtime_error("cannot write L0590 weights");
}

void WakeSurrogate::load(const std::string& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw std::runtime_error("cannot open L0590 weights");
    std::array<char, 8> magic{};
    stream.read(magic.data(), static_cast<std::streamsize>(magic.size()));
    constexpr std::array<char, 8> expected{'L','0','5','9','0','W','1','\0'};
    if (!stream || magic != expected) {
        throw std::runtime_error("invalid L0590 weight magic");
    }
    stream.read(
        reinterpret_cast<char*>(impl_->parameters.data()),
        static_cast<std::streamsize>(
            impl_->parameters.size() * sizeof(double)
        )
    );
    char trailing = 0;
    if (!stream || stream.read(&trailing, 1)) {
        throw std::runtime_error("invalid L0590 weight payload");
    }
    impl_->trained = true;
}

std::vector<Turbine> aligned_layout() {
    std::vector<Turbine> result;
    result.reserve(turbine_count);
    for (int row = 0; row < 5; ++row) {
        for (int column = 0; column < 6; ++column) {
            result.push_back({
                static_cast<double>(column) * 400.0,
                static_cast<double>(row) * 500.0,
                reference_hub_m,
            });
        }
    }
    canonicalize(result);
    return result;
}

struct Problem::Impl {
    std::string case_id;
    const WakeSurrogate* surrogate = nullptr;
    bool layout_variable = false;
    bool height_variable = false;
    bool cost_objective = false;
    int generation_limit = 0;

    [[nodiscard]] Evaluation evaluate_layout(
        const std::vector<Turbine>& layout
    ) const {
        Evaluation result;
        result.minimum_spacing_m = minimum_layout_spacing(layout);
        result.feasible = valid_layout(layout);
        if (!result.feasible) {
            result.objective = -std::numeric_limits<double>::infinity();
            return result;
        }
        result.turbine_speed_mps.resize(turbine_count);
        result.turbine_power_kw.resize(turbine_count);
        constexpr std::array<std::array<double, 2>, 13> rotor_points{{
            {0.0, 0.0},
            {0.5, 0.0}, {-0.5, 0.0}, {0.0, 0.5}, {0.0, -0.5},
            {0.35, 0.35}, {0.35, -0.35},
            {-0.35, 0.35}, {-0.35, -0.35},
            {0.8, 0.0}, {-0.8, 0.0}, {0.0, 0.8}, {0.0, -0.8},
        }};
        for (int target = 0; target < turbine_count; ++target) {
            double cube_sum = 0.0;
            for (const auto& rotor_point : rotor_points) {
                const double point_crosswind = rotor_point[0] * rotor_radius_m;
                const double point_height =
                    layout[static_cast<std::size_t>(target)].hub_height_m
                    + rotor_point[1] * rotor_radius_m;
                double deficit_square_sum = 0.0;
                for (int source = 0; source < turbine_count; ++source) {
                    if (source == target) continue;
                    const double downstream =
                        layout[static_cast<std::size_t>(source)].y_m
                        - layout[static_cast<std::size_t>(target)].y_m;
                    if (downstream < rotor_diameter_m) continue;
                    const double crosswind =
                        layout[static_cast<std::size_t>(target)].x_m
                        + point_crosswind
                        - layout[static_cast<std::size_t>(source)].x_m;
                    const double vertical =
                        point_height
                        - layout[static_cast<std::size_t>(source)].hub_height_m;
                    const double deficit = surrogate->predict_deficit_ratio(
                        downstream, crosswind, vertical
                    );
                    deficit_square_sum += deficit * deficit;
                }
                const double speed = ambient_speed(point_height)
                    * std::max(0.05, 1.0 - std::sqrt(deficit_square_sum));
                cube_sum += speed * speed * speed;
            }
            const double effective_speed = std::cbrt(
                cube_sum / static_cast<double>(rotor_points.size())
            );
            result.turbine_speed_mps[static_cast<std::size_t>(target)] =
                effective_speed;
            const double power = turbine_power(effective_speed);
            result.turbine_power_kw[static_cast<std::size_t>(target)] = power;
            result.total_power_kw += power;
            result.total_cost_usd += 1513.0 * installed_cost_per_kw(
                layout[static_cast<std::size_t>(target)].hub_height_m
            );
        }
        result.cost_of_power_usd_per_kw = result.total_cost_usd
            / std::max(1.0, result.total_power_kw);
        result.objective = cost_objective
            ? -result.cost_of_power_usd_per_kw
            : result.total_power_kw;
        return result;
    }
};

Problem::Problem(
    std::string case_id,
    const WakeSurrogate& surrogate
) : impl_(std::make_unique<Impl>()) {
    impl_->case_id = std::move(case_id);
    impl_->surrogate = &surrogate;
    if (impl_->case_id == "l0590_e1" || impl_->case_id == "l0590_c1") {
        impl_->generation_limit = 0;
    } else if (impl_->case_id == "l0590_e2") {
        impl_->height_variable = true;
        impl_->generation_limit = 838;
    } else if (
        impl_->case_id == "l0590_e3" || impl_->case_id == "l0590_c3"
    ) {
        impl_->layout_variable = true;
        impl_->generation_limit = 838;
    } else if (impl_->case_id == "l0590_e4") {
        impl_->layout_variable = true;
        impl_->height_variable = true;
        impl_->generation_limit = 838;
    } else if (impl_->case_id == "l0590_c2") {
        impl_->height_variable = true;
        impl_->cost_objective = true;
        impl_->generation_limit = 1017;
    } else if (impl_->case_id == "l0590_c4") {
        impl_->layout_variable = true;
        impl_->height_variable = true;
        impl_->cost_objective = true;
        impl_->generation_limit = 1017;
    } else {
        throw std::invalid_argument("unknown L0590 paper case");
    }
}

Problem::~Problem() = default;
Problem::Problem(Problem&&) noexcept = default;
Problem& Problem::operator=(Problem&&) noexcept = default;
const std::string& Problem::case_id() const noexcept {
    return impl_->case_id;
}
const std::string& Problem::semantic_id() const noexcept {
    static const std::string semantic =
        "l0590_shiren_3d_ann_layout_height_v1";
    return semantic;
}
bool Problem::optimizes_layout() const noexcept {
    return impl_->layout_variable;
}
bool Problem::optimizes_height() const noexcept {
    return impl_->height_variable;
}
bool Problem::minimizes_cost() const noexcept {
    return impl_->cost_objective;
}
int Problem::paper_generation_limit() const noexcept {
    return impl_->generation_limit;
}
Evaluation Problem::evaluate(const std::vector<Turbine>& layout) const {
    return impl_->evaluate_layout(layout);
}

RunResult Problem::optimize(const RunConfig& config) const {
    if (config.workers <= 0) {
        throw std::invalid_argument("L0590 workers must be positive");
    }
    const int generation_limit = config.generations >= 0
        ? config.generations : impl_->generation_limit;
    if (generation_limit < 0) {
        throw std::invalid_argument("L0590 generations must be nonnegative");
    }
    struct Individual {
        std::vector<Turbine> layout;
        Evaluation evaluation;
    };
    auto better = [](const Individual& left, const Individual& right) {
        if (left.evaluation.objective != right.evaluation.objective) {
            return left.evaluation.objective > right.evaluation.objective;
        }
        for (std::size_t index = 0; index < left.layout.size(); ++index) {
            if (left.layout[index].y_m != right.layout[index].y_m) {
                return left.layout[index].y_m > right.layout[index].y_m;
            }
            if (left.layout[index].x_m != right.layout[index].x_m) {
                return left.layout[index].x_m < right.layout[index].x_m;
            }
            if (
                left.layout[index].hub_height_m
                != right.layout[index].hub_height_m
            ) {
                return left.layout[index].hub_height_m
                    < right.layout[index].hub_height_m;
            }
        }
        return false;
    };
    const auto started = Clock::now();
    RunResult result;
    result.case_id = impl_->case_id;
    result.problem_semantic_id = semantic_id();
    result.method_semantic_id = "l0590_real_ga_completed_v1";
    result.training_semantic_id = "l0590_mlp_3_5_6_1_from_scratch_v1";
    result.seed = config.seed;
    result.requested_workers = config.workers;
    if (generation_limit == 0) {
        result.best_layout = aligned_layout();
        result.initial_best = impl_->evaluate_layout(result.best_layout);
        result.best_evaluation = result.initial_best;
        result.physical_fes = 1;
        result.best_objective_history = {result.best_evaluation.objective};
        std::vector<double> hash_values;
        for (const auto& turbine : result.best_layout) {
            hash_values.insert(
                hash_values.end(),
                {turbine.x_m, turbine.y_m, turbine.hub_height_m}
            );
        }
        result.scientific_hash = quantized_hash(hash_values, 1.0e6);
        result.end_to_end_seconds = elapsed_seconds(started);
        result.evaluator_seconds = result.end_to_end_seconds;
        result.observed_workers = 1;
        return result;
    }
    const fode::CounterRng random(config.seed);
    fode::PersistentExecutor executor(config.workers);
    executor.reset_work_receipt();
    const auto base = aligned_layout();
    std::vector<Individual> population(population_size);
    for (int individual = 0; individual < population_size; ++individual) {
        auto layout = base;
        if (impl_->layout_variable && individual != 0) {
            for (int turbine = 0; turbine < turbine_count; ++turbine) {
                const double dx = 7.0 * random.normal(
                    0, 100, static_cast<std::uint64_t>(individual),
                    static_cast<std::uint64_t>(turbine), 0
                );
                const double dy = 7.0 * random.normal(
                    0, 101, static_cast<std::uint64_t>(individual),
                    static_cast<std::uint64_t>(turbine), 0
                );
                const double old_x = layout[static_cast<std::size_t>(turbine)].x_m;
                const double old_y = layout[static_cast<std::size_t>(turbine)].y_m;
                layout[static_cast<std::size_t>(turbine)].x_m =
                    std::clamp(old_x + dx, 0.0, field_extent_m);
                layout[static_cast<std::size_t>(turbine)].y_m =
                    std::clamp(old_y + dy, 0.0, field_extent_m);
                if (!valid_layout(layout)) {
                    layout[static_cast<std::size_t>(turbine)].x_m = old_x;
                    layout[static_cast<std::size_t>(turbine)].y_m = old_y;
                }
            }
        }
        if (impl_->height_variable && individual != 0) {
            for (int turbine = 0; turbine < turbine_count; ++turbine) {
                layout[static_cast<std::size_t>(turbine)].hub_height_m =
                    45.0 + 40.0 * random.uniform(
                        0, 102, static_cast<std::uint64_t>(individual),
                        static_cast<std::uint64_t>(turbine)
                    );
            }
        }
        canonicalize(layout);
        population[static_cast<std::size_t>(individual)].layout =
            std::move(layout);
    }
    double evaluator_seconds = 0.0;
    auto evaluate_population = [&](std::vector<Individual>& values) {
        const auto evaluate_start = Clock::now();
        executor.parallel_for(0, static_cast<int>(values.size()), [&](int i) {
            values[static_cast<std::size_t>(i)].evaluation =
                impl_->evaluate_layout(
                    values[static_cast<std::size_t>(i)].layout
                );
        });
        evaluator_seconds += elapsed_seconds(evaluate_start);
    };
    evaluate_population(population);
    std::stable_sort(population.begin(), population.end(), better);
    result.initial_best = population.front().evaluation;
    result.best_objective_history.push_back(
        population.front().evaluation.objective
    );
    result.physical_fes = population_size;
    auto tournament = [&](const int generation, const int child, const int draw) {
        const int first = random.integer(
            0, population_size, generation, 200,
            static_cast<std::uint64_t>(child),
            static_cast<std::uint64_t>(draw), 0
        );
        const int second = random.integer(
            0, population_size, generation, 200,
            static_cast<std::uint64_t>(child),
            static_cast<std::uint64_t>(draw), 1
        );
        return better(
            population[static_cast<std::size_t>(first)],
            population[static_cast<std::size_t>(second)]
        ) ? first : second;
    };
    for (int generation = 1; generation <= generation_limit; ++generation) {
        std::vector<Individual> offspring(population_size);
        for (int child = 0; child < population_size; ++child) {
            const int parent_a = tournament(generation, child, 0);
            int parent_b = tournament(generation, child, 1);
            if (parent_b == parent_a) parent_b = (parent_b + 1) % population_size;
            auto layout =
                population[static_cast<std::size_t>(parent_a)].layout;
            const auto& other =
                population[static_cast<std::size_t>(parent_b)].layout;
            if (impl_->height_variable) {
                for (int turbine = 0; turbine < turbine_count; ++turbine) {
                    if (
                        random.uniform(generation, 201, child, turbine) < 0.5
                    ) {
                        layout[static_cast<std::size_t>(turbine)].hub_height_m =
                            0.5 * (
                                layout[static_cast<std::size_t>(turbine)]
                                    .hub_height_m
                                + other[static_cast<std::size_t>(turbine)]
                                    .hub_height_m
                            );
                    }
                }
            }
            if (impl_->layout_variable) {
                auto candidate = layout;
                for (int turbine = 0; turbine < turbine_count; ++turbine) {
                    if (
                        random.uniform(generation, 202, child, turbine) < 0.2
                    ) {
                        candidate[static_cast<std::size_t>(turbine)].x_m =
                            0.5 * (
                                layout[static_cast<std::size_t>(turbine)].x_m
                                + other[static_cast<std::size_t>(turbine)].x_m
                            );
                        candidate[static_cast<std::size_t>(turbine)].y_m =
                            0.5 * (
                                layout[static_cast<std::size_t>(turbine)].y_m
                                + other[static_cast<std::size_t>(turbine)].y_m
                            );
                    }
                }
                canonicalize(candidate);
                if (valid_layout(candidate)) layout = std::move(candidate);
            }
            if (impl_->height_variable) {
                const int slot = random.integer(
                    0, turbine_count, generation, 203, child
                );
                const double sigma = 4.0 * (
                    1.0 - 0.75 * static_cast<double>(generation)
                    / static_cast<double>(generation_limit)
                );
                auto& height =
                    layout[static_cast<std::size_t>(slot)].hub_height_m;
                height = std::clamp(
                    height + sigma * random.normal(generation, 204, child),
                    45.0,
                    85.0
                );
            }
            if (impl_->layout_variable) {
                const int slot = random.integer(
                    0, turbine_count, generation, 205, child
                );
                const auto original = layout[static_cast<std::size_t>(slot)];
                bool accepted = false;
                for (int attempt = 0; attempt < 12; ++attempt) {
                    const double scale = 35.0 + 85.0 * (
                        1.0 - static_cast<double>(generation)
                        / static_cast<double>(generation_limit)
                    );
                    layout[static_cast<std::size_t>(slot)].x_m = std::clamp(
                        original.x_m + scale * random.normal(
                            generation, 206, child, attempt, 0
                        ),
                        0.0,
                        field_extent_m
                    );
                    layout[static_cast<std::size_t>(slot)].y_m = std::clamp(
                        original.y_m + scale * random.normal(
                            generation, 207, child, attempt, 0
                        ),
                        0.0,
                        field_extent_m
                    );
                    if (valid_layout(layout)) {
                        accepted = true;
                        break;
                    }
                }
                if (!accepted) {
                    layout[static_cast<std::size_t>(slot)] = original;
                }
            }
            canonicalize(layout);
            offspring[static_cast<std::size_t>(child)].layout =
                std::move(layout);
        }
        evaluate_population(offspring);
        result.physical_fes += population_size;
        population.insert(
            population.end(),
            std::make_move_iterator(offspring.begin()),
            std::make_move_iterator(offspring.end())
        );
        std::stable_sort(population.begin(), population.end(), better);
        population.resize(population_size);
        result.best_objective_history.push_back(
            population.front().evaluation.objective
        );
    }
    result.generations = generation_limit;
    result.best_layout = population.front().layout;
    result.best_evaluation = population.front().evaluation;
    result.evaluator_seconds = evaluator_seconds;
    result.end_to_end_seconds = elapsed_seconds(started);
    result.algorithm_seconds = std::max(
        0.0, result.end_to_end_seconds - result.evaluator_seconds
    );
    const auto receipt = executor.work_receipt();
    result.observed_workers = receipt.distinct_participants;
    std::vector<double> hash_values = result.best_objective_history;
    for (const auto& turbine : result.best_layout) {
        hash_values.insert(
            hash_values.end(),
            {turbine.x_m, turbine.y_m, turbine.hub_height_m}
        );
    }
    result.scientific_hash = quantized_hash(hash_values, 1.0e6);
    return result;
}

std::vector<std::string> paper_case_ids() {
    return {
        "l0590_e1", "l0590_e2", "l0590_e3", "l0590_e4",
        "l0590_c1", "l0590_c2", "l0590_c3", "l0590_c4",
    };
}

}  // namespace core99::l0590
