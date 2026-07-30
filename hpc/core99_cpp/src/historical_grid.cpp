/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: high-performance T01/T02 historical grid evaluator and GA
Paper/DOI/source/missing/conflict/resolution/semantic/claim details: see
include/core99/historical_grid.hpp and
shared/contracts/core99_mosetti_grady_cases.json
Independent H5 authority: scripts/validate_core99_historical_grid.py
HPC design: precompute 36x100x100 wake coefficients; use stack-local selected
cell arrays; evaluate independent layouts through one persistent worker team;
keep roulette selection and elitist island commits deterministically ordered
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "core99/historical_grid.hpp"

#include "fode/rng.hpp"

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
#include <utility>

namespace core99 {
namespace {

using Clock = std::chrono::steady_clock;

constexpr int kCellCount = 100;
constexpr int kDirectionCount = 36;
constexpr double kCellWidth = 200.0;
constexpr double kRotorRadius = 20.0;
constexpr double kHubHeight = 60.0;
constexpr double kRoughness = 0.3;
constexpr double kThrustCoefficient = 0.88;
constexpr std::array<double, 9> kTailDirectionWeights{
    1.549, 1.841, 2.132, 3.395, 4.029, 3.395, 2.132, 1.841, 1.549
};
constexpr std::array<std::array<double, 3>, 9> kTailSpeedWeights{{
    {0.836, 0.578, 0.135},
    {0.836, 0.870, 0.135},
    {0.836, 1.161, 0.135},
    {0.836, 1.128, 1.431},
    {0.836, 1.762, 1.431},
    {0.836, 1.128, 1.431},
    {0.836, 1.161, 0.135},
    {0.836, 0.870, 0.135},
    {0.836, 0.578, 0.135},
}};

std::size_t deficit_index(int direction, int source, int target) {
    return static_cast<std::size_t>(
        (direction * kCellCount + source) * kCellCount + target
    );
}

double circle_overlap(double distance, double first_radius, double second_radius) {
    if (distance >= first_radius + second_radius) {
        return 0.0;
    }
    if (distance <= std::abs(first_radius - second_radius)) {
        const double radius = std::min(first_radius, second_radius);
        return std::numbers::pi * radius * radius;
    }
    const double first_angle = std::acos(
        std::clamp(
            (
                distance * distance
                + first_radius * first_radius
                - second_radius * second_radius
            ) / (2.0 * distance * first_radius),
            -1.0,
            1.0
        )
    );
    const double second_angle = std::acos(
        std::clamp(
            (
                distance * distance
                + second_radius * second_radius
                - first_radius * first_radius
            ) / (2.0 * distance * second_radius),
            -1.0,
            1.0
        )
    );
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

double turbine_power(double speed) {
    if (!(speed > 0.0)) {
        return 0.0;
    }
    return std::min(0.3 * speed * speed * speed, 630.0);
}

double farm_cost(int turbines) {
    const double count = static_cast<double>(turbines);
    return count * (
        2.0 / 3.0
        + std::exp(-0.00174 * count * count) / 3.0
    );
}

std::uint64_t profile_salt(const std::string& value) {
    std::uint64_t hash = 1469598103934665603ULL;
    for (const unsigned char byte : value) {
        hash ^= static_cast<std::uint64_t>(byte);
        hash *= 1099511628211ULL;
    }
    return hash;
}

bool occupied(LayoutBits layout, int cell) {
    if (cell < 64) {
        return ((layout.low >> cell) & 1ULL) != 0ULL;
    }
    return ((layout.high >> (cell - 64)) & 1ULL) != 0ULL;
}

void set_occupied(LayoutBits& layout, int cell, bool value) {
    std::uint64_t& word = cell < 64 ? layout.low : layout.high;
    const int shift = cell < 64 ? cell : cell - 64;
    const std::uint64_t mask = 1ULL << shift;
    if (value) {
        word |= mask;
    } else {
        word &= ~mask;
    }
}

std::uint64_t layout_hash(LayoutBits layout) {
    std::uint64_t value = layout.low + 0x9e3779b97f4a7c15ULL;
    value = (value ^ (value >> 30U)) * 0xbf58476d1ce4e5b9ULL;
    value ^= layout.high + 0x94d049bb133111ebULL;
    value = (value ^ (value >> 27U)) * 0x94d049bb133111ebULL;
    return value ^ (value >> 31U);
}

bool better(
    const HistoricalEvaluation& left,
    LayoutBits left_layout,
    const HistoricalEvaluation& right,
    LayoutBits right_layout
) {
    if (left.objective < right.objective) {
        return true;
    }
    if (left.objective > right.objective) {
        return false;
    }
    return left_layout < right_layout;
}

int select_parent(
    const std::vector<HistoricalEvaluation>& values,
    int begin,
    int end,
    const fode::CounterRng& rng,
    std::uint64_t generation,
    std::uint64_t island,
    std::uint64_t child,
    std::uint64_t parent_slot
) {
    double total = 0.0;
    for (int index = begin; index < end; ++index) {
        total += 1.0 / std::max(
            values[static_cast<std::size_t>(index)].objective,
            1.0e-300
        );
    }
    const double draw = rng.uniform(
        generation,
        island,
        child,
        parent_slot
    ) * total;
    double cumulative = 0.0;
    for (int index = begin; index < end; ++index) {
        cumulative += 1.0 / std::max(
            values[static_cast<std::size_t>(index)].objective,
            1.0e-300
        );
        if (draw <= cumulative) {
            return index;
        }
    }
    return end - 1;
}

}  // namespace

HistoricalGridProblem::HistoricalGridProblem(std::string problem_id)
    : id_(std::move(problem_id)),
      semantic_id_("core99_mosetti_grady_historical_grid_v1"),
      deficit_(
          static_cast<std::size_t>(kDirectionCount)
              * kCellCount * kCellCount,
          0.0
      ) {
    const std::array<std::string, 8> valid{
        "t01_mosetti_case_a",
        "t01_mosetti_case_b",
        "t01_mosetti_case_c",
        "t02_grady_case_a",
        "t02_grady_case_b",
        "t02_grady_case_c_body1000",
        "t02_grady_case_c_abstract2500",
        "t02_grady_case_c"
    };
    if (std::find(valid.begin(), valid.end(), id_) == valid.end()) {
        throw std::invalid_argument("unknown historical problem: " + id_);
    }
    if (id_ == "t02_grady_case_c") {
        id_ = "t02_grady_case_c_body1000";
    }
    configure_states();
    precompute_deficits();
}

const std::string& HistoricalGridProblem::id() const noexcept {
    return id_;
}

const std::string& HistoricalGridProblem::semantic_id() const noexcept {
    return semantic_id_;
}

void HistoricalGridProblem::configure_states() {
    const bool case_a = id_.ends_with("_case_a");
    const bool case_b = id_.ends_with("_case_b");
    if (case_a) {
        states_.push_back({12.0, 1.0, 0});
        return;
    }
    if (case_b) {
        for (int direction = 0; direction < kDirectionCount; ++direction) {
            states_.push_back(
                {12.0, 1.0 / static_cast<double>(kDirectionCount), direction}
            );
        }
        return;
    }
    double raw_total = 0.0;
    for (int direction = 0; direction < 27; ++direction) {
        raw_total += 0.836 + 0.292 + 0.135;
    }
    raw_total += std::accumulate(
        kTailDirectionWeights.begin(),
        kTailDirectionWeights.end(),
        0.0
    );
    const std::array<double, 3> speeds{8.0, 12.0, 17.0};
    for (int direction = 0; direction < kDirectionCount; ++direction) {
        const auto weights = direction < 27
            ? std::array<double, 3>{0.836, 0.292, 0.135}
            : kTailSpeedWeights[static_cast<std::size_t>(direction - 27)];
        for (int speed = 0; speed < 3; ++speed) {
            states_.push_back(
                {
                    speeds[static_cast<std::size_t>(speed)],
                    weights[static_cast<std::size_t>(speed)] / raw_total,
                    direction
                }
            );
        }
    }
}

void HistoricalGridProblem::precompute_deficits() {
    const double axial_induction =
        0.5 * (1.0 - std::sqrt(1.0 - kThrustCoefficient));
    const double initial_wake_radius = kRotorRadius * std::sqrt(
        (1.0 - axial_induction) / (1.0 - 2.0 * axial_induction)
    );
    const double entrainment = 0.5 / std::log(kHubHeight / kRoughness);
    const double full_rotor_area =
        std::numbers::pi * kRotorRadius * kRotorRadius;

    for (int direction = 0; direction < kDirectionCount; ++direction) {
        const double angle = (
            static_cast<double>(direction) * 10.0
        ) * std::numbers::pi / 180.0;
        const double sine = std::sin(angle);
        const double cosine = std::cos(angle);
        for (int source = 0; source < kCellCount; ++source) {
            const double source_x =
                (static_cast<double>(source % 10) + 0.5) * kCellWidth;
            const double source_y =
                (static_cast<double>(source / 10) + 0.5) * kCellWidth;
            for (int target = 0; target < kCellCount; ++target) {
                if (source == target) {
                    continue;
                }
                const double target_x =
                    (static_cast<double>(target % 10) + 0.5) * kCellWidth;
                const double target_y =
                    (static_cast<double>(target / 10) + 0.5) * kCellWidth;
                const double dx = target_x - source_x;
                const double dy = target_y - source_y;
                const double downstream = sine * dx + cosine * dy;
                if (!(downstream > 0.0)) {
                    continue;
                }
                const double crosswind = std::abs(cosine * dx - sine * dy);
                const double wake_radius =
                    initial_wake_radius + entrainment * downstream;
                const double overlap = circle_overlap(
                    crosswind,
                    wake_radius,
                    kRotorRadius
                );
                const double center_deficit =
                    2.0 * axial_induction
                    * initial_wake_radius * initial_wake_radius
                    / (wake_radius * wake_radius);
                deficit_[deficit_index(direction, source, target)] =
                    center_deficit * overlap / full_rotor_area;
            }
        }
    }
}

HistoricalEvaluation HistoricalGridProblem::evaluate(LayoutBits layout) const {
    std::array<int, kCellCount> selected{};
    int count = 0;
    for (int cell = 0; cell < kCellCount; ++cell) {
        if (occupied(layout, cell)) {
            selected[static_cast<std::size_t>(count++)] = cell;
        }
    }
    if (count == 0) {
        return {
            std::numeric_limits<double>::infinity(),
            0.0,
            0
        };
    }
    double expected_power = 0.0;
    for (const auto& state : states_) {
        double state_power = 0.0;
        for (int target_index = 0; target_index < count; ++target_index) {
            const int target = selected[static_cast<std::size_t>(target_index)];
            double squared_deficit = 0.0;
            for (int source_index = 0; source_index < count; ++source_index) {
                const int source =
                    selected[static_cast<std::size_t>(source_index)];
                const double deficit = deficit_[deficit_index(
                    state.direction_index,
                    source,
                    target
                )];
                squared_deficit += deficit * deficit;
            }
            const double combined = std::min(1.0, std::sqrt(squared_deficit));
            state_power += turbine_power(state.speed * (1.0 - combined));
        }
        expected_power += state.probability * state_power;
    }
    return {
        farm_cost(count) / expected_power,
        expected_power,
        count
    };
}

std::vector<HistoricalEvaluation> HistoricalGridProblem::evaluate_population(
    const std::vector<LayoutBits>& layouts,
    fode::PersistentExecutor& executor
) const {
    std::vector<HistoricalEvaluation> results(layouts.size());
    executor.parallel_for(
        0,
        static_cast<int>(layouts.size()),
        [&](int index) {
            results[static_cast<std::size_t>(index)] = evaluate(
                layouts[static_cast<std::size_t>(index)]
            );
        }
    );
    return results;
}

HistoricalProfile historical_profile(const std::string& algorithm_id) {
    if (algorithm_id == "t01_mosetti_ga") {
        return {
            algorithm_id,
            "t01_mosetti_ga_declared_v1",
            200,
            1,
            400,
            0.7,
            0.01
        };
    }
    if (algorithm_id == "t02_grady_island_ga") {
        return {
            algorithm_id,
            "t02_grady_island_ga_declared_v1",
            600,
            20,
            3000,
            0.7,
            0.01
        };
    }
    throw std::invalid_argument("unknown historical algorithm: " + algorithm_id);
}

std::uint64_t default_physical_fes(
    const HistoricalProfile& profile,
    const std::string& problem_id
) {
    int generations = profile.generations;
    if (
        problem_id == "t02_grady_case_c_body1000"
        || problem_id == "t02_grady_case_c"
    ) {
        generations = 1000;
    } else if (problem_id == "t02_grady_case_c_abstract2500") {
        generations = 2500;
    }
    return static_cast<std::uint64_t>(profile.population)
        * static_cast<std::uint64_t>(generations + 1);
}

HistoricalRunResult run_historical_ga(
    const HistoricalGridProblem& problem,
    const HistoricalProfile& profile,
    const HistoricalRunRequest& request
) {
    if (
        profile.population <= 0
        || profile.islands <= 0
        || profile.population % profile.islands != 0
        || request.physical_fes == 0
    ) {
        throw std::invalid_argument("invalid historical GA request");
    }
    const auto start = Clock::now();
    fode::PersistentExecutor executor(request.workers);
    const fode::CounterRng rng(
        request.seed ^ profile_salt(profile.method_semantic_id)
    );
    std::vector<LayoutBits> population(
        static_cast<std::size_t>(profile.population)
    );
    executor.parallel_for(0, profile.population, [&](int individual) {
        LayoutBits layout;
        for (int gene = 0; gene < kCellCount; ++gene) {
            set_occupied(
                layout,
                gene,
                rng.uniform(
                    0,
                    static_cast<std::uint64_t>(individual),
                    static_cast<std::uint64_t>(gene)
                ) < 0.5
            );
        }
        if (layout.low == 0 && layout.high == 0) {
            const int cell = rng.integer(
                0,
                kCellCount,
                0,
                static_cast<std::uint64_t>(individual),
                101
            );
            set_occupied(layout, cell, true);
        }
        population[static_cast<std::size_t>(individual)] = layout;
    });

    std::uint64_t fes = 0;
    double evaluator_seconds = 0.0;
    const int initial_count = static_cast<int>(
        std::min<std::uint64_t>(
            static_cast<std::uint64_t>(profile.population),
            request.physical_fes
        )
    );
    population.resize(static_cast<std::size_t>(initial_count));
    auto eval_start = Clock::now();
    auto values = problem.evaluate_population(population, executor);
    evaluator_seconds += std::chrono::duration<double>(
        Clock::now() - eval_start
    ).count();
    fes += static_cast<std::uint64_t>(initial_count);
    auto best_it = std::min_element(
        values.begin(),
        values.end(),
        [&](const HistoricalEvaluation& left, const HistoricalEvaluation& right) {
            const auto left_index = static_cast<std::size_t>(&left - values.data());
            const auto right_index = static_cast<std::size_t>(&right - values.data());
            return better(
                left,
                population[left_index],
                right,
                population[right_index]
            );
        }
    );
    std::size_t best_index = static_cast<std::size_t>(
        best_it - values.begin()
    );
    LayoutBits best_layout = population[best_index];
    HistoricalEvaluation best_value = values[best_index];
    std::uint64_t completed_generations = 0;

    while (
        population.size() == static_cast<std::size_t>(profile.population)
        && fes < request.physical_fes
    ) {
        const int island_size = profile.population / profile.islands;
        std::vector<LayoutBits> next(population.size());
        executor.parallel_for(0, profile.islands, [&](int island) {
            const int begin = island * island_size;
            const int end = begin + island_size;
            int elite = begin;
            for (int index = begin + 1; index < end; ++index) {
                if (better(
                        values[static_cast<std::size_t>(index)],
                        population[static_cast<std::size_t>(index)],
                        values[static_cast<std::size_t>(elite)],
                        population[static_cast<std::size_t>(elite)]
                    )) {
                    elite = index;
                }
            }
            next[static_cast<std::size_t>(begin)] =
                population[static_cast<std::size_t>(elite)];
            for (int offset = 1; offset < island_size; ++offset) {
                const int child_index = begin + offset;
                const int parent_a = select_parent(
                    values,
                    begin,
                    end,
                    rng,
                    completed_generations + 1,
                    static_cast<std::uint64_t>(island),
                    static_cast<std::uint64_t>(offset),
                    0
                );
                const int parent_b = select_parent(
                    values,
                    begin,
                    end,
                    rng,
                    completed_generations + 1,
                    static_cast<std::uint64_t>(island),
                    static_cast<std::uint64_t>(offset),
                    1
                );
                LayoutBits child = population[
                    static_cast<std::size_t>(parent_a)
                ];
                const bool crossover = rng.uniform(
                    completed_generations + 1,
                    static_cast<std::uint64_t>(island),
                    static_cast<std::uint64_t>(offset),
                    2
                ) < profile.crossover_probability;
                for (int gene = 0; gene < kCellCount; ++gene) {
                    if (
                        crossover
                        && rng.uniform(
                            completed_generations + 1,
                            static_cast<std::uint64_t>(island),
                            static_cast<std::uint64_t>(offset),
                            3,
                            static_cast<std::uint64_t>(gene)
                        ) < 0.5
                    ) {
                        set_occupied(
                            child,
                            gene,
                            occupied(
                                population[static_cast<std::size_t>(parent_b)],
                                gene
                            )
                        );
                    }
                    if (rng.uniform(
                            completed_generations + 1,
                            static_cast<std::uint64_t>(island),
                            static_cast<std::uint64_t>(offset),
                            4,
                            static_cast<std::uint64_t>(gene)
                        ) < profile.mutation_probability) {
                        set_occupied(child, gene, !occupied(child, gene));
                    }
                }
                if (child.low == 0 && child.high == 0) {
                    set_occupied(
                        child,
                        rng.integer(
                            0,
                            kCellCount,
                            completed_generations + 1,
                            static_cast<std::uint64_t>(island),
                            static_cast<std::uint64_t>(offset),
                            5
                        ),
                        true
                    );
                }
                next[static_cast<std::size_t>(child_index)] = child;
            }
        });
        const int evaluate_count = static_cast<int>(
            std::min<std::uint64_t>(
                static_cast<std::uint64_t>(next.size()),
                request.physical_fes - fes
            )
        );
        next.resize(static_cast<std::size_t>(evaluate_count));
        eval_start = Clock::now();
        auto next_values = problem.evaluate_population(next, executor);
        evaluator_seconds += std::chrono::duration<double>(
            Clock::now() - eval_start
        ).count();
        fes += static_cast<std::uint64_t>(evaluate_count);
        for (int index = 0; index < evaluate_count; ++index) {
            if (better(
                    next_values[static_cast<std::size_t>(index)],
                    next[static_cast<std::size_t>(index)],
                    best_value,
                    best_layout
                )) {
                best_value = next_values[static_cast<std::size_t>(index)];
                best_layout = next[static_cast<std::size_t>(index)];
            }
        }
        population = std::move(next);
        values = std::move(next_values);
        ++completed_generations;
    }

    const double end_to_end = std::chrono::duration<double>(
        Clock::now() - start
    ).count();
    return {
        profile.algorithm_id,
        profile.method_semantic_id,
        problem.id(),
        problem.semantic_id(),
        best_layout,
        best_value,
        fes,
        completed_generations,
        request.workers,
        executor.thread_count(),
        evaluator_seconds,
        std::max(0.0, end_to_end - evaluator_seconds),
        end_to_end,
        layout_hash(best_layout)
            ^ std::bit_cast<std::uint64_t>(best_value.objective)
            ^ fes
    };
}

std::vector<int> layout_cells(LayoutBits layout) {
    std::vector<int> cells;
    for (int cell = 0; cell < kCellCount; ++cell) {
        if (occupied(layout, cell)) {
            cells.push_back(cell + 1);
        }
    }
    return cells;
}

LayoutBits layout_from_cells(const std::vector<int>& cells) {
    LayoutBits layout;
    for (const int cell_1based : cells) {
        if (cell_1based < 1 || cell_1based > kCellCount) {
            throw std::invalid_argument("layout cell outside 1..100");
        }
        if (occupied(layout, cell_1based - 1)) {
            throw std::invalid_argument("duplicate layout cell");
        }
        set_occupied(layout, cell_1based - 1, true);
    }
    return layout;
}

}  // namespace core99
