/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T48 equations, quadrature, and full-resource search
Full source facts, missing fields, reconstruction, semantic IDs, and claim:
include/core99/lackner_t48.hpp
Independent oracle: scripts/validate_core99_t48.py
HPC design: flatten candidate x turbine x direction work onto one persistent
CPU team; fixed-order double reductions and candidate commits preserve output
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "core99/lackner_t48.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <utility>

namespace core99::t48 {
namespace {

using Clock = std::chrono::steady_clock;

constexpr int kDirectionCount = 360;
constexpr double kDirectionStepDegrees = 1.0;
constexpr double kSpeedStep = 0.25;
constexpr int kSpeedCount = 160;
constexpr double kRatedPowerKw = 1500.0;
constexpr double kRotorDiameterM = 77.0;
constexpr double kRotorRadiusM = 38.5;
constexpr double kThrustCoefficient = 0.80;
constexpr double kWakeSpreading = 0.04;
constexpr double kFixedChargeRate = 0.10;
constexpr double kOperationMaintenanceFraction = 0.02;
constexpr double kPowerCurveCalibration = 0.89;

constexpr std::array<double, 16> kScaleFactor{
    10.5, 10.5, 10.5, 8.5, 8.0, 7.2, 5.8, 7.8,
    8.0, 9.2, 9.0, 8.5, 10.1, 11.6, 12.0, 10.0
};
constexpr std::array<double, 16> kShapeFactor{
    2.0, 1.65, 1.70, 1.65, 2.05, 1.75, 1.70, 1.90,
    1.80, 2.15, 2.40, 2.40, 2.15, 2.80, 2.75, 2.60
};
constexpr std::array<double, 16> kDirectionFrequency{
    0.050, 0.050, 0.050, 0.035,
    0.065, 0.040, 0.020, 0.025,
    0.035, 0.085, 0.100, 0.090,
    0.085, 0.075, 0.065, 0.065
};

constexpr std::array<double, 14> kPowerSpeed{
    0.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0,
    9.0, 10.0, 11.0, 12.0, 25.0, 25.01, 40.0
};
constexpr std::array<double, 14> kPowerKw{
    0.0, 0.0, 70.0, 150.0, 300.0, 500.0, 730.0,
    1000.0, 1250.0, 1450.0, 1500.0, 1500.0, 0.0, 0.0
};

double periodic_pchip(
    const std::array<double, 16>& values,
    double degrees
) {
    degrees = std::fmod(degrees, 360.0);
    if (degrees < 0.0) {
        degrees += 360.0;
    }
    constexpr double spacing = 22.5;
    const int left = static_cast<int>(std::floor(degrees / spacing)) % 16;
    const int right = (left + 1) % 16;
    const int previous = (left + 15) % 16;
    const int next = (right + 1) % 16;
    const double delta_previous = (
        values[static_cast<std::size_t>(left)]
        - values[static_cast<std::size_t>(previous)]
    ) / spacing;
    const double delta = (
        values[static_cast<std::size_t>(right)]
        - values[static_cast<std::size_t>(left)]
    ) / spacing;
    const double delta_next = (
        values[static_cast<std::size_t>(next)]
        - values[static_cast<std::size_t>(right)]
    ) / spacing;
    auto derivative = [](const double first, const double second) {
        if (first * second <= 0.0) {
            return 0.0;
        }
        return 2.0 * first * second / (first + second);
    };
    const double left_slope = derivative(delta_previous, delta);
    const double right_slope = derivative(delta, delta_next);
    const double t = (degrees - spacing * static_cast<double>(left)) / spacing;
    const double t2 = t * t;
    const double t3 = t2 * t;
    return (2.0 * t3 - 3.0 * t2 + 1.0)
            * values[static_cast<std::size_t>(left)]
        + (t3 - 2.0 * t2 + t) * spacing * left_slope
        + (-2.0 * t3 + 3.0 * t2)
            * values[static_cast<std::size_t>(right)]
        + (t3 - t2) * spacing * right_slope;
}

double normalized_direction_weight(const double degrees) {
    static const double normalization = [] {
        double sum = 0.0;
        for (int direction = 0; direction < kDirectionCount; ++direction) {
            sum += std::max(
                0.0,
                periodic_pchip(
                    kDirectionFrequency,
                    (static_cast<double>(direction) + 0.5)
                    * kDirectionStepDegrees
                )
            );
        }
        return sum;
    }();
    return std::max(
        0.0,
        periodic_pchip(kDirectionFrequency, degrees)
    ) / normalization;
}

double power_curve(const double speed) {
    for (std::size_t index = 1; index < kPowerSpeed.size(); ++index) {
        if (speed <= kPowerSpeed[index]) {
            const double fraction = (
                speed - kPowerSpeed[index - 1]
            ) / (
                kPowerSpeed[index] - kPowerSpeed[index - 1]
            );
            return kPowerCurveCalibration * (
                kPowerKw[index - 1]
                + fraction * (kPowerKw[index] - kPowerKw[index - 1])
            );
        }
    }
    return 0.0;
}

double mean_power_kw(const double scale, const double shape) {
    if (!(scale > 0.0) || !(shape > 0.0)) {
        return 0.0;
    }
    double result = 0.0;
    for (int bin = 0; bin < kSpeedCount; ++bin) {
        const double speed =
            (static_cast<double>(bin) + 0.5) * kSpeedStep;
        const double ratio = speed / scale;
        const double density =
            (shape / scale)
            * std::pow(ratio, shape - 1.0)
            * std::exp(-std::pow(ratio, shape));
        result += power_curve(speed) * density * kSpeedStep;
    }
    return result;
}

double direction_turbine_power(
    const std::vector<double>& variables,
    const int turbine,
    const int direction,
    const bool ignore_wake = false
) {
    const double degrees =
        (static_cast<double>(direction) + 0.5) * kDirectionStepDegrees;
    const double radians = degrees * std::numbers::pi / 180.0;
    const double travel_x = std::sin(radians + std::numbers::pi);
    const double travel_y = std::cos(radians + std::numbers::pi);
    const double cross_x = -travel_y;
    const double cross_y = travel_x;
    const double x = variables[static_cast<std::size_t>(2 * turbine)];
    const double y = variables[static_cast<std::size_t>(2 * turbine + 1)];
    double deficit_squared = 0.0;
    for (int upstream = 0; !ignore_wake && upstream < 2; ++upstream) {
        if (upstream == turbine) {
            continue;
        }
        const double dx =
            x - variables[static_cast<std::size_t>(2 * upstream)];
        const double dy =
            y - variables[static_cast<std::size_t>(2 * upstream + 1)];
        const double axial = dx * travel_x + dy * travel_y;
        if (!(axial > 0.0)) {
            continue;
        }
        const double radial = std::abs(dx * cross_x + dy * cross_y);
        if (radial > kRotorRadiusM + kWakeSpreading * axial) {
            continue;
        }
        const double deficit = (
            1.0 - std::sqrt(1.0 - kThrustCoefficient)
        ) / std::pow(
            1.0 + 2.0 * kWakeSpreading * axial / kRotorDiameterM,
            2.0
        );
        deficit_squared += deficit * deficit;
    }
    const double shore_km = x / 1000.0;
    const double shore_scale =
        -0.25 * std::exp(-shore_km / 23.0) + 1.17;
    const double scale = periodic_pchip(kScaleFactor, degrees)
        * shore_scale
        * std::max(0.0, 1.0 - std::sqrt(deficit_squared));
    const double shape = std::max(
        0.1,
        periodic_pchip(kShapeFactor, degrees)
    );
    return normalized_direction_weight(degrees)
        * mean_power_kw(scale, shape);
}

double capital_cost(const std::vector<double>& variables) {
    const double x1 = variables[0];
    const double y1 = variables[1];
    const double x2 = variables[2];
    const double y2 = variables[3];
    const double inter = std::hypot(x1 - x2, y1 - y2);
    const double connection = std::min(
        std::hypot(x1, y1),
        std::hypot(x2, y2)
    );
    const double cable_m = connection + inter;
    return 2.0 * 700000.0
        + 2.0 * 600000.0
        + 100.0 * (x1 + x2)
        + 620.0 * cable_m;
}

double violation(const std::vector<double>& variables) {
    if (variables.size() != 4U) {
        return std::numeric_limits<double>::infinity();
    }
    double result = 0.0;
    for (const int turbine : {0, 1}) {
        const double x = variables[static_cast<std::size_t>(2 * turbine)];
        const double y =
            variables[static_cast<std::size_t>(2 * turbine + 1)];
        result += std::max({0.0, 1600.0 - x, x - 10000.0});
        result += std::max({0.0, -5000.0 - y, y - 5000.0});
    }
    const double separation = std::hypot(
        variables[0] - variables[2],
        variables[1] - variables[3]
    );
    if (separation <= 1.0e-9) {
        result += 1.0;
    }
    return result;
}

Evaluation finish_evaluation(
    const std::vector<double>& variables,
    const double average_power_kw
) {
    Evaluation result;
    result.constraint_violation = violation(variables);
    result.capital_cost_dollars = capital_cost(variables);
    result.annual_energy_kwh = 8760.0 * average_power_kw;
    result.capacity_factor = average_power_kw / (2.0 * kRatedPowerKw);
    result.lcoe_dollars_per_kwh = (
        result.capital_cost_dollars
        * (kFixedChargeRate + kOperationMaintenanceFraction)
    ) / result.annual_energy_kwh;

    double isolated_power = 0.0;
    for (int turbine = 0; turbine < 2; ++turbine) {
        for (int direction = 0; direction < kDirectionCount; ++direction) {
            isolated_power += direction_turbine_power(
                variables,
                turbine,
                direction,
                true
            );
        }
    }
    result.wake_loss_fraction = isolated_power > 0.0
        ? 1.0 - average_power_kw / isolated_power
        : 0.0;
    return result;
}

bool better(
    const Evaluation& left,
    const std::vector<double>& left_variables,
    const Evaluation& right,
    const std::vector<double>& right_variables
) {
    const bool left_feasible = left.constraint_violation <= 1.0e-12;
    const bool right_feasible = right.constraint_violation <= 1.0e-12;
    if (left_feasible != right_feasible) {
        return left_feasible;
    }
    if (left.constraint_violation != right.constraint_violation) {
        return left.constraint_violation < right.constraint_violation;
    }
    if (left.lcoe_dollars_per_kwh != right.lcoe_dollars_per_kwh) {
        return left.lcoe_dollars_per_kwh < right.lcoe_dollars_per_kwh;
    }
    return left_variables < right_variables;
}

std::uint64_t scientific_hash(
    const std::vector<double>& variables,
    const Evaluation& value
) {
    std::uint64_t hash = 1469598103934665603ULL;
    for (const double variable : variables) {
        hash ^= std::bit_cast<std::uint64_t>(variable);
        hash *= 1099511628211ULL;
    }
    hash ^= std::bit_cast<std::uint64_t>(value.lcoe_dollars_per_kwh);
    hash *= 1099511628211ULL;
    return hash;
}

}  // namespace

int Problem::dimension() const noexcept {
    return 4;
}

std::vector<double> Problem::lower_bounds() const {
    return {1600.0, -5000.0, 1600.0, -5000.0};
}

std::vector<double> Problem::upper_bounds() const {
    return {10000.0, 5000.0, 10000.0, 5000.0};
}

Evaluation Problem::evaluate(
    const std::vector<double>& variables
) const {
    if (variables.size() != 4U) {
        throw std::invalid_argument("T48 requires four variables");
    }
    double average_power_kw = 0.0;
    for (int turbine = 0; turbine < 2; ++turbine) {
        for (int direction = 0; direction < kDirectionCount; ++direction) {
            average_power_kw += direction_turbine_power(
                variables,
                turbine,
                direction
            );
        }
    }
    return finish_evaluation(variables, average_power_kw);
}

std::vector<Evaluation> Problem::evaluate_population(
    const std::vector<std::vector<double>>& population,
    fode::PersistentExecutor& executor
) const {
    for (const auto& variables : population) {
        if (variables.size() != 4U) {
            throw std::invalid_argument("T48 population dimension mismatch");
        }
    }
    const int tasks = static_cast<int>(
        population.size() * 2U * static_cast<std::size_t>(kDirectionCount)
    );
    std::vector<double> partial(static_cast<std::size_t>(tasks), 0.0);
    executor.parallel_for(0, tasks, [&](const int raw) {
        const int direction = raw % kDirectionCount;
        const int turbine = (raw / kDirectionCount) % 2;
        const int candidate = raw / (2 * kDirectionCount);
        partial[static_cast<std::size_t>(raw)] = direction_turbine_power(
            population[static_cast<std::size_t>(candidate)],
            turbine,
            direction
        );
    });
    std::vector<Evaluation> result(population.size());
    for (std::size_t candidate = 0; candidate < population.size(); ++candidate) {
        double average_power_kw = 0.0;
        const std::size_t begin =
            candidate * 2U * static_cast<std::size_t>(kDirectionCount);
        const std::size_t end =
            begin + 2U * static_cast<std::size_t>(kDirectionCount);
        for (std::size_t index = begin; index < end; ++index) {
            average_power_kw += partial[index];
        }
        result[candidate] = finish_evaluation(
            population[candidate],
            average_power_kw
        );
    }
    return result;
}

std::vector<double> paper_initial_layout() {
    return {9109.0, -2013.0, 3273.0, 1614.0};
}

std::vector<double> paper_reported_final_layout() {
    return {1794.0, -320.0, 1613.0, -115.0};
}

RunResult run(
    const Problem& problem,
    const std::uint64_t seed,
    const int iterations,
    const int workers
) {
    if (iterations <= 0) {
        throw std::invalid_argument("T48 iterations must be positive");
    }
    const auto start = Clock::now();
    fode::PersistentExecutor executor(workers);
    executor.reset_work_receipt();
    std::vector<double> current = paper_initial_layout();
    const auto initial_values = problem.evaluate_population({current}, executor);
    Evaluation current_value = initial_values[0];
    std::uint64_t physical_fes = 1;
    double evaluator_seconds = 0.0;
    double algorithm_seconds = 0.0;
    std::array<double, 4> step{300.0, 300.0, 300.0, 300.0};
    const auto lower = problem.lower_bounds();
    const auto upper = problem.upper_bounds();

    for (int iteration = 0; iteration < iterations; ++iteration) {
        const auto algorithm_start = Clock::now();
        std::vector<std::vector<double>> trials;
        trials.reserve(8);
        for (int coordinate = 0; coordinate < 4; ++coordinate) {
            for (const double sign : {-1.0, 1.0}) {
                auto trial = current;
                trial[static_cast<std::size_t>(coordinate)] = std::clamp(
                    trial[static_cast<std::size_t>(coordinate)]
                        + sign * step[static_cast<std::size_t>(coordinate)],
                    lower[static_cast<std::size_t>(coordinate)],
                    upper[static_cast<std::size_t>(coordinate)]
                );
                trials.push_back(std::move(trial));
            }
        }
        const auto algorithm_done = Clock::now();
        const auto values = problem.evaluate_population(trials, executor);
        const auto evaluation_done = Clock::now();
        physical_fes += trials.size();

        std::size_t best = 0;
        for (std::size_t candidate = 1; candidate < trials.size(); ++candidate) {
            if (better(
                values[candidate],
                trials[candidate],
                values[best],
                trials[best]
            )) {
                best = candidate;
            }
        }
        if (better(values[best], trials[best], current_value, current)) {
            current = trials[best];
            current_value = values[best];
        } else {
            for (double& value : step) {
                value = std::max(0.5, value * 0.5);
            }
        }
        const auto commit_done = Clock::now();
        algorithm_seconds +=
            std::chrono::duration<double>(algorithm_done - algorithm_start)
                .count()
            + std::chrono::duration<double>(commit_done - evaluation_done)
                .count();
        evaluator_seconds += std::chrono::duration<double>(
            evaluation_done - algorithm_done
        ).count();
    }
    const auto end = Clock::now();
    RunResult result;
    result.initial_variables = paper_initial_layout();
    result.initial_evaluation = initial_values[0];
    result.best_variables = current;
    result.best_evaluation = current_value;
    result.seed = seed;
    result.physical_fes = physical_fes;
    result.iterations = iterations;
    result.requested_workers = workers;
    result.observed_workers =
        executor.work_receipt().distinct_participants;
    result.evaluator_seconds = evaluator_seconds;
    result.algorithm_seconds = algorithm_seconds;
    result.end_to_end_seconds =
        std::chrono::duration<double>(end - start).count();
    result.scientific_hash = scientific_hash(
        result.best_variables,
        result.best_evaluation
    );
    return result;
}

}  // namespace core99::t48
