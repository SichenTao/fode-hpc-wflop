/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T67 mixed-variable physics, cost model, and all-core GA
Paper DOI: 10.1016/j.renene.2016.10.038
Public source: no target source or native 61-turbine arrays were located.
Related public source: https://github.com/NatLabRockies/SAM is an independent
commercial-turbine range reference only.
Missing information, conflicts, reconstruction, semantic IDs, production
backend, controlling contract, and claim boundary:
include/core99/abdulrahman_t67.hpp
Claim boundary: declared academic reconstruction, not author numerical replay.
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "core99/abdulrahman_t67.hpp"

#include "fode/executor.hpp"
#include "fode/rng.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <string>
#include <utility>

namespace core99::t67 {
namespace {

using Clock = std::chrono::steady_clock;
constexpr double kPi = 3.14159265358979323846;
constexpr double kAirDensity = 1.225;
constexpr double kReferenceHeightM = 60.0;
constexpr double kMinimumHubM = 80.0;
constexpr double kMaximumHubM = 140.0;
constexpr double kNominalDiameterM = 112.0;
constexpr double kMinimumSpacingM = 3.0 * kNominalDiameterM;

struct Individual {
    Decision decision;
    Evaluation evaluation;
    double fitness = std::numeric_limits<double>::infinity();
};

double elapsed(const Clock::time_point& start) {
    return std::chrono::duration<double>(Clock::now() - start).count();
}

double smoothstep5(const double value) {
    const double x = std::clamp(value, 0.0, 1.0);
    return x * x * x * (10.0 + x * (-15.0 + 6.0 * x));
}

double power_mw(const TurbineSpec& turbine, const double speed) {
    if (speed < turbine.cut_in_mps || speed > turbine.cut_out_mps) {
        return 0.0;
    }
    if (speed >= turbine.rated_speed_mps) {
        return turbine.rated_power_mw;
    }
    const double fraction =
        (speed - turbine.cut_in_mps)
        / (turbine.rated_speed_mps - turbine.cut_in_mps);
    return turbine.rated_power_mw * smoothstep5(fraction);
}

double thrust_coefficient(
    const TurbineSpec& turbine,
    const double speed
) {
    if (speed < turbine.cut_in_mps || speed > turbine.cut_out_mps) {
        return 0.05;
    }
    const double power_w = power_mw(turbine, speed) * 1.0e6;
    const double area = kPi * 0.25
        * turbine.diameter_m * turbine.diameter_m;
    const double cp = std::clamp(
        power_w / std::max(
            1.0,
            0.5 * kAirDensity * area * speed * speed * speed
        ),
        0.0,
        0.59
    );
    const double cp2 = cp * cp;
    const double cp3 = cp2 * cp;
    const double cp4 = cp3 * cp;
    const double cp5 = cp4 * cp;
    return std::clamp(
        0.10 + 2.20 * cp - 2.10 * cp2
        + 0.80 * cp3 - 0.20 * cp4 + 0.02 * cp5,
        0.08,
        0.88
    );
}

double circle_overlap(
    const double first_radius,
    const double second_radius,
    const double distance
) {
    if (distance >= first_radius + second_radius) return 0.0;
    if (distance <= std::abs(first_radius - second_radius)) {
        const double radius = std::min(first_radius, second_radius);
        return kPi * radius * radius;
    }
    const double first_angle = std::acos(std::clamp(
        (distance * distance + first_radius * first_radius
            - second_radius * second_radius)
        / (2.0 * distance * first_radius),
        -1.0,
        1.0
    ));
    const double second_angle = std::acos(std::clamp(
        (distance * distance + second_radius * second_radius
            - first_radius * first_radius)
        / (2.0 * distance * second_radius),
        -1.0,
        1.0
    ));
    const double triangle = 0.5 * std::sqrt(std::max(
        0.0,
        (-distance + first_radius + second_radius)
        * (distance + first_radius - second_radius)
        * (distance - first_radius + second_radius)
        * (distance + first_radius + second_radius)
    ));
    return first_radius * first_radius * first_angle
        + second_radius * second_radius * second_angle - triangle;
}

std::vector<std::pair<double, double>> coordinates(
    const Problem& problem,
    const Decision& decision
) {
    const int count = problem.turbine_count();
    std::vector<std::pair<double, double>> result(
        static_cast<std::size_t>(count)
    );
    if (problem.layout_kind() == LayoutKind::turbine_in_line) {
        for (int index = 0; index < count; ++index) {
            result[static_cast<std::size_t>(index)] = {
                0.0, decision.y_m[static_cast<std::size_t>(index)]
            };
        }
        return result;
    }
    const double crosswind_step = 3.0 * kNominalDiameterM;
    const double downwind_step =
        problem.length_y_m() / 5.0;
    for (int row = 0; row < 6; ++row) {
        for (int column = 0; column < 3; ++column) {
            const int index = row * 3 + column;
            double x = crosswind_step * static_cast<double>(column);
            if (
                problem.layout_kind() == LayoutKind::staggered
                && (row & 1) != 0
            ) {
                x += 0.5 * crosswind_step;
            }
            result[static_cast<std::size_t>(index)] = {
                x, downwind_step * static_cast<double>(row)
            };
        }
    }
    return result;
}

void repair_til(Decision& decision, const double length_y) {
    std::sort(decision.y_m.begin(), decision.y_m.end());
    const int count = static_cast<int>(decision.y_m.size());
    for (int index = 0; index < count; ++index) {
        const double lower = static_cast<double>(index) * kMinimumSpacingM;
        const double upper = length_y
            - static_cast<double>(count - 1 - index) * kMinimumSpacingM;
        decision.y_m[static_cast<std::size_t>(index)] =
            std::clamp(
                decision.y_m[static_cast<std::size_t>(index)],
                lower,
                upper
            );
        if (index > 0) {
            decision.y_m[static_cast<std::size_t>(index)] = std::max(
                decision.y_m[static_cast<std::size_t>(index)],
                decision.y_m[static_cast<std::size_t>(index - 1)]
                    + kMinimumSpacingM
            );
        }
    }
}

std::uint64_t hash_mix(std::uint64_t hash, const std::uint64_t value) {
    hash ^= value;
    hash *= 1099511628211ULL;
    return hash;
}

std::uint64_t scientific_hash(
    const Individual& best,
    const int generations,
    const std::uint64_t physical_fes
) {
    std::uint64_t hash = 1469598103934665603ULL;
    hash = hash_mix(hash, static_cast<std::uint64_t>(generations));
    hash = hash_mix(hash, physical_fes);
    for (const double value : best.decision.y_m) {
        hash = hash_mix(
            hash,
            static_cast<std::uint64_t>(std::llround(value * 1.0e6))
        );
    }
    for (const int value : best.decision.turbine_code) {
        hash = hash_mix(hash, static_cast<std::uint64_t>(value));
    }
    for (const int value : best.decision.hub_height_m) {
        hash = hash_mix(hash, static_cast<std::uint64_t>(value));
    }
    hash = hash_mix(hash, static_cast<std::uint64_t>(
        std::llround(best.evaluation.total_power_mw * 1.0e9)
    ));
    hash = hash_mix(hash, static_cast<std::uint64_t>(
        std::llround(best.evaluation.total_cost_index * 1.0e9)
    ));
    return hash;
}

}  // namespace

const std::vector<TurbineSpec>& turbine_catalog() {
    static const std::vector<TurbineSpec> catalog = [] {
        std::vector<TurbineSpec> result;
        result.reserve(61);
        const std::array<std::pair<int, double>, 6> power_anchors{{
            {1, 1.5},
            {6, 1.5},
            {19, 1.8},
            {45, 2.5},
            {56, 3.0},
            {61, 3.075},
        }};
        auto interpolated_power = [&](const int code) {
            for (std::size_t right = 1;
                 right < power_anchors.size();
                 ++right) {
                const auto [left_code, left_power] =
                    power_anchors[right - 1];
                const auto [right_code, right_power] =
                    power_anchors[right];
                if (code > right_code) continue;
                const double fraction = static_cast<double>(
                    code - left_code
                ) / static_cast<double>(right_code - left_code);
                return left_power
                    + fraction * (right_power - left_power);
            }
            return power_anchors.back().second;
        };
        for (int code = 1; code <= 61; ++code) {
            const double fraction = static_cast<double>(code - 1) / 60.0;
            TurbineSpec item;
            item.code = code;
            item.rated_power_mw = interpolated_power(code);
            item.diameter_m = std::clamp(
                66.0 + 43.0 * fraction
                    + static_cast<double>((code * 17) % 13),
                66.0,
                115.0
            );
            item.rated_speed_mps =
                11.0 + static_cast<double>((code * 5) % 7);
            result.push_back(item);
        }
        const std::array<TurbineSpec, 6> anchors{{
            {1, 1.5, 77.0, 13.0, 3.0, 25.0},
            {6, 1.5, 82.0, 12.0, 3.0, 25.0},
            {19, 1.8, 100.0, 12.0, 3.0, 25.0},
            {45, 2.5, 115.0, 12.0, 3.0, 25.0},
            {56, 3.0, 112.0, 12.0, 3.0, 25.0},
            {61, 3.075, 112.0, 13.0, 3.0, 25.0},
        }};
        for (const auto& anchor : anchors) {
            result[static_cast<std::size_t>(anchor.code - 1)] = anchor;
        }
        return result;
    }();
    return catalog;
}

std::string layout_kind_name(const LayoutKind value) {
    if (value == LayoutKind::turbine_in_line) return "til";
    if (value == LayoutKind::array) return "array";
    return "staggered";
}
std::string terrain_name(const Terrain value) {
    return value == Terrain::onshore ? "onshore" : "offshore";
}
std::string objective_name(const Objective value) {
    if (value == Objective::maximum_power) return "max_power";
    if (value == Objective::maximum_capacity_factor) return "max_cf";
    return "min_tciop";
}

Problem::Problem(
    const LayoutKind layout_kind,
    const int spacing_multiplier,
    const double reference_speed_mps,
    const Terrain terrain,
    const Objective objective
)
    : id_(
          "t67_" + layout_kind_name(layout_kind)
          + "_s" + std::to_string(spacing_multiplier)
          + "_u" + std::to_string(static_cast<int>(reference_speed_mps))
          + "_" + terrain_name(terrain)
          + "_" + objective_name(objective)
      ),
      layout_kind_(layout_kind),
      spacing_multiplier_(spacing_multiplier),
      reference_speed_mps_(reference_speed_mps),
      terrain_(terrain),
      objective_(objective),
      turbine_count_(
          layout_kind == LayoutKind::turbine_in_line ? 6 : 18
      ),
      length_y_m_(5.0 * spacing_multiplier * kNominalDiameterM),
      length_x_m_(
          layout_kind == LayoutKind::staggered
              ? 2.5 * 3.0 * kNominalDiameterM
              : 2.0 * 3.0 * kNominalDiameterM
      ) {
    if (
        (spacing_multiplier != 3
            && spacing_multiplier != 4
            && spacing_multiplier != 5)
        || (reference_speed_mps != 8.0
            && reference_speed_mps != 10.0
            && reference_speed_mps != 12.0)
    ) {
        throw std::invalid_argument("T67 case outside paper matrix");
    }
}

const std::string& Problem::id() const noexcept { return id_; }
LayoutKind Problem::layout_kind() const noexcept { return layout_kind_; }
int Problem::spacing_multiplier() const noexcept {
    return spacing_multiplier_;
}
double Problem::reference_speed_mps() const noexcept {
    return reference_speed_mps_;
}
Terrain Problem::terrain() const noexcept { return terrain_; }
Objective Problem::objective() const noexcept { return objective_; }
int Problem::turbine_count() const noexcept { return turbine_count_; }
double Problem::length_y_m() const noexcept { return length_y_m_; }
double Problem::length_x_m() const noexcept { return length_x_m_; }

Decision Problem::reference_decision() const {
    Decision result;
    if (layout_kind_ == LayoutKind::turbine_in_line) {
        result.y_m.resize(6);
        for (int index = 0; index < 6; ++index) {
            result.y_m[static_cast<std::size_t>(index)] =
                length_y_m_ * static_cast<double>(index) / 5.0;
        }
    }
    result.turbine_code.assign(
        static_cast<std::size_t>(turbine_count_), 61
    );
    result.hub_height_m.assign(
        static_cast<std::size_t>(turbine_count_), 140
    );
    return result;
}

Evaluation Problem::evaluate(const Decision& decision) const {
    if (
        static_cast<int>(decision.turbine_code.size()) != turbine_count_
        || static_cast<int>(decision.hub_height_m.size()) != turbine_count_
        || (
            layout_kind_ == LayoutKind::turbine_in_line
            && static_cast<int>(decision.y_m.size()) != turbine_count_
        )
    ) {
        throw std::invalid_argument("T67 decision cardinality mismatch");
    }
    const auto points = coordinates(*this, decision);
    const auto& catalog = turbine_catalog();
    Evaluation result;
    result.minimum_spacing_margin_m =
        std::numeric_limits<double>::infinity();
    std::vector<int> order(static_cast<std::size_t>(turbine_count_));
    std::iota(order.begin(), order.end(), 0);
    std::stable_sort(order.begin(), order.end(), [&](int left, int right) {
        return points[static_cast<std::size_t>(left)].second
            < points[static_cast<std::size_t>(right)].second;
    });
    std::vector<double> power(static_cast<std::size_t>(turbine_count_), 0.0);
    for (int rank = 0; rank < turbine_count_; ++rank) {
        const int index = order[static_cast<std::size_t>(rank)];
        const int code =
            decision.turbine_code[static_cast<std::size_t>(index)];
        const int hub =
            decision.hub_height_m[static_cast<std::size_t>(index)];
        if (code < 1 || code > 61 || hub < 80 || hub > 140) {
            return result;
        }
        const auto& turbine = catalog[static_cast<std::size_t>(code - 1)];
        const double roughness =
            terrain_ == Terrain::onshore ? 0.3 : 0.0002;
        const double undisturbed = reference_speed_mps_
            * std::log(static_cast<double>(hub) / roughness)
            / std::log(kReferenceHeightM / roughness);
        double squared_deficit = 0.0;
        for (int upstream_rank = 0;
             upstream_rank < rank;
             ++upstream_rank) {
            const int upstream =
                order[static_cast<std::size_t>(upstream_rank)];
            const double downstream_distance =
                points[static_cast<std::size_t>(index)].second
                - points[static_cast<std::size_t>(upstream)].second;
            if (downstream_distance <= 0.0) continue;
            const auto& source = catalog[static_cast<std::size_t>(
                decision.turbine_code[
                    static_cast<std::size_t>(upstream)
                ] - 1
            )];
            const double source_hub = static_cast<double>(
                decision.hub_height_m[
                    static_cast<std::size_t>(upstream)
                ]
            );
            const double source_rough_speed = reference_speed_mps_
                * std::log(source_hub / roughness)
                / std::log(kReferenceHeightM / roughness);
            const double ct = thrust_coefficient(
                source, source_rough_speed
            );
            const double axial =
                0.5 * (1.0 - std::sqrt(std::max(0.0, 1.0 - ct)));
            const double source_radius = 0.5 * source.diameter_m;
            const double expanded_radius = source_radius * std::sqrt(
                (1.0 - axial) / std::max(1.0e-9, 1.0 - 2.0 * axial)
            );
            const double wake_expansion =
                0.5 / std::log(source_hub / roughness);
            const double wake_radius =
                expanded_radius + wake_expansion * downstream_distance;
            const double target_radius = 0.5 * turbine.diameter_m;
            const double dx =
                points[static_cast<std::size_t>(index)].first
                - points[static_cast<std::size_t>(upstream)].first;
            const double dh = static_cast<double>(hub) - source_hub;
            const double center_distance = std::hypot(dx, dh);
            const double overlap = circle_overlap(
                wake_radius, target_radius, center_distance
            );
            const double target_area =
                kPi * target_radius * target_radius;
            const double deficit =
                (1.0 - std::sqrt(std::max(0.0, 1.0 - ct)))
                / std::pow(
                    1.0 + wake_expansion * downstream_distance
                        / expanded_radius,
                    2.0
                )
                * overlap / target_area;
            squared_deficit += deficit * deficit;
        }
        const double effective =
            undisturbed * (1.0 - std::sqrt(squared_deficit));
        power[static_cast<std::size_t>(index)] =
            power_mw(turbine, std::max(0.0, effective));
        result.rated_power_mw += turbine.rated_power_mw;
        const double tower_fraction =
            terrain_ == Terrain::onshore ? 0.12 : 0.0565;
        result.total_cost_index += turbine.rated_power_mw
            * (
                1.0 + tower_fraction / kMinimumHubM
                    * (static_cast<double>(hub) - kMinimumHubM)
            );
    }
    const double om = terrain_ == Terrain::onshore ? 0.15 : 0.25;
    result.total_cost_index /= 1.0 - om;
    result.total_power_mw =
        std::accumulate(power.begin(), power.end(), 0.0);
    result.capacity_factor = result.total_power_mw
        / std::max(1.0e-12, result.rated_power_mw);
    result.total_cost_index_per_output_power =
        result.total_cost_index
        / std::max(1.0e-12, result.total_power_mw);
    if (layout_kind_ == LayoutKind::turbine_in_line) {
        for (int left = 0; left < turbine_count_; ++left) {
            for (int right = left + 1; right < turbine_count_; ++right) {
                result.minimum_spacing_margin_m = std::min(
                    result.minimum_spacing_margin_m,
                    std::abs(
                        decision.y_m[static_cast<std::size_t>(right)]
                        - decision.y_m[static_cast<std::size_t>(left)]
                    ) - kMinimumSpacingM
                );
            }
        }
    } else {
        result.minimum_spacing_margin_m = 0.0;
    }
    result.feasible = result.minimum_spacing_margin_m >= -1.0e-9;
    return result;
}

double Problem::fitness(const Evaluation& evaluation) const {
    if (!evaluation.feasible) return 1.0e300;
    if (objective_ == Objective::maximum_power) {
        return -evaluation.total_power_mw;
    }
    if (objective_ == Objective::maximum_capacity_factor) {
        return -evaluation.capacity_factor;
    }
    return evaluation.total_cost_index_per_output_power;
}

RunResult run(const Problem& problem, const RunConfig& config) {
    if (
        config.workers < 1
        || config.population_size < 8
        || config.maximum_generations < 1
        || config.stall_generations < 1
        || config.tolerance_function < 0.0
    ) {
        throw std::invalid_argument("T67 run configuration invalid");
    }
    const auto started = Clock::now();
    fode::PersistentExecutor executor(config.workers);
    executor.reset_work_receipt();
    const fode::CounterRng rng(config.seed);
    const int count = problem.turbine_count();
    const int population_size = config.population_size;
    const int elite_count = std::max(
        1,
        static_cast<int>(
            std::ceil(0.05 * static_cast<double>(population_size))
        )
    );
    const int offspring_count = population_size - elite_count;

    auto initialize = [&](Decision& decision, int individual) {
        decision.turbine_code.resize(static_cast<std::size_t>(count));
        decision.hub_height_m.resize(static_cast<std::size_t>(count));
        for (int turbine = 0; turbine < count; ++turbine) {
            decision.turbine_code[static_cast<std::size_t>(turbine)] =
                rng.integer(1, 62, 0, 6701, individual, turbine);
            decision.hub_height_m[static_cast<std::size_t>(turbine)] =
                rng.integer(80, 141, 0, 6702, individual, turbine);
        }
        if (problem.layout_kind() == LayoutKind::turbine_in_line) {
            decision.y_m.resize(static_cast<std::size_t>(count));
            const double slack = problem.length_y_m()
                - static_cast<double>(count - 1) * kMinimumSpacingM;
            std::vector<double> offsets(static_cast<std::size_t>(count));
            for (int turbine = 0; turbine < count; ++turbine) {
                offsets[static_cast<std::size_t>(turbine)] =
                    slack * rng.uniform(
                        0, 6703, individual, turbine
                    );
            }
            std::sort(offsets.begin(), offsets.end());
            for (int turbine = 0; turbine < count; ++turbine) {
                decision.y_m[static_cast<std::size_t>(turbine)] =
                    static_cast<double>(turbine) * kMinimumSpacingM
                    + offsets[static_cast<std::size_t>(turbine)];
            }
        }
    };

    std::vector<Individual> population(
        static_cast<std::size_t>(population_size)
    );
    executor.parallel_for(0, population_size, [&](int individual) {
        initialize(
            population[static_cast<std::size_t>(individual)].decision,
            individual
        );
    });
    double evaluator_seconds = 0.0;
    auto evaluate_population = [&](std::vector<Individual>& values) {
        const auto evaluation_started = Clock::now();
        executor.parallel_for(
            0,
            static_cast<int>(values.size()),
            [&](int index) {
                auto& item = values[static_cast<std::size_t>(index)];
                item.evaluation = problem.evaluate(item.decision);
                item.fitness = problem.fitness(item.evaluation);
            }
        );
        evaluator_seconds += elapsed(evaluation_started);
    };
    evaluate_population(population);
    std::uint64_t physical_fes =
        static_cast<std::uint64_t>(population_size);
    auto better = [](const Individual& left, const Individual& right) {
        return left.fitness < right.fitness;
    };
    std::stable_sort(population.begin(), population.end(), better);
    double previous_best = population.front().fitness;
    int stall = 0;
    int generations = 0;
    bool converged = false;
    while (
        generations < config.maximum_generations
        && !converged
    ) {
        ++generations;
        std::vector<Individual> offspring(
            static_cast<std::size_t>(offspring_count)
        );
        executor.parallel_for(0, offspring_count, [&](int child) {
            auto tournament = [&](int draw) {
                const int left = rng.integer(
                    0, population_size,
                    generations, 6704, child, draw, 0
                );
                const int right = rng.integer(
                    0, population_size,
                    generations, 6704, child, draw, 1
                );
                return population[
                    static_cast<std::size_t>(
                        population[static_cast<std::size_t>(left)].fitness
                            <= population[static_cast<std::size_t>(right)].fitness
                            ? left : right
                    )
                ].decision;
            };
            const Decision first = tournament(0);
            const Decision second = tournament(1);
            auto& decision =
                offspring[static_cast<std::size_t>(child)].decision;
            decision.turbine_code.resize(static_cast<std::size_t>(count));
            decision.hub_height_m.resize(static_cast<std::size_t>(count));
            if (problem.layout_kind() == LayoutKind::turbine_in_line) {
                decision.y_m.resize(static_cast<std::size_t>(count));
            }
            const int dimensions = count * (
                problem.layout_kind() == LayoutKind::turbine_in_line ? 3 : 2
            );
            const bool perform_crossover = rng.uniform(
                generations, 6710, child
            ) < 0.8;
            for (int turbine = 0; turbine < count; ++turbine) {
                const bool select_second = perform_crossover
                    && rng.uniform(
                        generations, 6705, child, turbine
                    ) < 0.5;
                decision.turbine_code[static_cast<std::size_t>(turbine)] =
                    select_second
                        ? second.turbine_code[static_cast<std::size_t>(turbine)]
                        : first.turbine_code[static_cast<std::size_t>(turbine)];
                decision.hub_height_m[static_cast<std::size_t>(turbine)] =
                    select_second
                        ? second.hub_height_m[static_cast<std::size_t>(turbine)]
                        : first.hub_height_m[static_cast<std::size_t>(turbine)];
                if (
                    rng.uniform(generations, 6706, child, turbine, 0)
                    < 1.0 / static_cast<double>(dimensions)
                ) {
                    decision.turbine_code[static_cast<std::size_t>(turbine)] =
                        rng.integer(
                            1, 62, generations, 6707, child, turbine
                        );
                }
                if (
                    rng.uniform(generations, 6706, child, turbine, 1)
                    < 1.0 / static_cast<double>(dimensions)
                ) {
                    const int step = rng.integer(
                        -10, 11, generations, 6708, child, turbine
                    );
                    decision.hub_height_m[static_cast<std::size_t>(turbine)] =
                        std::clamp(
                            decision.hub_height_m[
                                static_cast<std::size_t>(turbine)
                            ] + step,
                            80,
                            140
                        );
                }
                if (problem.layout_kind() == LayoutKind::turbine_in_line) {
                    decision.y_m[static_cast<std::size_t>(turbine)] =
                        select_second
                            ? second.y_m[static_cast<std::size_t>(turbine)]
                            : first.y_m[static_cast<std::size_t>(turbine)];
                    if (
                        rng.uniform(generations, 6706, child, turbine, 2)
                        < 1.0 / static_cast<double>(dimensions)
                    ) {
                        decision.y_m[static_cast<std::size_t>(turbine)] +=
                            0.1 * problem.length_y_m()
                            * (
                                rng.uniform(
                                    generations, 6709, child, turbine, 0
                                )
                                - rng.uniform(
                                    generations, 6709, child, turbine, 1
                                )
                            );
                    }
                }
            }
            if (problem.layout_kind() == LayoutKind::turbine_in_line) {
                repair_til(decision, problem.length_y_m());
            }
        });
        evaluate_population(offspring);
        physical_fes += static_cast<std::uint64_t>(offspring_count);
        std::vector<Individual> next;
        next.reserve(static_cast<std::size_t>(population_size));
        for (int elite = 0; elite < elite_count; ++elite) {
            next.push_back(
                std::move(population[static_cast<std::size_t>(elite)])
            );
        }
        next.insert(
            next.end(),
            std::make_move_iterator(offspring.begin()),
            std::make_move_iterator(offspring.end())
        );
        std::stable_sort(next.begin(), next.end(), better);
        population = std::move(next);
        const double current = population.front().fitness;
        const double relative = std::abs(current - previous_best)
            / std::max(1.0, std::abs(previous_best));
        stall = relative <= config.tolerance_function ? stall + 1 : 0;
        previous_best = current;
        converged = stall >= config.stall_generations;
    }
    const double end_to_end = elapsed(started);
    const double algorithm_seconds =
        std::max(0.0, end_to_end - evaluator_seconds);
    const auto& best = population.front();
    return {
        .case_id = problem.id(),
        .method_semantic_id =
            "t67_matlab_ga_mixed_turbine_height_declared_v1",
        .problem_semantic_id =
            "t67_til_swf_power_cf_tciop_162role_declared_v1",
        .protocol_semantic_id =
            "t67_3000gen_25seed_162role_v1",
        .seed = config.seed,
        .requested_workers = config.workers,
        .observed_workers =
            executor.work_receipt().distinct_participants,
        .population_size = population_size,
        .generations = generations,
        .physical_fes = physical_fes,
        .converged = converged,
        .evaluator_seconds = evaluator_seconds,
        .algorithm_seconds = algorithm_seconds,
        .end_to_end_seconds = end_to_end,
        .scientific_hash =
            scientific_hash(best, generations, physical_fes),
        .best_decision = best.decision,
        .best_evaluation = best.evaluation,
    };
}

std::vector<std::string> paper_case_ids() {
    std::vector<std::string> result;
    for (const auto layout : {
        LayoutKind::turbine_in_line,
        LayoutKind::array,
        LayoutKind::staggered,
    }) {
        for (const int spacing : {3, 4, 5}) {
            for (const int speed : {8, 10, 12}) {
                for (const auto terrain : {
                    Terrain::onshore, Terrain::offshore,
                }) {
                    for (const auto objective : {
                        Objective::maximum_power,
                        Objective::maximum_capacity_factor,
                        Objective::minimum_tciop,
                    }) {
                        result.push_back(
                            Problem(
                                layout,
                                spacing,
                                static_cast<double>(speed),
                                terrain,
                                objective
                            ).id()
                        );
                    }
                }
            }
        }
    }
    return result;
}

}  // namespace core99::t67
