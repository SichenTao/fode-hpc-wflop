/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T20 evaluator, decoders, and HPC batch execution
Paper title and DOI: Comparative Performance of Twelve Metaheuristics for
Wind Farm Layout Optimisation, 10.1007/s11831-021-09586-7.
Public source: no paper-linked author code or data archive was located.
Missing fields and Reconstruction: include/core99/kunakote_t20.hpp.
Semantic IDs and Contract: shared/contracts/core99_t20_kunakote_2022.json.
Independent equation oracle: scripts/validate_core99_t20.py
HPC design: persistent workers evaluate independent complete layouts; writes
are indexed and deterministic; no algorithm semantics are changed
Claim boundary: problem and comparison-interface reproduction, not author
code or twelve-baseline numerical replay.
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "core99/kunakote_t20.hpp"

#include <algorithm>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <utility>

namespace core99::t20 {
namespace {

using Clock = std::chrono::steady_clock;

constexpr double kFarmSideM = 2000.0;
constexpr double kGridPitchM = 200.0;
constexpr double kRotorDiameterM = 40.0;
constexpr double kRotorRadiusM = 20.0;
constexpr double kHubHeightM = 60.0;
constexpr double kRoughnessM = 0.3;
constexpr double kThrustCoefficient = 0.88;
constexpr double kEfficiency = 0.4;
constexpr double kAirDensity = 1.225;
constexpr double kFreeSpeedMps = 12.0;
constexpr double kWakeDecay = 0.5 / std::log(kHubHeightM / kRoughnessM);
constexpr double kMinimumSpacingM = 5.0 * kRotorDiameterM;

double overlap_area(
    const double distance,
    const double first_radius,
    const double second_radius
) {
    if (distance >= first_radius + second_radius) {
        return 0.0;
    }
    if (distance <= std::abs(first_radius - second_radius)) {
        const double radius = std::min(first_radius, second_radius);
        return std::numbers::pi * radius * radius;
    }
    const double first_angle = std::acos(std::clamp(
        (
            distance * distance + first_radius * first_radius
            - second_radius * second_radius
        ) / (2.0 * distance * first_radius),
        -1.0,
        1.0
    ));
    const double second_angle = std::acos(std::clamp(
        (
            distance * distance + second_radius * second_radius
            - first_radius * first_radius
        ) / (2.0 * distance * second_radius),
        -1.0,
        1.0
    ));
    const double radicand = std::max(
        0.0,
        (-distance + first_radius + second_radius)
        * (distance + first_radius - second_radius)
        * (distance - first_radius + second_radius)
        * (distance + first_radius + second_radius)
    );
    return first_radius * first_radius * first_angle
        + second_radius * second_radius * second_angle
        - 0.5 * std::sqrt(radicand);
}

double paper_cost(const int turbines) {
    return static_cast<double>(turbines) * (
        2.0 / 3.0
        + std::exp(
            -0.00174
            * static_cast<double>(turbines)
            * static_cast<double>(turbines)
        ) / 3.0
    );
}

double direction_power_kw(
    const std::vector<Point>& layout,
    const double direction_radians,
    const bool partial_overlap
) {
    const std::size_t count = layout.size();
    std::vector<double> downwind(count, 0.0);
    std::vector<double> crosswind(count, 0.0);
    const double cosine = std::cos(direction_radians);
    const double sine = std::sin(direction_radians);
    for (std::size_t turbine = 0; turbine < count; ++turbine) {
        downwind[turbine] =
            layout[turbine].x_m * cosine + layout[turbine].y_m * sine;
        crosswind[turbine] =
            -layout[turbine].x_m * sine + layout[turbine].y_m * cosine;
    }

    const double rotor_area =
        std::numbers::pi * kRotorRadiusM * kRotorRadiusM;
    const double axial_deficit =
        1.0 - std::sqrt(1.0 - kThrustCoefficient);
    double total_power_w = 0.0;
    for (std::size_t downstream = 0; downstream < count; ++downstream) {
        double deficit_squared = 0.0;
        for (std::size_t upstream = 0; upstream < count; ++upstream) {
            const double axial_distance =
                downwind[downstream] - downwind[upstream];
            if (!(axial_distance > 1.0e-10)) {
                continue;
            }
            const double wake_radius =
                kRotorRadiusM + kWakeDecay * axial_distance;
            const double radial_distance = std::abs(
                crosswind[downstream] - crosswind[upstream]
            );
            double covered_fraction = 0.0;
            if (partial_overlap) {
                covered_fraction = overlap_area(
                    radial_distance,
                    wake_radius,
                    kRotorRadiusM
                ) / rotor_area;
            } else if (radial_distance <= wake_radius) {
                covered_fraction = 1.0;
            }
            if (!(covered_fraction > 0.0)) {
                continue;
            }
            const double expanded_diameter =
                kRotorDiameterM + 2.0 * kWakeDecay * axial_distance;
            const double deficit =
                axial_deficit
                * (kRotorDiameterM * kRotorDiameterM)
                / (expanded_diameter * expanded_diameter)
                * covered_fraction;
            deficit_squared += deficit * deficit;
        }
        const double local_speed = kFreeSpeedMps * std::max(
            0.0,
            1.0 - std::sqrt(deficit_squared)
        );
        total_power_w += 0.5
            * kEfficiency
            * kAirDensity
            * rotor_area
            * local_speed
            * local_speed
            * local_speed;
    }
    return total_power_w / 1000.0;
}

double constraint_violation(const std::vector<Point>& layout) {
    if (layout.empty()) {
        return 1.0;
    }
    double violation = 0.0;
    for (const Point& point : layout) {
        violation += std::max(
            {0.0, -point.x_m, point.x_m - kFarmSideM}
        );
        violation += std::max(
            {0.0, -point.y_m, point.y_m - kFarmSideM}
        );
    }
    for (std::size_t left = 0; left < layout.size(); ++left) {
        for (std::size_t right = left + 1; right < layout.size(); ++right) {
            const double dx = layout[left].x_m - layout[right].x_m;
            const double dy = layout[left].y_m - layout[right].y_m;
            violation += std::max(
                0.0,
                kMinimumSpacingM - std::sqrt(dx * dx + dy * dy)
            );
        }
    }
    return violation;
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
    if (left.objective != right.objective) {
        return left.objective < right.objective;
    }
    return left_variables < right_variables;
}

std::uint64_t mix(std::uint64_t value) {
    value += 0x9E3779B97F4A7C15ULL;
    value = (value ^ (value >> 30U)) * 0xBF58476D1CE4E5B9ULL;
    value = (value ^ (value >> 27U)) * 0x94D049BB133111EBULL;
    return value ^ (value >> 31U);
}

double unit_random(
    const std::uint64_t seed,
    const std::uint64_t candidate,
    const std::uint64_t coordinate
) {
    const std::uint64_t bits = mix(
        seed ^ mix(candidate + 1U) ^ mix(coordinate + 17U)
    );
    return static_cast<double>(bits >> 11U)
        * (1.0 / 9007199254740992.0);
}

std::uint64_t result_hash(
    const std::vector<double>& variables,
    const Evaluation& evaluation
) {
    std::uint64_t hash = 1469598103934665603ULL;
    for (const double value : variables) {
        hash ^= std::bit_cast<std::uint64_t>(value);
        hash *= 1099511628211ULL;
    }
    hash ^= std::bit_cast<std::uint64_t>(evaluation.objective);
    hash *= 1099511628211ULL;
    hash ^= std::bit_cast<std::uint64_t>(evaluation.average_power_kw);
    hash *= 1099511628211ULL;
    return hash;
}

}  // namespace

Problem::Problem(std::string problem_id) : id_(std::move(problem_id)) {
    if (id_ == "t20_case1_variable_hub") {
        paper_case_ = 1;
    } else if (id_ == "t20_case2_variable_partial") {
        paper_case_ = 2;
        partial_overlap_ = true;
    } else if (id_ == "t20_case3_fixed39_hub") {
        paper_case_ = 3;
        fixed_count_ = true;
    } else if (id_ == "t20_case4_fixed39_partial") {
        paper_case_ = 4;
        partial_overlap_ = true;
        fixed_count_ = true;
    } else {
        throw std::invalid_argument("unknown T20 problem: " + id_);
    }
}

const std::string& Problem::id() const noexcept {
    return id_;
}

int Problem::paper_case() const noexcept {
    return paper_case_;
}

int Problem::dimension() const noexcept {
    return fixed_count_ ? 39 : 100;
}

bool Problem::uses_partial_overlap() const noexcept {
    return partial_overlap_;
}

bool Problem::has_fixed_turbine_count() const noexcept {
    return fixed_count_;
}

std::vector<double> Problem::lower_bounds() const {
    return std::vector<double>(
        static_cast<std::size_t>(dimension()),
        fixed_count_ ? 1.0 : 0.0
    );
}

std::vector<double> Problem::upper_bounds() const {
    return std::vector<double>(
        static_cast<std::size_t>(dimension()),
        fixed_count_ ? 100.0 : 1.0
    );
}

std::vector<Point> Problem::decode(
    const std::vector<double>& variables
) const {
    if (static_cast<int>(variables.size()) != dimension()) {
        throw std::invalid_argument("T20 variable dimension mismatch");
    }
    std::vector<int> grid_indices;
    if (!fixed_count_) {
        for (int index = 0; index < 100; ++index) {
            if (std::floor(variables[static_cast<std::size_t>(index)] + 0.5)
                >= 1.0) {
                grid_indices.push_back(index);
            }
        }
    } else {
        std::vector<int> remaining(100);
        for (int index = 0; index < 100; ++index) {
            remaining[static_cast<std::size_t>(index)] = index;
        }
        for (const double variable : variables) {
            const long rounded = std::lround(variable);
            std::size_t selected = remaining.size() - 1U;
            if (rounded >= 1
                && rounded <= static_cast<long>(remaining.size())) {
                selected = static_cast<std::size_t>(rounded - 1L);
            }
            grid_indices.push_back(remaining[selected]);
            remaining.erase(remaining.begin() + static_cast<long>(selected));
        }
    }

    std::vector<Point> result;
    result.reserve(grid_indices.size());
    for (const int index : grid_indices) {
        const int column = index % 10;
        const int row = index / 10;
        result.push_back(Point{
            100.0 + kGridPitchM * static_cast<double>(column),
            100.0 + kGridPitchM * static_cast<double>(row)
        });
    }
    return result;
}

Evaluation Problem::evaluate(
    const std::vector<double>& variables
) const {
    return evaluate_layout(decode(variables));
}

Evaluation Problem::evaluate_layout(
    const std::vector<Point>& layout
) const {
    Evaluation result;
    result.turbine_count = static_cast<int>(layout.size());
    result.constraint_violation = constraint_violation(layout);
    result.cost = paper_cost(result.turbine_count);
    for (int direction = 1; direction <= 36; ++direction) {
        result.average_power_kw += direction_power_kw(
            layout,
            std::numbers::pi * static_cast<double>(10 * direction) / 180.0,
            partial_overlap_
        );
    }
    result.average_power_kw /= 36.0;
    result.objective = layout.empty()
        ? std::numeric_limits<double>::infinity()
        : result.cost / result.average_power_kw;
    return result;
}

std::vector<Evaluation> Problem::evaluate_population(
    const std::vector<std::vector<double>>& population,
    fode::PersistentExecutor& executor
) const {
    std::vector<Evaluation> result(population.size());
    executor.parallel_for(
        0,
        static_cast<int>(population.size()),
        [&](const int raw_index) {
            const auto index = static_cast<std::size_t>(raw_index);
            result[index] = evaluate(population[index]);
        }
    );
    return result;
}

std::vector<Point> paper_figure_5_layout() {
    const std::vector<std::vector<int>> labels_by_row{
        {1, 0, 0, 14, 21, 0, 34, 40, 0, 0},
        {2, 8, 0, 0, 0, 30, 0, 0, 44, 0},
        {0, 9, 15, 0, 25, 31, 35, 41, 45, 51},
        {3, 0, 16, 0, 26, 0, 36, 0, 46, 52},
        {4, 10, 17, 0, 0, 0, 37, 42, 47, 53},
        {0, 0, 18, 22, 0, 0, 38, 0, 0, 0},
        {0, 0, 19, 23, 0, 32, 39, 48, 0, 54},
        {5, 11, 0, 0, 27, 0, 0, 49, 0, 0},
        {6, 12, 20, 24, 28, 33, 0, 43, 0, 0},
        {7, 13, 0, 0, 29, 0, 0, 50, 0, 0}
    };
    std::vector<Point> result(54);
    for (std::size_t row = 0; row < labels_by_row.size(); ++row) {
        for (std::size_t column = 0;
             column < labels_by_row[row].size();
             ++column) {
            const int label = labels_by_row[row][column];
            if (label > 0) {
                result[static_cast<std::size_t>(label - 1)] = Point{
                    100.0 + kGridPitchM * static_cast<double>(column),
                    100.0 + kGridPitchM * static_cast<double>(row)
                };
            }
        }
    }
    return result;
}

BatchReceipt run_batch_profile(
    const Problem& problem,
    const std::uint64_t seed,
    const std::uint64_t physical_fes,
    const int workers
) {
    if (physical_fes == 0U) {
        throw std::invalid_argument("T20 physical FES must be positive");
    }
    const auto start = Clock::now();
    const auto lower = problem.lower_bounds();
    const auto upper = problem.upper_bounds();
    std::vector<std::vector<double>> population(
        static_cast<std::size_t>(physical_fes),
        std::vector<double>(static_cast<std::size_t>(problem.dimension()))
    );
    for (std::uint64_t candidate = 0; candidate < physical_fes; ++candidate) {
        for (int coordinate = 0; coordinate < problem.dimension(); ++coordinate) {
            const double unit = unit_random(
                seed,
                candidate,
                static_cast<std::uint64_t>(coordinate)
            );
            population[static_cast<std::size_t>(candidate)]
                      [static_cast<std::size_t>(coordinate)] =
                lower[static_cast<std::size_t>(coordinate)]
                + unit * (
                    upper[static_cast<std::size_t>(coordinate)]
                    - lower[static_cast<std::size_t>(coordinate)]
                );
        }
    }
    const auto algorithm_done = Clock::now();
    fode::PersistentExecutor executor(workers);
    const auto values = problem.evaluate_population(population, executor);
    const auto evaluation_done = Clock::now();

    std::size_t best = 0;
    for (std::size_t index = 1; index < values.size(); ++index) {
        if (better(
            values[index],
            population[index],
            values[best],
            population[best]
        )) {
            best = index;
        }
    }
    const auto end = Clock::now();
    BatchReceipt receipt;
    receipt.problem_id = problem.id();
    receipt.seed = seed;
    receipt.physical_fes = physical_fes;
    receipt.requested_workers = workers;
    receipt.observed_workers =
        executor.work_receipt().distinct_participants;
    receipt.algorithm_seconds =
        std::chrono::duration<double>(algorithm_done - start).count()
        + std::chrono::duration<double>(end - evaluation_done).count();
    receipt.evaluator_seconds =
        std::chrono::duration<double>(
            evaluation_done - algorithm_done
        ).count();
    receipt.end_to_end_seconds =
        std::chrono::duration<double>(end - start).count();
    receipt.best_evaluation = values[best];
    receipt.best_variables = population[best];
    receipt.scientific_hash = result_hash(
        receipt.best_variables,
        receipt.best_evaluation
    );
    return receipt;
}

}  // namespace core99::t20
