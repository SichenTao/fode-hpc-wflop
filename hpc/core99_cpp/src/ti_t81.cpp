/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T81 pure-C++ inhomogeneous wave/AEP evaluator and
all-core Multistart-SLSQP
Paper DOI: 10.1016/j.apenergy.2021.117947
Public source: no target source or native wind, bathymetry, mesh, wave-model,
or wave-load arrays were located. FLORIS v2.4 supplies only cited NREL 5 MW
and legacy-Gauss lineage data.
Missing information, deterministic completions, semantic IDs, HPC design,
controlling contract, and claim boundary: include/core99/ti_t81.hpp
Claim boundary: academic flexible reconstruction, not author numerical replay.
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "core99/ti_t81.hpp"

#include "fode/executor.hpp"

#include <nlopt.h>

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <limits>
#include <numeric>
#include <numbers>
#include <random>
#include <stdexcept>
#include <utility>

namespace core99::t81 {
namespace {

using Clock = std::chrono::steady_clock;
constexpr double kRotorDiameterM = 126.0;
constexpr double kRotorRadiusM = 63.0;
constexpr double kHubHeightM = 90.0;
constexpr double kMonopileDiameterM = 6.0;
constexpr double kMinimumSpacingM = 2.0 * kRotorDiameterM;
constexpr double kAirDensity = 1.225;
constexpr double kWaterDensity = 1025.0;
constexpr double kGravity = 9.80665;
constexpr double kAnnualHours = 8760.0;
constexpr double kAmbientTurbulence = 0.06;
constexpr double kWakeGrowth = 0.38 * kAmbientTurbulence + 0.004;
constexpr double kFiniteDifferenceStepM = 0.5;

const std::array<double, 48> kSpeeds{
    2.0, 2.5, 3.0, 3.5, 4.0, 4.5, 5.0, 5.5,
    6.0, 6.5, 7.0, 7.5, 8.0, 8.5, 9.0, 9.5,
    10.0, 10.5, 11.0, 11.5, 12.0, 12.5, 13.0, 13.5,
    14.0, 14.5, 15.0, 15.5, 16.0, 16.5, 17.0, 17.5,
    18.0, 18.5, 19.0, 19.5, 20.0, 20.5, 21.0, 21.5,
    22.0, 22.5, 23.0, 23.5, 24.0, 24.5, 25.0, 25.5,
};

const std::array<double, 48> kPowerCoefficient{
    0.0, 0.0, 0.1780851, 0.28907459, 0.34902166, 0.3847278,
    0.40605878, 0.4202279, 0.42882274, 0.43387274, 0.43622267,
    0.43684468, 0.43657497, 0.43651053, 0.4365612, 0.43651728,
    0.43590309, 0.43467276, 0.43322955, 0.43003137, 0.37655587,
    0.33328466, 0.29700574, 0.26420779, 0.23839379, 0.21459275,
    0.19382354, 0.1756635, 0.15970926, 0.14561785, 0.13287856,
    0.12130194, 0.11219941, 0.10311631, 0.09545392, 0.08813781,
    0.08186763, 0.07585005, 0.07071926, 0.06557558, 0.06148104,
    0.05755207, 0.05413366, 0.05097969, 0.04806545, 0.04536883,
    0.04287006, 0.04055141,
};

const std::array<double, 48> kThrustCoefficient{
    1.19187945, 1.17284634, 1.09860817, 1.02889592, 0.97373036,
    0.92826162, 0.89210543, 0.86100905, 0.835423, 0.81237673,
    0.79225789, 0.77584769, 0.7629228, 0.76156073, 0.76261984,
    0.76169723, 0.75232027, 0.74026851, 0.72987175, 0.70701647,
    0.54054532, 0.45509459, 0.39343381, 0.34250785, 0.30487242,
    0.27164979, 0.24361964, 0.21973831, 0.19918151, 0.18131868,
    0.16537679, 0.15103727, 0.13998636, 0.1289037, 0.11970413,
    0.11087113, 0.10339901, 0.09617888, 0.09009926, 0.08395078,
    0.0791188, 0.07448356, 0.07050731, 0.06684119, 0.06345518,
    0.06032267, 0.05741999, 0.05472609,
};

double elapsed(const Clock::time_point& start) {
    return std::chrono::duration<double>(Clock::now() - start).count();
}

double interpolate(
    const double value,
    const std::array<double, 48>& values
) {
    if (value <= kSpeeds.front()) return values.front();
    if (value >= kSpeeds.back()) return values.back();
    const auto upper = std::upper_bound(
        kSpeeds.begin(), kSpeeds.end(), value
    );
    const std::size_t right = static_cast<std::size_t>(
        std::distance(kSpeeds.begin(), upper)
    );
    const std::size_t left = right - 1U;
    const double fraction = (value - kSpeeds[left])
        / (kSpeeds[right] - kSpeeds[left]);
    return values[left] + fraction * (values[right] - values[left]);
}

double turbine_power_mw(const double speed) {
    if (speed < 3.0 || speed > 25.0) return 0.0;
    const double cp = interpolate(speed, kPowerCoefficient);
    const double area = std::numbers::pi * kRotorRadiusM * kRotorRadiusM;
    return std::min(
        5.0,
        0.5 * kAirDensity * area * cp * speed * speed * speed / 1.0e6
    );
}

double thrust_coefficient(const double speed) {
    if (speed < 3.0 || speed > 25.0) return 0.0;
    return std::clamp(
        interpolate(speed, kThrustCoefficient), 0.0, 0.999
    );
}

double signed_cross(
    const Point& first,
    const Point& second,
    const Point& point
) {
    return (second.x_m - first.x_m) * (point.y_m - first.y_m)
        - (second.y_m - first.y_m) * (point.x_m - first.x_m);
}

bool inside_convex(
    const std::vector<Point>& polygon,
    const Point& point
) {
    for (std::size_t index = 0; index < polygon.size(); ++index) {
        if (signed_cross(
                polygon[index], polygon[(index + 1U) % polygon.size()], point
            ) < -1.0e-9) {
            return false;
        }
    }
    return true;
}

double wave_number(const double depth_m, const double period_s) {
    const double omega = 2.0 * std::numbers::pi / period_s;
    double value = omega * omega / kGravity;
    for (int iteration = 0; iteration < 40; ++iteration) {
        const double kd = value * depth_m;
        const double tanh_kd = std::tanh(kd);
        const double function = kGravity * value * tanh_kd
            - omega * omega;
        const double derivative = kGravity * (
            tanh_kd + kd / (std::cosh(kd) * std::cosh(kd))
        );
        value = std::max(1.0e-8, value - function / derivative);
    }
    return value;
}

double mild_slope_wave_load(const double x_m) {
    const double x = std::clamp(x_m, 0.0, 3000.0);
    const double depth = 5.0 + 35.0 * x / 3000.0;
    constexpr double period = 10.5;
    constexpr double deep_significant_height = 6.78;
    const double k = wave_number(depth, period);
    const double kd = k * depth;
    const double omega = 2.0 * std::numbers::pi / period;
    const double phase_speed = omega / k;
    const double group_factor = 0.5 * (
        1.0 + 2.0 * kd / std::sinh(2.0 * kd)
    );
    const double group_speed = phase_speed * group_factor;
    const double deep_group_speed = kGravity * period
        / (4.0 * std::numbers::pi);
    const double shoaling = std::sqrt(
        deep_group_speed / std::max(1.0e-9, group_speed)
    );
    const double predicted_significant = deep_significant_height * shoaling;
    const bool breaking = 1.86 * predicted_significant >= 0.78 * depth;
    const double significant_height = breaking
        ? std::min(predicted_significant, 0.78 * depth / 1.86)
        : predicted_significant;
    const double maximum_height = 1.86 * significant_height;
    double regular_moment = 0.0;
    constexpr int vertical_samples = 48;
    const double dz = depth / static_cast<double>(vertical_samples);
    for (int sample = 0; sample < vertical_samples; ++sample) {
        const double z = -depth
            + (static_cast<double>(sample) + 0.5) * dz;
        const double profile = std::cosh(k * (z + depth))
            / std::max(1.0e-9, std::sinh(kd));
        const double velocity = std::numbers::pi * maximum_height
            / period * profile;
        const double acceleration = 2.0 * std::numbers::pi
            * std::numbers::pi * maximum_height / (period * period)
            * profile;
        const double drag = 0.5 * 1.2 * kWaterDensity
            * kMonopileDiameterM * velocity * velocity;
        const double inertia = 2.0 * kWaterDensity
            * (std::numbers::pi * kMonopileDiameterM
                * kMonopileDiameterM / 4.0) * acceleration;
        regular_moment += (z + depth) * (drag + inertia) * dz;
    }
    double impact_moment = 0.0;
    if (breaking) {
        const double celerity = phase_speed;
        const double eta_b = 0.75 * maximum_height;
        const double envelope = std::exp(
            -0.5 * std::pow((depth - 12.5) / 2.8, 2.0)
        );
        impact_moment = 0.5 * eta_b * kWaterDensity
            * (0.5 * kMonopileDiameterM) * celerity * celerity
            * 2.0 * std::numbers::pi
            * (depth + 0.5 * eta_b) * envelope;
    }
    const double nonbreaking_minimum_completion = 1.0
        + 0.20 * std::pow((depth - 22.0) / 18.0, 2.0);
    return (regular_moment * nonbreaking_minimum_completion + impact_moment)
        / 1.0e6;
}

double gaussian2(
    const Point& point,
    const double x,
    const double y,
    const double sx,
    const double sy
) {
    const double dx = (point.x_m - x) / sx;
    const double dy = (point.y_m - y) / sy;
    return std::exp(-0.5 * (dx * dx + dy * dy));
}

double complex_wave_load(const Point& point) {
    const double broad = 0.20 * point.y_m / 6500.0
        + 0.12 * point.x_m / 6500.0;
    const double risky = 1.10 * gaussian2(point, 3200.0, 4200.0, 650.0, 900.0)
        + 0.85 * gaussian2(point, 4300.0, 3000.0, 550.0, 750.0)
        + 0.55 * gaussian2(point, 1600.0, 4700.0, 700.0, 650.0);
    const double lagoon = 0.65 * gaussian2(
        point, 3100.0, 1300.0, 1400.0, 700.0
    );
    return 20.0 + 55.0 * std::max(0.08, 0.55 + broad + risky - lagoon);
}

std::vector<Point> flatten_to_layout(const double* values, const int count) {
    std::vector<Point> result(static_cast<std::size_t>(count));
    for (int turbine = 0; turbine < count; ++turbine) {
        result[static_cast<std::size_t>(turbine)] = {
            values[static_cast<std::size_t>(2 * turbine)],
            values[static_cast<std::size_t>(2 * turbine + 1)],
        };
    }
    return result;
}

std::vector<double> layout_to_flatten(const std::vector<Point>& layout) {
    std::vector<double> result(2U * layout.size());
    for (std::size_t turbine = 0; turbine < layout.size(); ++turbine) {
        result[2U * turbine] = layout[turbine].x_m;
        result[2U * turbine + 1U] = layout[turbine].y_m;
    }
    return result;
}

struct EvaluationCache {
    const Problem* problem = nullptr;
    double baseline_aep_gwh = 1.0;
    double baseline_wave_load = 1.0;
    bool coupled = false;
    std::vector<double> lower_bounds;
    std::vector<double> upper_bounds;
    std::vector<double> point;
    Evaluation evaluation;
    std::vector<double> aep_gradient;
    std::vector<double> wave_gradient;
    bool has_value = false;
    bool has_gradient = false;
    std::uint64_t physical_fes = 0;
    double evaluator_seconds = 0.0;
    int objective_calls = 0;
    int gradient_calls = 0;
    int constraint_calls = 0;

    void evaluate_point(
        const unsigned variables,
        const double* values,
        const bool gradient
    ) {
        const bool same = has_value
            && point.size() == variables
            && std::equal(point.begin(), point.end(), values);
        if (same && (!gradient || has_gradient)) return;
        point.assign(values, values + variables);
        const auto started = Clock::now();
        evaluation = problem->evaluate(
            flatten_to_layout(values, problem->turbine_count())
        );
        ++physical_fes;
        has_value = true;
        has_gradient = false;
        if (gradient) {
            aep_gradient.assign(variables, 0.0);
            wave_gradient.assign(variables, 0.0);
            std::vector<double> perturbed = point;
            for (unsigned variable = 0; variable < variables; ++variable) {
                const double plus_step = std::min(
                    kFiniteDifferenceStepM,
                    std::max(0.0, upper_bounds[variable] - point[variable])
                );
                const double minus_step = std::min(
                    kFiniteDifferenceStepM,
                    std::max(0.0, point[variable] - lower_bounds[variable])
                );
                Evaluation plus = evaluation;
                Evaluation minus = evaluation;
                if (plus_step > 1.0e-12) {
                    perturbed[variable] = point[variable] + plus_step;
                    plus = problem->evaluate(flatten_to_layout(
                        perturbed.data(), problem->turbine_count()
                    ));
                    ++physical_fes;
                }
                if (minus_step > 1.0e-12) {
                    perturbed[variable] = point[variable] - minus_step;
                    minus = problem->evaluate(flatten_to_layout(
                        perturbed.data(), problem->turbine_count()
                    ));
                    ++physical_fes;
                }
                perturbed[variable] = point[variable];
                const double denominator = plus_step + minus_step;
                if (denominator <= 1.0e-12) continue;
                aep_gradient[variable] = (plus.aep_gwh - minus.aep_gwh)
                    / denominator;
                wave_gradient[variable] = (
                    plus.total_wave_load_index
                    - minus.total_wave_load_index
                ) / denominator;
            }
            has_gradient = true;
            ++gradient_calls;
        }
        evaluator_seconds += elapsed(started);
    }
};

double objective_callback(
    const unsigned variables,
    const double* values,
    double* gradient,
    void* opaque
) {
    auto& cache = *static_cast<EvaluationCache*>(opaque);
    ++cache.objective_calls;
    cache.evaluate_point(variables, values, gradient != nullptr);
    if (gradient != nullptr) {
        for (unsigned variable = 0; variable < variables; ++variable) {
            gradient[variable] = cache.coupled
                ? cache.wave_gradient[variable]
                : -cache.aep_gradient[variable];
        }
    }
    return cache.coupled
        ? cache.evaluation.total_wave_load_index
        : -cache.evaluation.aep_gwh;
}

struct ConstraintContext {
    EvaluationCache* cache = nullptr;
    const Problem* problem = nullptr;
    double alpha0 = 0.0;
};

void constraint_callback(
    const unsigned constraints,
    double* results,
    const unsigned variables,
    const double* values,
    double* gradients,
    void* opaque
) {
    auto& context = *static_cast<ConstraintContext*>(opaque);
    ++context.cache->constraint_calls;
    const auto layout = flatten_to_layout(
        values, context.problem->turbine_count()
    );
    unsigned row = 0;
    for (int left = 0; left < context.problem->turbine_count(); ++left) {
        for (int right = left + 1;
             right < context.problem->turbine_count();
             ++right) {
            const double dx = layout[static_cast<std::size_t>(left)].x_m
                - layout[static_cast<std::size_t>(right)].x_m;
            const double dy = layout[static_cast<std::size_t>(left)].y_m
                - layout[static_cast<std::size_t>(right)].y_m;
            results[row] = (kMinimumSpacingM * kMinimumSpacingM
                - dx * dx - dy * dy)
                / (kMinimumSpacingM * kMinimumSpacingM);
            if (gradients != nullptr) {
                double* gradient = gradients
                    + static_cast<std::size_t>(row) * variables;
                std::fill(gradient, gradient + variables, 0.0);
                const double scale = 2.0
                    / (kMinimumSpacingM * kMinimumSpacingM);
                gradient[static_cast<std::size_t>(2 * left)] = -scale * dx;
                gradient[static_cast<std::size_t>(2 * left + 1)] = -scale * dy;
                gradient[static_cast<std::size_t>(2 * right)] = scale * dx;
                gradient[static_cast<std::size_t>(2 * right + 1)] = scale * dy;
            }
            ++row;
        }
    }
    const auto& polygon = context.problem->boundary_polygon();
    if (!polygon.empty()) {
        for (std::size_t edge = 0; edge < polygon.size(); ++edge) {
            const Point& first = polygon[edge];
            const Point& second = polygon[(edge + 1U) % polygon.size()];
            const double ex = second.x_m - first.x_m;
            const double ey = second.y_m - first.y_m;
            const double edge_length = std::hypot(ex, ey);
            for (int turbine = 0;
                 turbine < context.problem->turbine_count();
                 ++turbine) {
                results[row] = -signed_cross(
                    first, second, layout[static_cast<std::size_t>(turbine)]
                ) / (edge_length * 3000.0);
                if (gradients != nullptr) {
                    double* gradient = gradients
                        + static_cast<std::size_t>(row) * variables;
                    std::fill(gradient, gradient + variables, 0.0);
                    gradient[static_cast<std::size_t>(2 * turbine)] =
                        ey / (edge_length * 3000.0);
                    gradient[static_cast<std::size_t>(2 * turbine + 1)] =
                        -ex / (edge_length * 3000.0);
                }
                ++row;
            }
        }
    }
    if (context.cache->coupled) {
        context.cache->evaluate_point(
            variables, values, gradients != nullptr
        );
        results[row] = context.alpha0
            - context.cache->evaluation.aep_gwh
                / context.cache->baseline_aep_gwh;
        if (gradients != nullptr) {
            double* gradient = gradients
                + static_cast<std::size_t>(row) * variables;
            for (unsigned variable = 0; variable < variables; ++variable) {
                gradient[variable] =
                    -context.cache->aep_gradient[variable]
                    / context.cache->baseline_aep_gwh;
            }
        }
        ++row;
    }
    if (row != constraints) {
        throw std::runtime_error("T81 constraint cardinality mismatch");
    }
}

struct SolverOutcome {
    std::vector<Point> layout;
    Evaluation evaluation;
    std::uint64_t physical_fes = 0;
    double evaluator_seconds = 0.0;
    int status = 0;
    double coupled_constraint_violation = 0.0;
};

SolverOutcome optimize_one(
    const Problem& problem,
    const std::vector<Point>& start,
    const bool coupled,
    const Evaluation& baseline,
    const double alpha0,
    const RunConfig& config
) {
    const auto start_evaluation_started = Clock::now();
    const Evaluation start_evaluation = problem.evaluate(start);
    const double start_evaluator_seconds = elapsed(start_evaluation_started);
    const unsigned variables = static_cast<unsigned>(2 * problem.turbine_count());
    std::vector<double> values = layout_to_flatten(start);
    std::vector<double> lower(variables, 0.0);
    std::vector<double> upper(variables, 0.0);
    if (problem.role() == CaseRole::mild_slope) {
        for (int turbine = 0; turbine < problem.turbine_count(); ++turbine) {
            lower[static_cast<std::size_t>(2 * turbine)] = 0.0;
            upper[static_cast<std::size_t>(2 * turbine)] = 3000.0;
            lower[static_cast<std::size_t>(2 * turbine + 1)] = -1500.0;
            upper[static_cast<std::size_t>(2 * turbine + 1)] = 1500.0;
        }
    } else {
        for (int turbine = 0; turbine < problem.turbine_count(); ++turbine) {
            lower[static_cast<std::size_t>(2 * turbine)] = 0.0;
            upper[static_cast<std::size_t>(2 * turbine)] = 6500.0;
            lower[static_cast<std::size_t>(2 * turbine + 1)] = 0.0;
            upper[static_cast<std::size_t>(2 * turbine + 1)] = 6500.0;
        }
    }
    EvaluationCache cache;
    cache.problem = &problem;
    cache.baseline_aep_gwh = coupled
        ? baseline.aep_gwh
        : static_cast<double>(problem.turbine_count()) * 5.0
            * kAnnualHours / 1000.0;
    cache.baseline_wave_load = coupled
        ? baseline.total_wave_load_index
        : 1.0;
    cache.coupled = coupled;
    cache.lower_bounds = lower;
    cache.upper_bounds = upper;
    ConstraintContext constraints;
    constraints.cache = &cache;
    constraints.problem = &problem;
    constraints.alpha0 = alpha0;
    const unsigned pair_constraints = static_cast<unsigned>(
        problem.turbine_count() * (problem.turbine_count() - 1) / 2
    );
    const unsigned boundary_constraints = static_cast<unsigned>(
        problem.boundary_polygon().size()
        * static_cast<std::size_t>(problem.turbine_count())
    );
    const unsigned total_constraints = pair_constraints
        + boundary_constraints + (coupled ? 1U : 0U);
    std::vector<double> tolerances(total_constraints, 1.0e-7);
    nlopt_opt optimizer = nlopt_create(NLOPT_LD_SLSQP, variables);
    if (optimizer == nullptr) {
        throw std::runtime_error("T81 failed to create NLopt SLSQP");
    }
    nlopt_result status = NLOPT_FAILURE;
    double optimum = 0.0;
    try {
        if (
            nlopt_set_lower_bounds(optimizer, lower.data()) < 0
            || nlopt_set_upper_bounds(optimizer, upper.data()) < 0
            || nlopt_set_min_objective(
                optimizer, objective_callback, &cache
            ) < 0
            || nlopt_add_inequality_mconstraint(
                optimizer,
                total_constraints,
                constraint_callback,
                &constraints,
                tolerances.data()
            ) < 0
            || nlopt_set_xtol_rel(
                optimizer, config.relative_x_tolerance
            ) < 0
            || nlopt_set_initial_step1(optimizer, 10.0) < 0
            || nlopt_set_maxeval(
                optimizer, config.maximum_evaluations_per_start
            ) < 0
        ) {
            throw std::runtime_error("T81 failed to configure SLSQP");
        }
        status = nlopt_optimize(optimizer, values.data(), &optimum);
    } catch (...) {
        nlopt_destroy(optimizer);
        throw;
    }
    nlopt_destroy(optimizer);
    SolverOutcome result;
    result.layout = flatten_to_layout(values.data(), problem.turbine_count());
    const auto final_started = Clock::now();
    result.evaluation = problem.evaluate(result.layout);
    cache.evaluator_seconds += elapsed(final_started);
    ++cache.physical_fes;
    result.physical_fes = cache.physical_fes + 1U;
    result.evaluator_seconds = cache.evaluator_seconds
        + start_evaluator_seconds;
    result.status = static_cast<int>(status);
    result.coupled_constraint_violation = coupled
        ? std::max(0.0, alpha0 - result.evaluation.aep_gwh / baseline.aep_gwh)
        : 0.0;
    const double start_constraint_violation = coupled
        ? std::max(0.0, alpha0 - start_evaluation.aep_gwh / baseline.aep_gwh)
        : 0.0;
    const bool keep_start = coupled
        ? (
            start_evaluation.feasible
            && start_constraint_violation <= 1.0e-6
            && (
                !result.evaluation.feasible
                || result.coupled_constraint_violation > 1.0e-6
                || start_evaluation.total_wave_load_index
                    < result.evaluation.total_wave_load_index
            )
        )
        : (
            start_evaluation.feasible
            && (
                !result.evaluation.feasible
                || start_evaluation.aep_gwh > result.evaluation.aep_gwh
            )
        );
    if (keep_start) {
        result.layout = start;
        result.evaluation = start_evaluation;
        result.coupled_constraint_violation = start_constraint_violation;
    }
    return result;
}

bool better_baseline(const SolverOutcome& left, const SolverOutcome& right) {
    const double left_violation = std::max({
        left.evaluation.spacing_violation_m,
        left.evaluation.boundary_violation_m,
    });
    const double right_violation = std::max({
        right.evaluation.spacing_violation_m,
        right.evaluation.boundary_violation_m,
    });
    if (left_violation != right_violation) {
        return left_violation < right_violation;
    }
    return left.evaluation.aep_gwh > right.evaluation.aep_gwh;
}

bool better_coupled(const SolverOutcome& left, const SolverOutcome& right) {
    const double left_violation = std::max({
        left.evaluation.spacing_violation_m / kMinimumSpacingM,
        left.evaluation.boundary_violation_m / 3000.0,
        left.coupled_constraint_violation,
    });
    const double right_violation = std::max({
        right.evaluation.spacing_violation_m / kMinimumSpacingM,
        right.evaluation.boundary_violation_m / 3000.0,
        right.coupled_constraint_violation,
    });
    if (left_violation != right_violation) {
        return left_violation < right_violation;
    }
    return left.evaluation.total_wave_load_index
        < right.evaluation.total_wave_load_index;
}

std::uint64_t hash_mix(std::uint64_t hash, const std::uint64_t value) {
    hash ^= value;
    hash *= 1099511628211ULL;
    return hash;
}

std::uint64_t scientific_hash(const RunResult& result) {
    std::uint64_t hash = 1469598103934665603ULL;
    hash = hash_mix(hash, result.physical_fes);
    for (const auto& stage : result.stages) {
        hash = hash_mix(hash, std::bit_cast<std::uint64_t>(stage.alpha0));
        hash = hash_mix(hash, stage.physical_fes);
        hash = hash_mix(
            hash,
            std::bit_cast<std::uint64_t>(stage.best_evaluation.aep_gwh)
        );
        hash = hash_mix(
            hash,
            std::bit_cast<std::uint64_t>(
                stage.best_evaluation.total_wave_load_index
            )
        );
        for (const auto& point : stage.best_layout) {
            hash = hash_mix(hash, std::bit_cast<std::uint64_t>(point.x_m));
            hash = hash_mix(hash, std::bit_cast<std::uint64_t>(point.y_m));
        }
    }
    return hash;
}

}  // namespace

Problem::Problem(const CaseRole role)
    : role_(role),
      id_(role == CaseRole::mild_slope
          ? "t81_case1_mild_slope"
          : "t81_case2_complex_terrain"),
      turbine_count_(role == CaseRole::mild_slope ? 16 : 20) {
    if (role == CaseRole::mild_slope) {
        for (int sector = 0; sector < 24; ++sector) {
            wind_states_.push_back({
                static_cast<double>(15 * sector), 8.0, 1.0 / 24.0,
            });
        }
    } else {
        const std::array<double, 24> speeds{
            8.2, 9.0, 9.7, 8.8, 7.3, 6.8, 6.5, 6.3,
            6.5, 7.0, 7.6, 8.3, 8.8, 8.7, 8.4, 8.0,
            7.8, 7.8, 8.0, 8.5, 9.0, 9.2, 9.0, 8.5,
        };
        const std::array<double, 24> raw_probability{
            0.040, 0.055, 0.070, 0.055, 0.040, 0.030,
            0.025, 0.028, 0.032, 0.038, 0.045, 0.055,
            0.050, 0.070, 0.080, 0.070, 0.065, 0.055,
            0.050, 0.048, 0.045, 0.043, 0.040, 0.035,
        };
        const double total = std::accumulate(
            raw_probability.begin(), raw_probability.end(), 0.0
        );
        for (int sector = 0; sector < 24; ++sector) {
            wind_states_.push_back({
                static_cast<double>(15 * sector),
                speeds[static_cast<std::size_t>(sector)],
                raw_probability[static_cast<std::size_t>(sector)] / total,
            });
        }
        boundary_polygon_ = {
            {0.0, 2000.0},
            {1500.0, 0.0},
            {5200.0, 500.0},
            {6500.0, 3500.0},
            {4300.0, 6500.0},
            {800.0, 5500.0},
        };
    }
    if (role_ == CaseRole::mild_slope) {
        wave_chart_step_x_m_ = 0.25;
        wave_chart_step_y_m_ = 1.0;
        wave_chart_columns_ = 12001;
        wave_chart_rows_ = 1;
        wave_chart_.resize(static_cast<std::size_t>(wave_chart_columns_));
        for (int column = 0; column < wave_chart_columns_; ++column) {
            wave_chart_[static_cast<std::size_t>(column)] =
                mild_slope_wave_load(
                    static_cast<double>(column) * wave_chart_step_x_m_
                );
        }
    } else {
        wave_chart_step_x_m_ = 25.0;
        wave_chart_step_y_m_ = 25.0;
        wave_chart_columns_ = 261;
        wave_chart_rows_ = 261;
        wave_chart_.resize(
            static_cast<std::size_t>(wave_chart_columns_ * wave_chart_rows_)
        );
        for (int row = 0; row < wave_chart_rows_; ++row) {
            for (int column = 0; column < wave_chart_columns_; ++column) {
                wave_chart_[static_cast<std::size_t>(
                    row * wave_chart_columns_ + column
                )] = complex_wave_load({
                    static_cast<double>(column) * wave_chart_step_x_m_,
                    static_cast<double>(row) * wave_chart_step_y_m_,
                });
            }
        }
    }
}

const std::string& Problem::id() const noexcept { return id_; }
CaseRole Problem::role() const noexcept { return role_; }
int Problem::turbine_count() const noexcept { return turbine_count_; }
const std::vector<WindState>& Problem::wind_states() const noexcept {
    return wind_states_;
}
const std::vector<Point>& Problem::boundary_polygon() const noexcept {
    return boundary_polygon_;
}

std::vector<Point> Problem::reference_layout() const {
    if (role_ == CaseRole::mild_slope) {
        std::vector<Point> result;
        for (int row = 0; row < 4; ++row) {
            for (int column = 0; column < 4; ++column) {
                result.push_back({
                    150.0 + static_cast<double>(column) * 900.0,
                    -1350.0 + static_cast<double>(row) * 900.0,
                });
            }
        }
        return result;
    }
    return reconstructed_start(0, 81001);
}

std::vector<Point> Problem::reconstructed_start(
    const int start_index,
    const std::uint64_t seed
) const {
    std::mt19937_64 generator(
        seed ^ (0x9e3779b97f4a7c15ULL
            * static_cast<std::uint64_t>(start_index + 1))
    );
    std::uniform_real_distribution<double> unit(0.0, 1.0);
    std::vector<Point> result;
    result.reserve(static_cast<std::size_t>(turbine_count_));
    int attempts = 0;
    while (static_cast<int>(result.size()) < turbine_count_) {
        if (++attempts > 3000000) {
            throw std::runtime_error("T81 failed to construct feasible start");
        }
        Point candidate;
        if (role_ == CaseRole::mild_slope) {
            candidate = {3000.0 * unit(generator), 3000.0 * unit(generator) - 1500.0};
        } else {
            candidate = {6500.0 * unit(generator), 6500.0 * unit(generator)};
            if (!inside_convex(boundary_polygon_, candidate)) continue;
        }
        bool accepted = true;
        for (const auto& existing : result) {
            if (std::hypot(
                    candidate.x_m - existing.x_m,
                    candidate.y_m - existing.y_m
                ) < kMinimumSpacingM) {
                accepted = false;
                break;
            }
        }
        if (accepted) result.push_back(candidate);
    }
    return result;
}

double Problem::wave_load_at(const Point& point) const {
    const double clipped_x = std::clamp(
        point.x_m,
        0.0,
        static_cast<double>(wave_chart_columns_ - 1) * wave_chart_step_x_m_
    );
    const double position_x = clipped_x / wave_chart_step_x_m_;
    const int left = std::min(
        static_cast<int>(position_x), wave_chart_columns_ - 1
    );
    const int right = std::min(left + 1, wave_chart_columns_ - 1);
    const double fraction_x = position_x - static_cast<double>(left);
    if (wave_chart_rows_ == 1) {
        const double low = wave_chart_[static_cast<std::size_t>(left)];
        const double high = wave_chart_[static_cast<std::size_t>(right)];
        return low + fraction_x * (high - low);
    }
    const double clipped_y = std::clamp(
        point.y_m,
        0.0,
        static_cast<double>(wave_chart_rows_ - 1) * wave_chart_step_y_m_
    );
    const double position_y = clipped_y / wave_chart_step_y_m_;
    const int bottom = std::min(
        static_cast<int>(position_y), wave_chart_rows_ - 1
    );
    const int top = std::min(bottom + 1, wave_chart_rows_ - 1);
    const double fraction_y = position_y - static_cast<double>(bottom);
    const auto at = [&](const int column, const int row) {
        return wave_chart_[static_cast<std::size_t>(
            row * wave_chart_columns_ + column
        )];
    };
    const double lower = at(left, bottom)
        + fraction_x * (at(right, bottom) - at(left, bottom));
    const double upper = at(left, top)
        + fraction_x * (at(right, top) - at(left, top));
    return lower + fraction_y * (upper - lower);
}

Evaluation Problem::evaluate(const std::vector<Point>& layout) const {
    if (layout.size() != static_cast<std::size_t>(turbine_count_)) {
        throw std::invalid_argument("T81 layout cardinality mismatch");
    }
    Evaluation result;
    result.minimum_spacing_m = std::numeric_limits<double>::infinity();
    if (role_ == CaseRole::mild_slope) {
        for (const auto& point : layout) {
            result.boundary_violation_m = std::max({
                result.boundary_violation_m,
                -point.x_m,
                point.x_m - 3000.0,
                -1500.0 - point.y_m,
                point.y_m - 1500.0,
            });
        }
    } else {
        for (const auto& point : layout) {
            for (std::size_t edge = 0;
                 edge < boundary_polygon_.size();
                 ++edge) {
                const Point& first = boundary_polygon_[edge];
                const Point& second = boundary_polygon_[
                    (edge + 1U) % boundary_polygon_.size()
                ];
                result.boundary_violation_m = std::max(
                    result.boundary_violation_m,
                    -signed_cross(first, second, point)
                        / std::hypot(
                            second.x_m - first.x_m,
                            second.y_m - first.y_m
                        )
                );
            }
        }
    }
    for (int left = 0; left < turbine_count_; ++left) {
        result.total_wave_load_index += wave_load_at(
            layout[static_cast<std::size_t>(left)]
        );
        for (int right = left + 1; right < turbine_count_; ++right) {
            result.minimum_spacing_m = std::min(
                result.minimum_spacing_m,
                std::hypot(
                    layout[static_cast<std::size_t>(left)].x_m
                        - layout[static_cast<std::size_t>(right)].x_m,
                    layout[static_cast<std::size_t>(left)].y_m
                        - layout[static_cast<std::size_t>(right)].y_m
                )
            );
        }
    }
    result.spacing_violation_m = std::max(
        0.0, kMinimumSpacingM - result.minimum_spacing_m
    );
    double expected_power_mw = 0.0;
    std::vector<int> order(static_cast<std::size_t>(turbine_count_));
    std::iota(order.begin(), order.end(), 0);
    for (const auto& state : wind_states_) {
        const double radians = state.direction_deg
            * std::numbers::pi / 180.0;
        const double flow_x = -std::sin(radians);
        const double flow_y = -std::cos(radians);
        std::stable_sort(order.begin(), order.end(), [&](const int left, const int right) {
            const auto projection = [&](const int index) {
                const auto& point = layout[static_cast<std::size_t>(index)];
                return point.x_m * flow_x + point.y_m * flow_y;
            };
            return projection(left) < projection(right);
        });
        std::vector<double> effective_speed(
            static_cast<std::size_t>(turbine_count_), state.speed_mps
        );
        for (int rank = 0; rank < turbine_count_; ++rank) {
            const int target = order[static_cast<std::size_t>(rank)];
            double squared = 0.0;
            for (int prior = 0; prior < rank; ++prior) {
                const int source = order[static_cast<std::size_t>(prior)];
                const double dx = layout[static_cast<std::size_t>(target)].x_m
                    - layout[static_cast<std::size_t>(source)].x_m;
                const double dy = layout[static_cast<std::size_t>(target)].y_m
                    - layout[static_cast<std::size_t>(source)].y_m;
                const double downstream = dx * flow_x + dy * flow_y;
                if (downstream <= 0.0) continue;
                const double lateral = -dx * flow_y + dy * flow_x;
                const double ct = thrust_coefficient(
                    effective_speed[static_cast<std::size_t>(source)]
                );
                if (ct <= 0.0) continue;
                const double beta = 0.5
                    * (1.0 + std::sqrt(1.0 - ct))
                    / std::sqrt(1.0 - ct);
                const double sigma = kWakeGrowth
                    * downstream / kRotorDiameterM + 0.2 * beta;
                const double amplitude = 1.0 - std::sqrt(std::max(
                    0.0, 1.0 - ct / (8.0 * sigma * sigma)
                ));
                const double deficit = amplitude * std::exp(
                    -0.5 * std::pow(
                        lateral / (kRotorDiameterM * sigma), 2.0
                    )
                );
                squared += deficit * deficit;
            }
            effective_speed[static_cast<std::size_t>(target)] =
                state.speed_mps * std::max(0.0, 1.0 - std::sqrt(squared));
            expected_power_mw += state.probability * turbine_power_mw(
                effective_speed[static_cast<std::size_t>(target)]
            );
        }
    }
    result.aep_gwh = expected_power_mw * kAnnualHours / 1000.0;
    result.feasible = result.spacing_violation_m <= 1.0e-3
        && result.boundary_violation_m <= 1.0e-3;
    return result;
}

std::vector<std::string> paper_case_ids() {
    return {"t81_case1_mild_slope", "t81_case2_complex_terrain"};
}

RunResult run(const Problem& problem, const RunConfig& config) {
    if (
        config.workers < 1 || config.multistarts < 1
        || config.maximum_evaluations_per_start < 1
        || config.relative_x_tolerance <= 0.0
        || config.alpha0_values.empty()
    ) {
        throw std::invalid_argument("T81 run configuration invalid");
    }
    const auto run_started = Clock::now();
    fode::PersistentExecutor executor(config.workers);
    executor.reset_work_receipt();
    RunResult result;
    result.case_id = problem.id();
    result.seed = config.seed;
    result.requested_workers = config.workers;

    auto execute_stage = [&](
        const std::string& stage_id,
        const bool coupled,
        const Evaluation& baseline,
        const std::vector<Point>& baseline_layout,
        const double alpha0
    ) {
        const auto stage_started = Clock::now();
        std::vector<SolverOutcome> outcomes(
            static_cast<std::size_t>(config.multistarts)
        );
        executor.parallel_for(0, config.multistarts, [&](const int start) {
            const auto initial = start == 0
                ? (coupled ? baseline_layout : problem.reference_layout())
                : problem.reconstructed_start(start, config.seed);
            outcomes[static_cast<std::size_t>(start)] = optimize_one(
                problem, initial, coupled, baseline, alpha0, config
            );
        });
        int best = 0;
        for (int start = 1; start < config.multistarts; ++start) {
            const bool is_better = coupled
                ? better_coupled(
                    outcomes[static_cast<std::size_t>(start)],
                    outcomes[static_cast<std::size_t>(best)]
                )
                : better_baseline(
                    outcomes[static_cast<std::size_t>(start)],
                    outcomes[static_cast<std::size_t>(best)]
                );
            if (is_better) best = start;
        }
        StageResult stage;
        stage.stage_id = stage_id;
        stage.alpha0 = alpha0;
        stage.multistarts = config.multistarts;
        stage.successful_starts = static_cast<int>(std::count_if(
            outcomes.begin(), outcomes.end(), [&](const SolverOutcome& item) {
                return item.evaluation.feasible
                    && (!coupled || item.coupled_constraint_violation <= 1.0e-6);
            }
        ));
        stage.physical_fes = std::accumulate(
            outcomes.begin(), outcomes.end(), std::uint64_t{0},
            [](const std::uint64_t sum, const SolverOutcome& item) {
                return sum + item.physical_fes;
            }
        );
        stage.seconds = elapsed(stage_started);
        stage.best_layout = outcomes[static_cast<std::size_t>(best)].layout;
        stage.best_evaluation = outcomes[
            static_cast<std::size_t>(best)
        ].evaluation;
        stage.beta = coupled
            ? stage.best_evaluation.total_wave_load_index
                / baseline.total_wave_load_index
            : 1.0;
        stage.alpha1 = coupled
            ? stage.best_evaluation.aep_gwh / baseline.aep_gwh
            : 1.0;
        result.physical_fes += stage.physical_fes;
        const double evaluator_sum = std::accumulate(
            outcomes.begin(), outcomes.end(), 0.0,
            [](const double sum, const SolverOutcome& item) {
                return sum + item.evaluator_seconds;
            }
        );
        const double evaluator_max = std::max_element(
            outcomes.begin(), outcomes.end(),
            [](const SolverOutcome& left, const SolverOutcome& right) {
                return left.evaluator_seconds < right.evaluator_seconds;
            }
        )->evaluator_seconds;
        const int active_lanes = std::min(
            config.workers, config.multistarts
        );
        result.evaluator_seconds += std::max(
            evaluator_max,
            evaluator_sum / static_cast<double>(active_lanes)
        );
        result.stages.push_back(std::move(stage));
    };

    const Evaluation empty_baseline{};
    execute_stage(
        "t81_stage1_maximum_aep",
        false,
        empty_baseline,
        {},
        1.0
    );
    const Evaluation baseline = result.stages.front().best_evaluation;
    const std::vector<Point> baseline_layout =
        result.stages.front().best_layout;
    std::vector<Point> coupled_incumbent = baseline_layout;
    for (const double alpha0 : config.alpha0_values) {
        execute_stage(
            "t81_stage2_wave_alpha_" + std::to_string(alpha0),
            true,
            baseline,
            coupled_incumbent,
            alpha0
        );
        coupled_incumbent = result.stages.back().best_layout;
    }
    result.observed_workers = executor.work_receipt().distinct_participants;
    result.end_to_end_seconds = elapsed(run_started);
    result.algorithm_seconds = std::max(
        0.0, result.end_to_end_seconds - result.evaluator_seconds
    );
    result.scientific_hash = scientific_hash(result);
    return result;
}

}  // namespace core99::t81
