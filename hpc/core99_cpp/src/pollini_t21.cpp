/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T21 pure-C++ density evaluator, gradients, and MMA driver
Paper/DOI: Topology Optimization of Wind Farm Layouts;
10.1016/j.renene.2022.06.019
Public source:
https://github.com/byuflowlab/iea37-wflo-casestudies revision
af88908d22795030ac2dfbe37bc38e912aee8ed6
Missing/conflicts/completion:
hpc/core99_cpp/include/core99/pollini_t21.hpp
Independent oracle: scripts/validate_core99_t21.py
HPC design: immutable direction-pair wake deficits are precomputed once;
each objective-and-gradient call is partitioned into balanced target blocks
across every configured worker and reduced deterministically
Method/problem semantic IDs: t21_ramp_mma_declared_reconstruction_v1;
t21_pollini_two_circle_density_wflop_v1
Controlling contract: shared/contracts/core99_t21_pollini_2022.json
Claim boundary: academic declared reconstruction, not author MATLAB/MMA replay
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "core99/pollini_t21.hpp"

#include "fode/rng.hpp"

#include <nlopt.h>

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <limits>
#include <numbers>
#include <numeric>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace core99::t21 {
namespace {

using Clock = std::chrono::steady_clock;

constexpr std::array<double, 16> kDirectionsDeg = {
    0.0, 22.5, 45.0, 67.5, 90.0, 112.5, 135.0, 157.5,
    180.0, 202.5, 225.0, 247.5, 270.0, 292.5, 315.0, 337.5,
};
constexpr std::array<double, 16> kFrequencies = {
    0.025, 0.024, 0.029, 0.036, 0.063, 0.065, 0.100, 0.122,
    0.063, 0.038, 0.039, 0.083, 0.213, 0.046, 0.032, 0.022,
};
constexpr double kWindSpeed = 9.8;
constexpr double kDiameter = 130.0;
constexpr double kMinimumSpacing = 260.0;
constexpr double kRatedPowerW = 3370000.0;
constexpr double kCutIn = 4.0;
constexpr double kRatedSpeed = 9.8;
constexpr double kCutOut = 25.0;
constexpr double kThrust = 8.0 / 9.0;
constexpr double kTurbulence = 0.075;
constexpr double kWakeExpansion = 0.3837 * kTurbulence + 0.003678;
constexpr double kRampMaximum = 10.0;
constexpr double kPaperMovingLimit = 0.1;
constexpr double kDensityTolerance = 1.0e-8;
constexpr double kWakeGradientFloor = 1.0e-12;

double seconds_since(const Clock::time_point start) {
    return std::chrono::duration<double>(Clock::now() - start).count();
}

std::vector<Point> make_grid(CaseId id) {
    const double radius =
        id == CaseId::radius_1300 ? 1300.0 : 3000.0;
    const int start =
        id == CaseId::radius_1300 ? -1100 : -3000;
    const int finish =
        id == CaseId::radius_1300 ? 1100 : 3000;
    std::vector<Point> result;
    for (int x = start; x <= finish; x += 200) {
        for (int y = start; y <= finish; y += 200) {
            if (
                static_cast<double>(x) * x
                    + static_cast<double>(y) * y
                <= radius * radius + 1.0e-9
            ) {
                result.push_back(
                    {static_cast<double>(x), static_cast<double>(y)}
                );
            }
        }
    }
    const std::size_t expected =
        id == CaseId::radius_1300 ? 124U : 709U;
    if (result.size() != expected) {
        throw std::runtime_error("T21 paper-grid cardinality mismatch");
    }
    return result;
}

std::vector<std::pair<int, int>> make_spacing_pairs(
    const std::vector<Point>& grid
) {
    std::vector<std::pair<int, int>> result;
    const double threshold =
        kMinimumSpacing * kMinimumSpacing + 1.0e-9;
    for (std::size_t left = 0; left < grid.size(); ++left) {
        for (
            std::size_t right = left + 1;
            right < grid.size();
            ++right
        ) {
            const double dx = grid[left].x_m - grid[right].x_m;
            const double dy = grid[left].y_m - grid[right].y_m;
            if (dx * dx + dy * dy <= threshold) {
                result.emplace_back(
                    static_cast<int>(left),
                    static_cast<int>(right)
                );
            }
        }
    }
    return result;
}

double near_wake_discontinuity() {
    const double root = std::sqrt(1.0 - kThrust);
    const double x0 = kDiameter * (1.0 + root)
        / (
            std::sqrt(2.0)
            * (
                2.32 * kTurbulence
                + 0.154 * (1.0 - root)
            )
        );
    const double a = 2.0 * kWakeExpansion;
    const double b =
        4.0 * kWakeExpansion * kWakeExpansion * (kThrust - 1.0);
    const double c =
        2.0 * std::sqrt(8.0) * kWakeExpansion * kWakeExpansion;
    return x0
        + kDiameter * (a - std::sqrt(std::max(0.0, a * a - b))) / c;
}

double pair_deficit_squared(
    const Point& upstream,
    const Point& downstream,
    double direction_deg
) {
    const double angle =
        (270.0 - direction_deg) * std::numbers::pi / 180.0;
    const double cosine = std::cos(angle);
    const double sine = std::sin(angle);
    const double upstream_along =
        cosine * upstream.x_m + sine * upstream.y_m;
    const double downstream_along =
        cosine * downstream.x_m + sine * downstream.y_m;
    const double distance = downstream_along - upstream_along;
    if (distance <= 0.0) {
        return 0.0;
    }
    const double upstream_across =
        -sine * upstream.x_m + cosine * upstream.y_m;
    const double downstream_across =
        -sine * downstream.x_m + cosine * downstream.y_m;
    const double root = std::sqrt(1.0 - kThrust);
    const double x0 = kDiameter * (1.0 + root)
        / (
            std::sqrt(2.0)
            * (
                2.32 * kTurbulence
                + 0.154 * (1.0 - root)
            )
        );
    const double model_distance =
        std::max(distance, near_wake_discontinuity());
    const double sigma =
        kWakeExpansion * (model_distance - x0)
        + kDiameter / std::sqrt(8.0);
    const double radical = std::clamp(
        1.0
            - kThrust
                / (8.0 * sigma * sigma / (kDiameter * kDiameter)),
        0.0,
        1.0
    );
    const double dy = downstream_across - upstream_across;
    const double deficit = (1.0 - std::sqrt(radical))
        * std::exp(-0.5 * dy * dy / (sigma * sigma));
    return deficit * deficit;
}

double turbine_power(double speed) {
    if (speed < kCutIn || speed >= kCutOut) {
        return 0.0;
    }
    if (speed < kRatedSpeed) {
        const double fraction =
            (speed - kCutIn) / (kRatedSpeed - kCutIn);
        return kRatedPowerW * fraction * fraction * fraction;
    }
    return kRatedPowerW;
}

double turbine_power_derivative(double speed) {
    if (speed < kCutIn || speed >= kRatedSpeed) {
        return 0.0;
    }
    const double scale = kRatedSpeed - kCutIn;
    const double fraction = (speed - kCutIn) / scale;
    return 3.0 * kRatedPowerW * fraction * fraction / scale;
}

std::string nlopt_status_name(nlopt_result status) {
    switch (status) {
        case NLOPT_FAILURE: return "failure";
        case NLOPT_INVALID_ARGS: return "invalid_args";
        case NLOPT_OUT_OF_MEMORY: return "out_of_memory";
        case NLOPT_ROUNDOFF_LIMITED: return "roundoff_limited";
        case NLOPT_FORCED_STOP: return "forced_stop";
        case NLOPT_SUCCESS: return "success";
        case NLOPT_STOPVAL_REACHED: return "stopval_reached";
        case NLOPT_FTOL_REACHED: return "ftol_reached";
        case NLOPT_XTOL_REACHED: return "xtol_reached";
        case NLOPT_MAXEVAL_REACHED: return "maxeval_reached";
        case NLOPT_MAXTIME_REACHED: return "maxtime_reached";
        default: return "unknown";
    }
}

struct ObjectiveContext {
    const Problem* problem = nullptr;
    fode::PersistentExecutor* executor = nullptr;
    double ramp_penalty = 0.0;
    int objective_calls = 0;
    int gradient_calls = 0;
    double evaluator_seconds = 0.0;
};

double objective_callback(
    unsigned variables,
    const double* values,
    double* gradient,
    void* raw_context
) {
    auto& context = *static_cast<ObjectiveContext*>(raw_context);
    const std::vector<double> densities(values, values + variables);
    const auto started = Clock::now();
    const Evaluation evaluation = context.problem->evaluate(
        densities,
        context.ramp_penalty,
        *context.executor,
        gradient != nullptr
    );
    context.evaluator_seconds += seconds_since(started);
    ++context.objective_calls;
    if (gradient != nullptr) {
        ++context.gradient_calls;
        std::copy(
            evaluation.objective_gradient.begin(),
            evaluation.objective_gradient.end(),
            gradient
        );
    }
    return evaluation.objective;
}

struct ConstraintContext {
    const Problem* problem = nullptr;
    int calls = 0;
};

void constraint_callback(
    unsigned constraints,
    double* result,
    unsigned variables,
    const double* values,
    double* gradient,
    void* raw_context
) {
    auto& context = *static_cast<ConstraintContext*>(raw_context);
    const auto& pairs = context.problem->spacing_pairs();
    if (constraints != pairs.size() + 2U) {
        std::fill(
            result,
            result + constraints,
            std::numeric_limits<double>::infinity()
        );
        return;
    }
    const double count =
        std::accumulate(values, values + variables, 0.0);
    result[0] =
        static_cast<double>(context.problem->minimum_turbines())
            / variables
        - count / variables;
    result[1] =
        count / variables
        - static_cast<double>(context.problem->maximum_turbines())
            / variables;
    for (std::size_t index = 0; index < pairs.size(); ++index) {
        const auto [left, right] = pairs[index];
        result[index + 2] = values[left] + values[right] - 1.0;
    }
    if (gradient != nullptr) {
        std::fill(
            gradient,
            gradient
                + static_cast<std::size_t>(constraints) * variables,
            0.0
        );
        for (unsigned variable = 0; variable < variables; ++variable) {
            gradient[variable] = -1.0 / variables;
            gradient[variables + variable] = 1.0 / variables;
        }
        for (std::size_t index = 0; index < pairs.size(); ++index) {
            const auto [left, right] = pairs[index];
            const std::size_t row = index + 2;
            gradient[row * variables + static_cast<std::size_t>(left)] =
                1.0;
            gradient[row * variables + static_cast<std::size_t>(right)] =
                1.0;
        }
    }
    ++context.calls;
}

std::uint64_t hash_result(
    const std::vector<double>& densities,
    double discrete_aep_gwh
) {
    std::uint64_t hash = 1469598103934665603ULL;
    auto mix = [&](double value) {
        hash ^= std::bit_cast<std::uint64_t>(value);
        hash *= 1099511628211ULL;
    };
    for (double value : densities) {
        mix(value);
    }
    mix(discrete_aep_gwh);
    return hash;
}

}  // namespace

Problem::Problem(CaseId id, int preprocessing_workers)
    : case_id_(id),
      id_(
          id == CaseId::radius_1300
              ? "t21_circle_r1300_n124"
              : "t21_circle_r3000_n709"
      ),
      minimum_turbines_(id == CaseId::radius_1300 ? 16 : 64),
      maximum_turbines_(id == CaseId::radius_1300 ? 64 : 256),
      initial_density_(id == CaseId::radius_1300 ? 0.2 : 0.1805),
      grid_(make_grid(id)),
      spacing_pairs_(make_spacing_pairs(grid_)) {
    if (preprocessing_workers <= 0) {
        throw std::invalid_argument("T21 preprocessing workers must be positive");
    }
    const std::size_t sites = grid_.size();
    squared_pair_deficits_.assign(
        kDirectionsDeg.size() * sites * sites,
        0.0
    );
    fode::PersistentExecutor executor(preprocessing_workers);
    const int tasks = std::max(1, preprocessing_workers);
    executor.parallel_for(0, tasks, [&](int task) {
        const std::size_t total = kDirectionsDeg.size() * sites;
        for (
            std::size_t item = static_cast<std::size_t>(task);
            item < total;
            item += static_cast<std::size_t>(tasks)
        ) {
            const std::size_t direction = item / sites;
            const std::size_t downstream = item % sites;
            const std::size_t base =
                (direction * sites + downstream) * sites;
            for (std::size_t upstream = 0; upstream < sites; ++upstream) {
                squared_pair_deficits_[base + upstream] =
                    pair_deficit_squared(
                        grid_[upstream],
                        grid_[downstream],
                        kDirectionsDeg[direction]
                    );
            }
        }
    });
}

const std::string& Problem::id() const noexcept { return id_; }
int Problem::potential_sites() const noexcept {
    return static_cast<int>(grid_.size());
}
int Problem::minimum_turbines() const noexcept {
    return minimum_turbines_;
}
int Problem::maximum_turbines() const noexcept {
    return maximum_turbines_;
}
double Problem::paper_initial_density() const noexcept {
    return initial_density_;
}
const std::vector<Point>& Problem::grid() const noexcept { return grid_; }
const std::vector<std::pair<int, int>>&
Problem::spacing_pairs() const noexcept {
    return spacing_pairs_;
}

Evaluation Problem::evaluate(
    const std::vector<double>& densities,
    double ramp_penalty,
    fode::PersistentExecutor& executor,
    bool need_gradient
) const {
    const std::size_t sites = grid_.size();
    if (densities.size() != sites || ramp_penalty < 0.0) {
        throw std::invalid_argument("T21 evaluation input mismatch");
    }
    std::vector<double> effective(sites);
    std::vector<double> effective_derivative(sites);
    for (std::size_t index = 0; index < sites; ++index) {
        const double density = densities[index];
        const double denominator =
            1.0 + ramp_penalty * (1.0 - density);
        effective[index] = density / denominator;
        effective_derivative[index] =
            (1.0 + ramp_penalty) / (denominator * denominator);
    }

    const int tasks = executor.thread_count();
    std::vector<double> task_aep(static_cast<std::size_t>(tasks), 0.0);
    std::vector<double> task_gradient(
        need_gradient
            ? static_cast<std::size_t>(tasks) * sites
            : 0U,
        0.0
    );
    const std::size_t target_jobs = kDirectionsDeg.size() * sites;
    executor.parallel_for(0, tasks, [&](int task) {
        double local_aep = 0.0;
        double* local_gradient = need_gradient
            ? task_gradient.data()
                + static_cast<std::size_t>(task) * sites
            : nullptr;
        for (
            std::size_t job = static_cast<std::size_t>(task);
            job < target_jobs;
            job += static_cast<std::size_t>(tasks)
        ) {
            const std::size_t direction = job / sites;
            const std::size_t downstream = job % sites;
            const std::size_t base =
                (direction * sites + downstream) * sites;
            double squared_loss = 0.0;
            for (std::size_t upstream = 0; upstream < sites; ++upstream) {
                squared_loss += effective[upstream]
                    * squared_pair_deficits_[base + upstream];
            }
            const double root_loss = std::sqrt(
                std::max(0.0, squared_loss)
            );
            const double speed =
                std::max(0.0, kWindSpeed * (1.0 - root_loss));
            const double power = turbine_power(speed);
            const double annual_scale =
                8760.0 * kFrequencies[direction] / 1.0e9;
            local_aep += annual_scale
                * effective[downstream] * power;
            if (need_gradient) {
                local_gradient[downstream] += annual_scale * power;
                const double power_slope =
                    turbine_power_derivative(speed);
                if (power_slope != 0.0) {
                    const double speed_slope =
                        -0.5 * kWindSpeed
                        / std::max(root_loss, kWakeGradientFloor);
                    const double common = annual_scale
                        * effective[downstream]
                        * power_slope
                        * speed_slope;
                    for (
                        std::size_t upstream = 0;
                        upstream < sites;
                        ++upstream
                    ) {
                        local_gradient[upstream] += common
                            * squared_pair_deficits_[base + upstream];
                    }
                }
            }
        }
        task_aep[static_cast<std::size_t>(task)] = local_aep;
    });

    Evaluation result;
    result.aep_gwh =
        std::accumulate(task_aep.begin(), task_aep.end(), 0.0);
    result.objective = -result.aep_gwh;
    const double count =
        std::accumulate(densities.begin(), densities.end(), 0.0);
    result.minimum_count_constraint =
        static_cast<double>(minimum_turbines_) / sites - count / sites;
    result.maximum_count_constraint =
        count / sites - static_cast<double>(maximum_turbines_) / sites;
    result.maximum_spacing_constraint =
        -std::numeric_limits<double>::infinity();
    for (const auto [left, right] : spacing_pairs_) {
        result.maximum_spacing_constraint = std::max(
            result.maximum_spacing_constraint,
            densities[static_cast<std::size_t>(left)]
                + densities[static_cast<std::size_t>(right)]
                - 1.0
        );
    }
    result.observed_workers = executor.work_receipt().distinct_participants;
    if (need_gradient) {
        result.objective_gradient.assign(sites, 0.0);
        for (std::size_t variable = 0; variable < sites; ++variable) {
            double derivative = 0.0;
            for (int task = 0; task < tasks; ++task) {
                derivative += task_gradient[
                    static_cast<std::size_t>(task) * sites + variable
                ];
            }
            result.objective_gradient[variable] =
                -derivative * effective_derivative[variable];
        }
    }
    return result;
}

double Problem::maximum_constraint_violation(
    const std::vector<double>& densities
) const {
    if (densities.size() != grid_.size()) {
        throw std::invalid_argument("T21 constraint input mismatch");
    }
    const double count =
        std::accumulate(densities.begin(), densities.end(), 0.0);
    double violation = std::max(
        0.0,
        static_cast<double>(minimum_turbines_) / grid_.size()
            - count / grid_.size()
    );
    violation = std::max(
        violation,
        count / grid_.size()
            - static_cast<double>(maximum_turbines_) / grid_.size()
    );
    for (const auto [left, right] : spacing_pairs_) {
        violation = std::max(
            violation,
            densities[static_cast<std::size_t>(left)]
                + densities[static_cast<std::size_t>(right)]
                - 1.0
        );
    }
    return std::max(0.0, violation);
}

RunResult run(const Problem& problem, const RunConfig& config) {
    if (
        config.workers <= 0
        || config.maximum_objective_evaluations <= 0
        || config.start_index < 0
    ) {
        throw std::invalid_argument("T21 run configuration invalid");
    }
    const auto started = Clock::now();
    fode::PersistentExecutor executor(config.workers);
    executor.reset_work_receipt();
    const std::size_t variables =
        static_cast<std::size_t>(problem.potential_sites());
    const fode::CounterRng rng(config.seed);
    std::vector<double> densities(
        variables,
        problem.paper_initial_density()
    );
    double q = 0.0;
    double q_increment = 0.5;
    if (config.random_start) {
        const double common_density = problem.paper_initial_density()
            * rng.uniform(
                static_cast<std::uint64_t>(config.start_index),
                2101,
                0
            );
        std::fill(densities.begin(), densities.end(), common_density);
        q = rng.uniform(
            static_cast<std::uint64_t>(config.start_index),
            2102,
            0
        );
        q_increment = std::max(
            1.0e-6,
            rng.uniform(
                static_cast<std::uint64_t>(config.start_index),
                2103,
                0
            )
        );
    }
    if (config.linear_interpolation) {
        q = 0.0;
        q_increment = 0.0;
    }
    const double initial_q = q;

    ObjectiveContext objective_context{
        &problem,
        &executor,
        q,
        0,
        0,
        0.0
    };
    ConstraintContext constraint_context{&problem, 0};
    const unsigned constraint_count = static_cast<unsigned>(
        problem.spacing_pairs().size() + 2U
    );
    const std::vector<double> constraint_tolerances(
        constraint_count,
        1.0e-8
    );
    nlopt_result final_status = NLOPT_FAILURE;
    double minimum = std::numeric_limits<double>::infinity();
    int remaining = config.maximum_objective_evaluations;
    std::vector<double> prior = densities;

    while (remaining > 0) {
        const int calls_before_stage = objective_context.objective_calls;
        const int stage_budget = std::min(10, remaining);
        const std::vector<double> lower(variables, 0.0);
        const std::vector<double> upper(variables, 1.0);
        objective_context.ramp_penalty = q;
        nlopt_opt optimizer = nlopt_create(
            NLOPT_LD_MMA,
            static_cast<unsigned>(variables)
        );
        if (optimizer == nullptr) {
            throw std::runtime_error("failed to create T21 NLopt MMA");
        }
        try {
            if (
                nlopt_set_lower_bounds(optimizer, lower.data()) < 0
                || nlopt_set_upper_bounds(optimizer, upper.data()) < 0
                || nlopt_set_min_objective(
                    optimizer,
                    objective_callback,
                    &objective_context
                ) < 0
                || nlopt_add_inequality_mconstraint(
                    optimizer,
                    constraint_count,
                    constraint_callback,
                    &constraint_context,
                    constraint_tolerances.data()
                ) < 0
                || nlopt_set_xtol_abs1(
                    optimizer,
                    kDensityTolerance
                ) < 0
                || nlopt_set_initial_step1(
                    optimizer,
                    kPaperMovingLimit
                ) < 0
                || nlopt_set_param(
                    optimizer,
                    "inner_maxeval",
                    10.0
                ) < 0
                || nlopt_set_param(
                    optimizer,
                    "dual_ftol_rel",
                    1.0e-8
                ) < 0
                || nlopt_set_param(
                    optimizer,
                    "dual_maxeval",
                    500.0
                ) < 0
                || nlopt_set_maxeval(optimizer, stage_budget) < 0
            ) {
                throw std::runtime_error("failed to configure T21 MMA");
            }
            final_status = nlopt_optimize(
                optimizer,
                densities.data(),
                &minimum
            );
        } catch (...) {
            nlopt_destroy(optimizer);
            throw;
        }
        nlopt_destroy(optimizer);
        remaining =
            config.maximum_objective_evaluations
            - objective_context.objective_calls;
        if (objective_context.objective_calls == calls_before_stage) {
            break;
        }
        double change_squared = 0.0;
        for (std::size_t index = 0; index < variables; ++index) {
            const double difference = densities[index] - prior[index];
            change_squared += difference * difference;
        }
        prior = densities;
        if (
            final_status == NLOPT_XTOL_REACHED
            || std::sqrt(change_squared) < kDensityTolerance
        ) {
            break;
        }
        if (!config.linear_interpolation && q < kRampMaximum) {
            q = std::min(kRampMaximum, q + q_increment);
        }
        if (
            final_status == NLOPT_INVALID_ARGS
            || final_status == NLOPT_OUT_OF_MEMORY
            || final_status == NLOPT_FORCED_STOP
        ) {
            break;
        }
    }

    const Evaluation relaxed = problem.evaluate(
        densities,
        q,
        executor,
        false
    );
    std::vector<double> discrete(variables, 0.0);
    int discrete_turbines = 0;
    for (std::size_t index = 0; index < variables; ++index) {
        if (densities[index] > 0.5) {
            discrete[index] = 1.0;
            ++discrete_turbines;
        }
    }
    const Evaluation discrete_evaluation = problem.evaluate(
        discrete,
        0.0,
        executor,
        false
    );
    const double end_to_end = seconds_since(started);
    RunResult result;
    result.problem_id = problem.id();
    result.seed = config.seed;
    result.start_index = config.start_index;
    result.requested_workers = config.workers;
    result.observed_workers =
        executor.work_receipt().distinct_participants;
    result.potential_sites = static_cast<int>(variables);
    result.discrete_turbines = discrete_turbines;
    result.objective_evaluations = objective_context.objective_calls;
    result.gradient_evaluations = objective_context.gradient_calls;
    result.optimizer_status = static_cast<int>(final_status);
    result.optimizer_status_name = nlopt_status_name(final_status);
    result.initial_q = initial_q;
    result.q_increment = q_increment;
    result.final_q = q;
    result.relaxed_aep_gwh = relaxed.aep_gwh;
    result.discrete_aep_gwh = discrete_evaluation.aep_gwh;
    result.relaxed_constraint_violation =
        problem.maximum_constraint_violation(densities);
    result.evaluator_seconds = objective_context.evaluator_seconds;
    result.end_to_end_seconds = end_to_end;
    result.optimizer_seconds =
        std::max(0.0, end_to_end - result.evaluator_seconds);
    result.scientific_hash =
        hash_result(densities, result.discrete_aep_gwh);
    result.densities = std::move(densities);
    return result;
}

}  // namespace core99::t21
