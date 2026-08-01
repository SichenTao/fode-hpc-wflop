/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: L0649 pure-C++ FLOWERS model, gradient and projected
L-BFGS CPU-HPC implementation
Paper/DOI: LoCascio et al.; 10.1002/WE.2954.
Public source: paper-linked https://github.com/locascio-m/flowers at paper-era
revision dcb729f7ea4ab9307344e45c329b6f50796e861b is an unlicensed numerical
oracle; no author code is redistributed.
Missing and Reconstruction: SNOPT state, histories, final coordinates and
randomized-case RNG are absent. Printed FLOWERS equations are independently
implemented and SNOPT is replaced by deterministic projected L-BFGS. The
paper/source random-grid conflict and every completion decision are recorded
in include/core99/locascio_l0649.hpp.
Semantic IDs: l0649_flowers_aep_analytic_gradient_projected_lbfgs_v1,
l0649_wr7_nine_turbine_14d_square_v1 and
l0649_native_single_optimization_plus_n500_h6_v1.
Claim boundary: source-oracled flexible academic reproduction; full boundary
is recorded in include/core99/locascio_l0649.hpp.
HPC realization: immutable pair slots are computed by one persistent executor;
all scientific reductions are deterministic in target-source index order.
Controlling contract: shared/contracts/core99_l0649_flowers_aep_2024.json.
Last evidence-audit date: 2026-08-01
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "core99/locascio_l0649.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <limits>
#include <numbers>
#include <numeric>
#include <stdexcept>
#include <utility>
#include <vector>

namespace core99::l0649 {
namespace {

using Clock = std::chrono::steady_clock;
constexpr double kRotorDiameterM = 126.0;
constexpr double kCutOutSpeedMps = 25.0;
constexpr double kWakeExpansion = 0.05;
constexpr double kAirDensity = 1.225;
constexpr double kHoursPerYear = 8760.0;
constexpr double kBoundarySideM = 14.0 * kRotorDiameterM;
constexpr int kTurbines = 9;
constexpr int kMaximumModes = 10;
constexpr double kWr7Freestream = 0.25288446457987956;
constexpr std::array<double, kMaximumModes> kWr7A{
    0.26235275907257194,
    -0.02627115678401385,
    -0.03466842004127342,
    0.011447972080701665,
    0.0012219671424580692,
    -0.002958954257572057,
    0.0027995850262601575,
    -0.0028112535923626053,
    -0.0003272505954031815,
    -0.00009222481554872692,
};
constexpr std::array<double, kMaximumModes> kWr7B{
    -0.0,
    -0.011376467651948179,
    0.010384637292032498,
    0.01314969916552662,
    -0.022227205216820194,
    -0.003124945846808366,
    0.008723557031588603,
    0.002728161268318766,
    -0.00930857376625241,
    0.0008703165766409636,
};

double elapsed(const Clock::time_point start) {
    return std::chrono::duration<double>(Clock::now() - start).count();
}

struct PairTerm {
    double deficit = 0.0;
    double derivative_x = 0.0;
    double derivative_y = 0.0;
};

PairTerm pair_term(
    const double x_normalized,
    const double y_normalized,
    const int modes,
    const bool gradient
) {
    const double radius2 = x_normalized * x_normalized
        + y_normalized * y_normalized;
    const double radius = std::sqrt(radius2);
    if (radius <= 0.5) {
        return {kWr7Freestream, 0.0, 0.0};
    }
    const double theta = std::atan2(y_normalized, x_normalized)
        / (2.0 * std::numbers::pi);
    const double inverse_2r = 1.0 / (2.0 * radius);
    const double radical = std::sqrt(
        1.0 + kWakeExpansion * kWakeExpansion
        - inverse_2r * inverse_2r
    );
    const double theta_critical = std::atan(
        (inverse_2r + kWakeExpansion * radical)
        / (-kWakeExpansion * inverse_2r + radical)
    ) / (2.0 * std::numbers::pi);
    const double denominator = 1.0 + 2.0 * kWakeExpansion * radius;
    double deficit = kWr7A[0] * theta_critical
        / (denominator * denominator)
        * (1.0 + 8.0 * std::numbers::pi * std::numbers::pi
           * theta_critical * theta_critical * kWakeExpansion * radius
           / (3.0 * denominator));

    double radial_derivative = 0.0;
    double angular_derivative = 0.0;
    double critical_radial = 0.0;
    if (gradient) {
        critical_radial = -1.0 /
            (4.0 * std::numbers::pi * radius2
             * std::sqrt(kWakeExpansion * kWakeExpansion
                         - inverse_2r * inverse_2r + 1.0));
        radial_derivative =
            (-4.0 * kWr7A[0] * kWakeExpansion * theta_critical
             * (3.0 + 6.0 * kWakeExpansion * radius
                + 2.0 * std::numbers::pi * std::numbers::pi
                  * (4.0 * kWakeExpansion * radius - 1.0)
                  * theta_critical * theta_critical)
             + 3.0 * kWr7A[0] * denominator
               * (denominator
                  + 8.0 * std::numbers::pi * std::numbers::pi
                    * kWakeExpansion * radius
                    * theta_critical * theta_critical)
               * critical_radial)
            / (3.0 * denominator * denominator
               * denominator * denominator);
    }

    for (int mode = 1; mode < modes; ++mode) {
        const double m = static_cast<double>(mode);
        const double phase = 2.0 * std::numbers::pi * m * theta;
        const double edge_phase =
            2.0 * std::numbers::pi * m * theta_critical;
        const double orientation = kWr7A[static_cast<std::size_t>(mode)]
                * std::cos(phase)
            + kWr7B[static_cast<std::size_t>(mode)] * std::sin(phase);
        const double edge = std::sin(edge_phase)
            + 2.0 * kWakeExpansion * radius / (m * m * denominator)
              * (((edge_phase * edge_phase) - 2.0)
                 * std::sin(edge_phase)
                 + 4.0 * std::numbers::pi * m * theta_critical
                   * std::cos(edge_phase));
        deficit += orientation * edge
            / (std::numbers::pi * m * denominator * denominator);

        if (!gradient) continue;
        angular_derivative += 2.0
            / (denominator * denominator)
            * (kWr7B[static_cast<std::size_t>(mode)] * std::cos(phase)
               - kWr7A[static_cast<std::size_t>(mode)] * std::sin(phase))
            * edge;
        radial_derivative += orientation
            / (std::numbers::pi * m * m * m
               * denominator * denominator
               * denominator * denominator)
            * (-4.0 * kWakeExpansion * std::sin(edge_phase)
               * (1.0 + m * m
                  + 2.0 * kWakeExpansion * radius * (m * m - 2.0)
                  + 2.0 * std::numbers::pi * std::numbers::pi * m * m
                    * (4.0 * kWakeExpansion * radius - 1.0)
                    * theta_critical * theta_critical)
               + 2.0 * std::numbers::pi * m * std::cos(edge_phase)
                 * (4.0 * kWakeExpansion
                    * (1.0 - 4.0 * kWakeExpansion * radius)
                    * theta_critical
                    + m * m * denominator
                      * (denominator
                         + 8.0 * std::numbers::pi * std::numbers::pi
                           * kWakeExpansion * radius
                           * theta_critical * theta_critical)
                      * critical_radial));
    }

    PairTerm result;
    result.deficit = deficit;
    if (gradient) {
        result.derivative_x = x_normalized / radius * radial_derivative
            - y_normalized / (2.0 * std::numbers::pi * radius2)
              * angular_derivative;
        result.derivative_y = y_normalized / radius * radial_derivative
            + x_normalized / (2.0 * std::numbers::pi * radius2)
              * angular_derivative;
    }
    return result;
}

double dot(const std::vector<double>& left, const std::vector<double>& right) {
    double result = 0.0;
    for (std::size_t index = 0; index < left.size(); ++index) {
        result += left[index] * right[index];
    }
    return result;
}

std::vector<double> flatten(const std::vector<Point>& layout) {
    std::vector<double> result(layout.size() * 2);
    for (std::size_t index = 0; index < layout.size(); ++index) {
        result[index] = layout[index].x_m;
        result[layout.size() + index] = layout[index].y_m;
    }
    return result;
}

std::vector<Point> points(const std::vector<double>& values) {
    const std::size_t n = values.size() / 2;
    std::vector<Point> result(n);
    for (std::size_t index = 0; index < n; ++index) {
        result[index] = {values[index], values[n + index]};
    }
    return result;
}

std::vector<double> projected_gradient(
    const std::vector<double>& values,
    const std::vector<double>& gradient
) {
    std::vector<double> result = gradient;
    for (std::size_t index = 0; index < values.size(); ++index) {
        if ((values[index] <= 1.0e-12 && gradient[index] > 0.0)
            || (values[index] >= kBoundarySideM - 1.0e-12
                && gradient[index] < 0.0)) {
            result[index] = 0.0;
        }
    }
    return result;
}

double norm_infinity(const std::vector<double>& values) {
    double result = 0.0;
    for (const double value : values) {
        result = std::max(result, std::abs(value));
    }
    return result;
}

std::uint64_t hash_result(const RunResult& result) {
    std::uint64_t hash = 1469598103934665603ULL;
    auto mix = [&](const std::uint64_t value) {
        hash ^= value;
        hash *= 1099511628211ULL;
    };
    mix(static_cast<std::uint64_t>(result.iterations));
    mix(static_cast<std::uint64_t>(result.objective_gradient_calls));
    mix(std::bit_cast<std::uint64_t>(result.initial_evaluation.aep_wh));
    mix(std::bit_cast<std::uint64_t>(result.final_evaluation.aep_wh));
    for (const Point& point : result.final_layout) {
        mix(std::bit_cast<std::uint64_t>(point.x_m));
        mix(std::bit_cast<std::uint64_t>(point.y_m));
    }
    return hash;
}

std::uint64_t splitmix64(std::uint64_t value) {
    value += 0x9e3779b97f4a7c15ULL;
    value = (value ^ (value >> 30U)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27U)) * 0x94d049bb133111ebULL;
    return value ^ (value >> 31U);
}

}  // namespace

FlowersModel::FlowersModel(const int workers, const int fourier_modes)
    : workers_(workers), modes_(fourier_modes) {
    if (workers_ <= 0) throw std::invalid_argument("L0649 workers must be positive");
    if (modes_ <= 0 || modes_ > kMaximumModes) {
        throw std::invalid_argument("L0649 supports one to ten WR7 modes");
    }
    for (const double x_d : {3.0, 7.0, 11.0}) {
        for (const double y_d : {3.0, 7.0, 11.0}) {
            initial_layout_.push_back({x_d * kRotorDiameterM,
                                       y_d * kRotorDiameterM});
        }
    }
}

int FlowersModel::fourier_modes() const noexcept { return modes_; }
double FlowersModel::rotor_diameter_m() const noexcept {
    return kRotorDiameterM;
}
double FlowersModel::boundary_side_m() const noexcept {
    return kBoundarySideM;
}
const std::vector<Point>& FlowersModel::initial_layout() const noexcept {
    return initial_layout_;
}

Evaluation FlowersModel::evaluate(
    const std::vector<Point>& layout,
    const bool gradient,
    fode::PersistentExecutor& executor
) const {
    if (layout.empty()) throw std::invalid_argument("L0649 layout is empty");
    for (const Point& point : layout) {
        if (!std::isfinite(point.x_m) || !std::isfinite(point.y_m)) {
            throw std::invalid_argument("L0649 layout is non-finite");
        }
    }
    const auto start = Clock::now();
    const int n = static_cast<int>(layout.size());
    std::vector<PairTerm> pairs(static_cast<std::size_t>(n * n));
    executor.reset_work_receipt();
    executor.parallel_for(0, n, [&](const int target) {
        for (int source = 0; source < n; ++source) {
            if (target == source) continue;
            const double dx = (layout[static_cast<std::size_t>(source)].x_m
                - layout[static_cast<std::size_t>(target)].x_m)
                / kRotorDiameterM;
            const double dy = (layout[static_cast<std::size_t>(source)].y_m
                - layout[static_cast<std::size_t>(target)].y_m)
                / kRotorDiameterM;
            pairs[static_cast<std::size_t>(target * n + source)] =
                pair_term(dx, dy, modes_, gradient);
        }
    });

    std::vector<double> incident(static_cast<std::size_t>(n));
    for (int target = 0; target < n; ++target) {
        double deficit = 0.0;
        for (int source = 0; source < n; ++source) {
            deficit += pairs[static_cast<std::size_t>(target * n + source)]
                .deficit;
        }
        incident[static_cast<std::size_t>(target)] =
            kWr7Freestream - deficit;
    }
    const double coefficient = std::numbers::pi / 8.0 * kAirDensity
        * kRotorDiameterM * kRotorDiameterM
        * kCutOutSpeedMps * kCutOutSpeedMps * kCutOutSpeedMps
        * kHoursPerYear;
    double normalized_power = 0.0;
    for (const double speed : incident) {
        normalized_power += speed * speed * speed;
    }

    Evaluation result;
    result.aep_wh = coefficient * normalized_power;
    result.turbines = n;
    result.fourier_modes = modes_;
    result.requested_workers = executor.thread_count();
    result.ordered_pair_terms = static_cast<std::uint64_t>(n)
        * static_cast<std::uint64_t>(n - 1)
        * static_cast<std::uint64_t>(modes_);
    if (gradient) {
        result.gradient_wh_per_m.resize(static_cast<std::size_t>(n));
        const double gradient_coefficient =
            -3.0 * coefficient / kRotorDiameterM;
        executor.parallel_for(0, n, [&](const int moved) {
            double x_value = 0.0;
            double y_value = 0.0;
            const double moved_weight = incident[static_cast<std::size_t>(moved)]
                * incident[static_cast<std::size_t>(moved)];
            for (int other = 0; other < n; ++other) {
                const PairTerm& row = pairs[
                    static_cast<std::size_t>(moved * n + other)];
                x_value -= moved_weight * row.derivative_x;
                y_value -= moved_weight * row.derivative_y;
                const double other_weight =
                    incident[static_cast<std::size_t>(other)]
                    * incident[static_cast<std::size_t>(other)];
                const PairTerm& column = pairs[
                    static_cast<std::size_t>(other * n + moved)];
                x_value += other_weight * column.derivative_x;
                y_value += other_weight * column.derivative_y;
            }
            result.gradient_wh_per_m[static_cast<std::size_t>(moved)] = {
                gradient_coefficient * x_value,
                gradient_coefficient * y_value,
            };
        });
    }
    result.observed_workers = executor.work_receipt().distinct_participants;
    result.seconds = elapsed(start);
    return result;
}

std::vector<Point> make_paper_scale_layout(
    const int turbines,
    const std::uint64_t seed
) {
    if (turbines < 2 || turbines > 500) {
        throw std::invalid_argument("L0649 paper scale requires 2..500 turbines");
    }
    const int columns = turbines + 1;
    const int rows = 6;
    std::vector<int> slots(static_cast<std::size_t>(columns * rows));
    std::iota(slots.begin(), slots.end(), 0);
    for (std::size_t index = slots.size(); index > 1; --index) {
        const std::uint64_t random = splitmix64(seed + index);
        const std::size_t selected = static_cast<std::size_t>(random % index);
        std::swap(slots[index - 1], slots[selected]);
    }
    std::vector<Point> result(static_cast<std::size_t>(turbines));
    for (int index = 0; index < turbines; ++index) {
        const int slot = slots[static_cast<std::size_t>(index)];
        result[static_cast<std::size_t>(index)] = {
            3.0 * kRotorDiameterM * static_cast<double>(slot % columns),
            3.0 * kRotorDiameterM * static_cast<double>(slot / columns),
        };
    }
    return result;
}

RunResult run(const FlowersModel& model, const RunConfig& config) {
    if (config.workers <= 0 || config.maximum_iterations <= 0
        || config.optimality_tolerance <= 0.0) {
        throw std::invalid_argument("L0649 invalid run configuration");
    }
    const auto total_start = Clock::now();
    fode::PersistentExecutor executor(config.workers);
    RunResult result;
    result.requested_workers = config.workers;
    result.initial_evaluation = model.evaluate(
        model.initial_layout(), true, executor);
    result.observed_workers = result.initial_evaluation.observed_workers;
    result.evaluator_seconds += result.initial_evaluation.seconds;
    ++result.objective_gradient_calls;
    const double objective_scale = result.initial_evaluation.aep_wh / 1.0e4;

    std::vector<double> values = flatten(model.initial_layout());
    auto scaled = [&](const Evaluation& evaluation) {
        std::vector<double> gradient(values.size());
        for (std::size_t index = 0;
             index < evaluation.gradient_wh_per_m.size(); ++index) {
            gradient[index] =
                -evaluation.gradient_wh_per_m[index].x_m / objective_scale;
            gradient[evaluation.gradient_wh_per_m.size() + index] =
                -evaluation.gradient_wh_per_m[index].y_m / objective_scale;
        }
        return std::pair{-evaluation.aep_wh / objective_scale,
                         std::move(gradient)};
    };
    auto [objective, gradient] = scaled(result.initial_evaluation);
    std::deque<std::vector<double>> s_history;
    std::deque<std::vector<double>> y_history;
    std::deque<double> inverse_curvature;
    constexpr std::size_t memory = 10;
    const int maximum_iterations = config.smoke
        ? std::min(3, config.maximum_iterations)
        : config.maximum_iterations;
    const auto algorithm_start = Clock::now();

    for (int iteration = 0; iteration < maximum_iterations; ++iteration) {
        const std::vector<double> projected =
            projected_gradient(values, gradient);
        const double projected_norm = norm_infinity(projected);
        if (projected_norm <= config.optimality_tolerance) break;

        std::vector<double> direction = projected;
        std::vector<double> alpha(s_history.size());
        for (std::size_t reverse = s_history.size(); reverse > 0; --reverse) {
            const std::size_t index = reverse - 1;
            alpha[index] = inverse_curvature[index]
                * dot(s_history[index], direction);
            for (std::size_t slot = 0; slot < direction.size(); ++slot) {
                direction[slot] -= alpha[index] * y_history[index][slot];
            }
        }
        if (!s_history.empty()) {
            const auto& last_s = s_history.back();
            const auto& last_y = y_history.back();
            const double yy = dot(last_y, last_y);
            const double gamma = yy > 0.0 ? dot(last_s, last_y) / yy : 1.0;
            for (double& value : direction) value *= gamma;
        }
        for (std::size_t index = 0; index < s_history.size(); ++index) {
            const double beta = inverse_curvature[index]
                * dot(y_history[index], direction);
            for (std::size_t slot = 0; slot < direction.size(); ++slot) {
                direction[slot] += s_history[index][slot]
                    * (alpha[index] - beta);
            }
        }
        for (double& value : direction) value = -value;
        for (std::size_t index = 0; index < direction.size(); ++index) {
            if (projected[index] == 0.0) direction[index] = 0.0;
        }
        if (!(dot(gradient, direction) < 0.0)) {
            direction = projected;
            for (double& value : direction) value = -value;
        }

        double step = s_history.empty() ? 0.5 * kRotorDiameterM : 1.0;
        std::vector<double> candidate(values.size());
        Evaluation candidate_evaluation;
        double candidate_objective = objective;
        std::vector<double> candidate_gradient;
        bool accepted = false;
        for (int line_search = 0; line_search < 40; ++line_search) {
            for (std::size_t index = 0; index < values.size(); ++index) {
                candidate[index] = std::clamp(
                    values[index] + step * direction[index],
                    0.0, kBoundarySideM);
            }
            std::vector<double> displacement(candidate.size());
            for (std::size_t index = 0; index < candidate.size(); ++index) {
                displacement[index] = candidate[index] - values[index];
            }
            const double directional = dot(gradient, displacement);
            if (!(directional < 0.0)) {
                step *= 0.5;
                continue;
            }
            candidate_evaluation = model.evaluate(
                points(candidate), true, executor);
            result.evaluator_seconds += candidate_evaluation.seconds;
            result.observed_workers = std::max(
                result.observed_workers,
                candidate_evaluation.observed_workers);
            ++result.objective_gradient_calls;
            auto scaled_candidate = scaled(candidate_evaluation);
            candidate_objective = scaled_candidate.first;
            candidate_gradient = std::move(scaled_candidate.second);
            if (candidate_objective
                <= objective + 1.0e-4 * directional) {
                accepted = true;
                break;
            }
            step *= 0.5;
        }
        if (!accepted) break;

        std::vector<double> s(values.size());
        std::vector<double> y(values.size());
        for (std::size_t index = 0; index < values.size(); ++index) {
            s[index] = candidate[index] - values[index];
            y[index] = candidate_gradient[index] - gradient[index];
        }
        const double curvature = dot(s, y);
        if (curvature > 1.0e-12 * std::max(1.0, dot(s, s))) {
            if (s_history.size() == memory) {
                s_history.pop_front();
                y_history.pop_front();
                inverse_curvature.pop_front();
            }
            s_history.push_back(s);
            y_history.push_back(y);
            inverse_curvature.push_back(1.0 / curvature);
        }
        values = std::move(candidate);
        objective = candidate_objective;
        gradient = std::move(candidate_gradient);
        result.history.push_back({
            iteration + 1,
            objective,
            candidate_evaluation.aep_wh,
            norm_infinity(projected_gradient(values, gradient)),
            step,
        });
        result.iterations = iteration + 1;
        result.final_evaluation = std::move(candidate_evaluation);
    }

    result.algorithm_seconds = elapsed(algorithm_start);
    result.final_layout = points(values);
    if (result.final_evaluation.gradient_wh_per_m.empty()) {
        result.final_evaluation = model.evaluate(
            result.final_layout, true, executor);
        result.evaluator_seconds += result.final_evaluation.seconds;
        ++result.objective_gradient_calls;
    }
    result.objective_gain_percent = 100.0
        * (result.final_evaluation.aep_wh
           / result.initial_evaluation.aep_wh - 1.0);
    result.end_to_end_seconds = elapsed(total_start);
    result.scientific_hash = hash_result(result);
    if (result.final_layout.size() != kTurbines
        || !std::isfinite(result.final_evaluation.aep_wh)
        || result.final_evaluation.aep_wh
            < result.initial_evaluation.aep_wh) {
        throw std::runtime_error("L0649 final scientific validation failed");
    }
    return result;
}

}  // namespace core99::l0649
