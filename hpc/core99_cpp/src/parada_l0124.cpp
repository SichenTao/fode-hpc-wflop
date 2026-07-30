/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: L0124 pure-C++ Gaussian evaluator, MI-LXPM transitions,
fixed-order receipts and persistent-population parallelism
Paper/DOI/source/missing/conflict/reconstruction/semantic IDs/backend/claim:
hpc/core99_cpp/include/core99/parada_l0124.hpp
Controlling contract: shared/contracts/core99_l0124_parada_2017.json
Independent validator: scripts/validate_core99_l0124.py
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "core99/parada_l0124.hpp"

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
#include <string>
#include <utility>

namespace core99::l0124 {
namespace {

using Clock = std::chrono::steady_clock;

constexpr int direction_count = 36;
constexpr double farm_width_m = 2000.0;
constexpr double rotor_diameter_m = 40.0;
constexpr double minimum_spacing_m = 5.0 * rotor_diameter_m;
constexpr double thrust_coefficient = 0.88;
constexpr double wake_growth = 0.055;
constexpr int tournament_size = 3;
constexpr double crossover_probability = 0.8;
constexpr double mutation_probability = 0.005;
constexpr double elite_fraction = 0.05;
constexpr double laplace_location = 0.0;
constexpr double laplace_integer_scale = 0.35;
constexpr double power_integer_index = 4.0;
// Fig. 6 is vector artwork, not a numeric table. Each probability below is
// the nearest 0.001 represented by the rectangle height against the published
// 0.01 y-axis ticks. The first 27 directions share {0.004, 0.009, 0.011}.
constexpr std::array<std::array<double, 3>, 9> case_c_tail_probabilities{{
    {0.004, 0.011, 0.013},
    {0.004, 0.013, 0.016},
    {0.004, 0.015, 0.019},
    {0.004, 0.014, 0.031},
    {0.004, 0.019, 0.036},
    {0.004, 0.014, 0.031},
    {0.004, 0.015, 0.019},
    {0.004, 0.012, 0.016},
    {0.004, 0.011, 0.013},
}};

double turbine_power_kw(const double speed_mps) {
    return 0.3 * speed_mps * speed_mps * speed_mps;
}

double farm_cost(const int turbine_count) {
    const double count = static_cast<double>(turbine_count);
    return count * (
        2.0 / 3.0
        + std::exp(-0.00174 * count * count) / 3.0
    );
}

std::size_t deficit_index(
    const int direction,
    const int cell_count,
    const int source,
    const int target
) {
    return static_cast<std::size_t>(
        (direction * cell_count + source) * cell_count + target
    );
}

bool better(
    const Evaluation& left,
    const std::vector<int>& left_coordinates,
    const Evaluation& right,
    const std::vector<int>& right_coordinates
) {
    if (left.feasible != right.feasible) return left.feasible;
    if (left.feasible) {
        if (left.objective < right.objective) return true;
        if (left.objective > right.objective) return false;
    } else {
        if (
            left.total_normalized_constraint_violation
            < right.total_normalized_constraint_violation
        ) return true;
        if (
            left.total_normalized_constraint_violation
            > right.total_normalized_constraint_violation
        ) return false;
        if (left.objective < right.objective) return true;
        if (left.objective > right.objective) return false;
    }
    return left_coordinates < right_coordinates;
}

int stochastic_integer(
    const double raw,
    const int upper_exclusive,
    const double rounding_draw
) {
    const double bounded = std::clamp(
        raw, 0.0, static_cast<double>(upper_exclusive - 1)
    );
    const double floor_value = std::floor(bounded);
    if (bounded == floor_value) return static_cast<int>(floor_value);
    const int result = static_cast<int>(floor_value)
        + (rounding_draw >= 0.5 ? 1 : 0);
    return std::min(result, upper_exclusive - 1);
}

std::uint64_t mix_hash(std::uint64_t hash, const std::uint64_t value) {
    hash ^= value + 0x9e3779b97f4a7c15ULL + (hash << 6U) + (hash >> 2U);
    return hash;
}

}  // namespace

double gaussian_deficit_ratio(
    const double downstream_m,
    const double crosswind_m
) {
    // The target paper states that the Gaussian model is valid from 3D.
    if (downstream_m < 3.0 * rotor_diameter_m) return 0.0;
    const double root = std::sqrt(1.0 - thrust_coefficient);
    const double beta = 0.5 * (1.0 + root) / root;
    const double epsilon = 0.2 * std::sqrt(beta);
    const double sigma_ratio =
        wake_growth * downstream_m / rotor_diameter_m + epsilon;
    const double radicand = 1.0
        - thrust_coefficient / (8.0 * sigma_ratio * sigma_ratio);
    if (radicand <= 0.0) return 0.0;
    const double centre = 1.0 - std::sqrt(radicand);
    const double normalized_crosswind = crosswind_m / rotor_diameter_m;
    return centre * std::exp(
        -0.5 * normalized_crosswind * normalized_crosswind
        / (sigma_ratio * sigma_ratio)
    );
}

Problem::Problem(std::string problem_id)
    : id_(std::move(problem_id)),
      semantic_id_("l0124_parada_gaussian_grid_v1") {
    const std::array<std::string, 6> valid{
        "l0124_case_a_grid10",
        "l0124_case_a_grid20",
        "l0124_case_b_grid10",
        "l0124_case_b_grid20",
        "l0124_case_c_grid10",
        "l0124_case_c_grid20",
    };
    if (std::find(valid.begin(), valid.end(), id_) == valid.end()) {
        throw std::invalid_argument("unknown L0124 problem ID: " + id_);
    }
    grid_size_ = id_.ends_with("grid10") ? 10 : 20;
    if (id_.find("_case_a_") != std::string::npos) {
        turbine_count_ = 30;
    } else {
        turbine_count_ = 39;
    }
    configure_states();
    precompute_deficits();
    for (const WindState& state : wind_states_) {
        no_wake_power_kw_ += state.probability
            * static_cast<double>(turbine_count_)
            * turbine_power_kw(state.speed_mps);
    }
}

const std::string& Problem::id() const noexcept {
    return id_;
}

const std::string& Problem::semantic_id() const noexcept {
    return semantic_id_;
}

int Problem::grid_size() const noexcept {
    return grid_size_;
}

int Problem::turbine_count() const noexcept {
    return turbine_count_;
}

int Problem::variable_count() const noexcept {
    return 2 * turbine_count_;
}

double Problem::no_wake_power_kw() const noexcept {
    return no_wake_power_kw_;
}

void Problem::configure_states() {
    if (id_.find("_case_a_") != std::string::npos) {
        wind_states_.push_back({12.0, 1.0, 0});
        return;
    }
    if (id_.find("_case_b_") != std::string::npos) {
        for (int direction = 0; direction < direction_count; ++direction) {
            wind_states_.push_back({
                12.0,
                1.0 / static_cast<double>(direction_count),
                direction,
            });
        }
        return;
    }
    const std::array<double, 3> speeds{8.0, 12.0, 17.0};
    for (int direction = 0; direction < direction_count; ++direction) {
        const auto probabilities = direction < 27
            ? std::array<double, 3>{0.004, 0.009, 0.011}
            : case_c_tail_probabilities[
                static_cast<std::size_t>(direction - 27)
            ];
        for (int speed = 0; speed < 3; ++speed) {
            wind_states_.push_back({
                speeds[static_cast<std::size_t>(speed)],
                probabilities[static_cast<std::size_t>(speed)],
                direction,
            });
        }
    }
}

void Problem::precompute_deficits() {
    const int cell_count = grid_size_ * grid_size_;
    deficit_square_.assign(
        static_cast<std::size_t>(direction_count)
            * static_cast<std::size_t>(cell_count)
            * static_cast<std::size_t>(cell_count),
        0.0
    );
    const double cell_width =
        farm_width_m / static_cast<double>(grid_size_);
    for (int direction = 0; direction < direction_count; ++direction) {
        const double angle = static_cast<double>(direction) * 10.0
            * std::numbers::pi / 180.0;
        const double flow_x = std::sin(angle);
        const double flow_y = std::cos(angle);
        const double cross_x = std::cos(angle);
        const double cross_y = -std::sin(angle);
        for (int source = 0; source < cell_count; ++source) {
            const int source_x = source % grid_size_;
            const int source_y = source / grid_size_;
            for (int target = 0; target < cell_count; ++target) {
                if (target == source) continue;
                const int target_x = target % grid_size_;
                const int target_y = target / grid_size_;
                const double dx =
                    static_cast<double>(target_x - source_x) * cell_width;
                const double dy =
                    static_cast<double>(target_y - source_y) * cell_width;
                const double downstream = dx * flow_x + dy * flow_y;
                const double crosswind = dx * cross_x + dy * cross_y;
                const double deficit = gaussian_deficit_ratio(
                    downstream, crosswind
                );
                deficit_square_[deficit_index(
                    direction, cell_count, source, target
                )] = deficit * deficit;
            }
        }
    }
}

Evaluation Problem::evaluate(const std::vector<int>& coordinates) const {
    Evaluation result;
    if (static_cast<int>(coordinates.size()) != variable_count()) {
        result.objective = std::numeric_limits<double>::infinity();
        result.total_normalized_constraint_violation =
            static_cast<double>(
                std::abs(
                    static_cast<int>(coordinates.size()) - variable_count()
                )
            );
        return result;
    }
    const double cell_width =
        farm_width_m / static_cast<double>(grid_size_);
    std::vector<int> cells(static_cast<std::size_t>(turbine_count_));
    for (int turbine = 0; turbine < turbine_count_; ++turbine) {
        const int x = coordinates[static_cast<std::size_t>(2 * turbine)];
        const int y = coordinates[static_cast<std::size_t>(2 * turbine + 1)];
        if (x < 0 || x >= grid_size_ || y < 0 || y >= grid_size_) {
            result.total_normalized_constraint_violation += 1.0;
        }
        const int bounded_x = std::clamp(x, 0, grid_size_ - 1);
        const int bounded_y = std::clamp(y, 0, grid_size_ - 1);
        cells[static_cast<std::size_t>(turbine)] =
            bounded_y * grid_size_ + bounded_x;
    }
    for (int left = 0; left < turbine_count_; ++left) {
        const int left_x = coordinates[static_cast<std::size_t>(2 * left)];
        const int left_y = coordinates[
            static_cast<std::size_t>(2 * left + 1)
        ];
        for (int right = left + 1; right < turbine_count_; ++right) {
            const int right_x =
                coordinates[static_cast<std::size_t>(2 * right)];
            const int right_y =
                coordinates[static_cast<std::size_t>(2 * right + 1)];
            const double distance = cell_width * std::hypot(
                static_cast<double>(left_x - right_x),
                static_cast<double>(left_y - right_y)
            );
            result.total_normalized_constraint_violation += std::max(
                0.0, (minimum_spacing_m - distance) / minimum_spacing_m
            );
        }
    }
    result.feasible =
        result.total_normalized_constraint_violation <= 1.0e-12;

    const int cell_count = grid_size_ * grid_size_;
    std::vector<double> effective_ratio(
        static_cast<std::size_t>(direction_count)
            * static_cast<std::size_t>(turbine_count_),
        1.0
    );
    for (int direction = 0; direction < direction_count; ++direction) {
        for (int target = 0; target < turbine_count_; ++target) {
            const int target_cell = cells[static_cast<std::size_t>(target)];
            double sum_square = 0.0;
            for (int source = 0; source < turbine_count_; ++source) {
                if (source == target) continue;
                const int source_cell =
                    cells[static_cast<std::size_t>(source)];
                sum_square += deficit_square_[deficit_index(
                    direction, cell_count, source_cell, target_cell
                )];
            }
            effective_ratio[
                static_cast<std::size_t>(direction * turbine_count_ + target)
            ] = std::max(0.0, 1.0 - std::sqrt(sum_square));
        }
    }
    for (const WindState& state : wind_states_) {
        double farm_power = 0.0;
        const std::size_t offset =
            static_cast<std::size_t>(state.direction * turbine_count_);
        for (int turbine = 0; turbine < turbine_count_; ++turbine) {
            farm_power += turbine_power_kw(
                state.speed_mps
                * effective_ratio[offset + static_cast<std::size_t>(turbine)]
            );
        }
        result.expected_power_kw += state.probability * farm_power;
    }
    result.objective = farm_cost(turbine_count_)
        / std::max(result.expected_power_kw, 1.0e-300);
    result.efficiency = result.expected_power_kw / no_wake_power_kw_;
    return result;
}

std::vector<Evaluation> Problem::evaluate_population(
    const std::vector<std::vector<int>>& population,
    fode::PersistentExecutor& executor
) const {
    std::vector<Evaluation> result(population.size());
    executor.parallel_for(
        0,
        static_cast<int>(population.size()),
        [&](const int index) {
            result[static_cast<std::size_t>(index)] = evaluate(
                population[static_cast<std::size_t>(index)]
            );
        }
    );
    return result;
}

RunResult run(const Problem& problem, const RunConfig& config) {
    if (
        config.workers < 1 || config.population < 2
        || config.population % 2 != 0 || config.generations < 0
    ) {
        throw std::invalid_argument("invalid L0124 run configuration");
    }
    const auto run_started = Clock::now();
    fode::PersistentExecutor executor(config.workers);
    fode::CounterRng random(config.seed);
    const int variables = problem.variable_count();
    const int grid_size = problem.grid_size();
    std::vector<std::vector<int>> population(
        static_cast<std::size_t>(config.population),
        std::vector<int>(static_cast<std::size_t>(variables))
    );
    for (int individual = 0; individual < config.population; ++individual) {
        for (int coordinate = 0; coordinate < variables; ++coordinate) {
            population[static_cast<std::size_t>(individual)][
                static_cast<std::size_t>(coordinate)
            ] = random.integer(
                0, grid_size, 0, 1,
                static_cast<std::uint64_t>(individual),
                static_cast<std::uint64_t>(coordinate)
            );
        }
    }
    std::vector<std::vector<int>> offspring(
        static_cast<std::size_t>(config.population),
        std::vector<int>(static_cast<std::size_t>(variables))
    );
    std::vector<int> mating(static_cast<std::size_t>(config.population));
    std::vector<int> elite_indices(static_cast<std::size_t>(config.population));
    std::array<std::vector<int>, tournament_size> tournament_candidates;
    for (auto& candidates : tournament_candidates) {
        candidates.resize(static_cast<std::size_t>(config.population));
    }

    RunResult result;
    result.problem_id = problem.id();
    result.problem_semantic_id = problem.semantic_id();
    result.method_semantic_id =
        "l0124_mi_lxpm_target_survival_completed_v1";
    result.seed = config.seed;
    result.requested_workers = config.workers;
    result.population = config.population;
    result.generations = config.generations;
    Evaluation best;
    std::vector<int> best_coordinates;
    bool has_best = false;

    auto evaluate_current = [&]() {
        const auto started = Clock::now();
        executor.reset_work_receipt();
        std::vector<Evaluation> evaluations =
            problem.evaluate_population(population, executor);
        result.evaluator_seconds += std::chrono::duration<double>(
            Clock::now() - started
        ).count();
        const auto receipt = executor.work_receipt();
        result.observed_workers = std::max(
            result.observed_workers, receipt.peak_region_participants
        );
        result.physical_fes +=
            static_cast<std::uint64_t>(config.population);
        return evaluations;
    };

    std::vector<Evaluation> evaluations = evaluate_current();
    auto update_best = [&]() {
        for (int individual = 0; individual < config.population; ++individual) {
            const auto& candidate =
                evaluations[static_cast<std::size_t>(individual)];
            const auto& candidate_coordinates =
                population[static_cast<std::size_t>(individual)];
            if (
                !has_best || better(
                    candidate, candidate_coordinates, best, best_coordinates
                )
            ) {
                best = candidate;
                best_coordinates = candidate_coordinates;
                has_best = true;
            }
        }
        result.best_objective_history.push_back(best.objective);
    };
    update_best();

    for (int generation = 1; generation <= config.generations; ++generation) {
        std::iota(elite_indices.begin(), elite_indices.end(), 0);
        const int elite_count = std::max(
            1,
            static_cast<int>(std::floor(
                elite_fraction * static_cast<double>(config.population)
            ))
        );
        std::partial_sort(
            elite_indices.begin(),
            elite_indices.begin() + elite_count,
            elite_indices.end(),
            [&](const int left, const int right) {
                return better(
                    evaluations[static_cast<std::size_t>(left)],
                    population[static_cast<std::size_t>(left)],
                    evaluations[static_cast<std::size_t>(right)],
                    population[static_cast<std::size_t>(right)]
                );
            }
        );
        // Deep et al. require systematic tournaments: every individual
        // participates exactly k times. Each round is therefore an independent
        // permutation, rather than sampling competitors with replacement.
        for (int round = 0; round < tournament_size; ++round) {
            auto& candidates =
                tournament_candidates[static_cast<std::size_t>(round)];
            std::iota(candidates.begin(), candidates.end(), 0);
            for (int index = config.population - 1; index > 0; --index) {
                const int other = random.integer(
                    0,
                    index + 1,
                    static_cast<std::uint64_t>(generation),
                    2,
                    static_cast<std::uint64_t>(round),
                    static_cast<std::uint64_t>(index)
                );
                std::swap(
                    candidates[static_cast<std::size_t>(index)],
                    candidates[static_cast<std::size_t>(other)]
                );
            }
        }
        executor.parallel_for(
            0,
            config.population,
            [&](const int slot) {
                int winner = tournament_candidates[0][
                    static_cast<std::size_t>(slot)
                ];
                for (int draw = 1; draw < tournament_size; ++draw) {
                    const int candidate = tournament_candidates[
                        static_cast<std::size_t>(draw)
                    ][static_cast<std::size_t>(slot)];
                    if (better(
                        evaluations[static_cast<std::size_t>(candidate)],
                        population[static_cast<std::size_t>(candidate)],
                        evaluations[static_cast<std::size_t>(winner)],
                        population[static_cast<std::size_t>(winner)]
                    )) {
                        winner = candidate;
                    }
                }
                mating[static_cast<std::size_t>(slot)] = winner;
            }
        );

        executor.parallel_for(
            0,
            (config.population + 1) / 2,
            [&](const int pair_index) {
                const int first = 2 * pair_index;
                const int second = std::min(first + 1, config.population - 1);
                offspring[static_cast<std::size_t>(first)] =
                    population[static_cast<std::size_t>(
                        mating[static_cast<std::size_t>(first)]
                    )];
                offspring[static_cast<std::size_t>(second)] =
                    population[static_cast<std::size_t>(
                        mating[static_cast<std::size_t>(second)]
                    )];
                const double cross_draw = random.uniform(
                    static_cast<std::uint64_t>(generation),
                    3,
                    static_cast<std::uint64_t>(pair_index)
                );
                if (cross_draw < crossover_probability) {
                    for (int coordinate = 0; coordinate < variables;
                         ++coordinate) {
                        const double u = std::max(
                            random.uniform(
                                static_cast<std::uint64_t>(generation),
                                3,
                                static_cast<std::uint64_t>(pair_index),
                                static_cast<std::uint64_t>(coordinate),
                                1
                            ),
                            std::numeric_limits<double>::min()
                        );
                        const double sign_draw = random.uniform(
                            static_cast<std::uint64_t>(generation),
                            3,
                            static_cast<std::uint64_t>(pair_index),
                            static_cast<std::uint64_t>(coordinate),
                            2
                        );
                        const double beta = sign_draw <= 0.5
                            ? laplace_location
                                - laplace_integer_scale * std::log(u)
                            : laplace_location
                                + laplace_integer_scale * std::log(u);
                        const double first_parent = static_cast<double>(
                            offspring[static_cast<std::size_t>(first)][
                                static_cast<std::size_t>(coordinate)
                            ]
                        );
                        const double second_parent = static_cast<double>(
                            offspring[static_cast<std::size_t>(second)][
                                static_cast<std::size_t>(coordinate)
                            ]
                        );
                        const double difference =
                            std::abs(first_parent - second_parent);
                        offspring[static_cast<std::size_t>(first)][
                            static_cast<std::size_t>(coordinate)
                        ] = stochastic_integer(
                            first_parent + beta * difference,
                            grid_size,
                            random.uniform(
                                static_cast<std::uint64_t>(generation),
                                3,
                                static_cast<std::uint64_t>(pair_index),
                                static_cast<std::uint64_t>(coordinate),
                                3
                            )
                        );
                        offspring[static_cast<std::size_t>(second)][
                            static_cast<std::size_t>(coordinate)
                        ] = stochastic_integer(
                            second_parent + beta * difference,
                            grid_size,
                            random.uniform(
                                static_cast<std::uint64_t>(generation),
                                3,
                                static_cast<std::uint64_t>(pair_index),
                                static_cast<std::uint64_t>(coordinate),
                                4
                            )
                        );
                    }
                }
            }
        );

        executor.parallel_for(
            0,
            config.population,
            [&](const int individual) {
                auto& child = offspring[static_cast<std::size_t>(individual)];
                for (int coordinate = 0; coordinate < variables;
                     ++coordinate) {
                    if (
                        random.uniform(
                            static_cast<std::uint64_t>(generation),
                            4,
                            static_cast<std::uint64_t>(individual),
                            static_cast<std::uint64_t>(coordinate),
                            0
                        ) >= mutation_probability
                    ) {
                        continue;
                    }
                    const double current = static_cast<double>(
                        child[static_cast<std::size_t>(coordinate)]
                    );
                    const double s = std::pow(
                        random.uniform(
                            static_cast<std::uint64_t>(generation),
                            4,
                            static_cast<std::uint64_t>(individual),
                            static_cast<std::uint64_t>(coordinate),
                            1
                        ),
                        power_integer_index
                    );
                    const double upper =
                        static_cast<double>(grid_size - 1);
                    const double threshold = upper > 0.0
                        ? current / upper
                        : 0.0;
                    const double direction_draw = random.uniform(
                        static_cast<std::uint64_t>(generation),
                        4,
                        static_cast<std::uint64_t>(individual),
                        static_cast<std::uint64_t>(coordinate),
                        2
                    );
                    const double mutated = threshold < direction_draw
                        ? current - s * current
                        : current + s * (upper - current);
                    child[static_cast<std::size_t>(coordinate)] =
                        stochastic_integer(
                            mutated,
                            grid_size,
                            random.uniform(
                                static_cast<std::uint64_t>(generation),
                                4,
                                static_cast<std::uint64_t>(individual),
                                static_cast<std::uint64_t>(coordinate),
                                3
                            )
                        );
                }
            }
        );
        executor.parallel_for(
            0,
            elite_count,
            [&](const int elite) {
                offspring[static_cast<std::size_t>(elite)] =
                    population[static_cast<std::size_t>(
                        elite_indices[static_cast<std::size_t>(elite)]
                    )];
            }
        );
        population.swap(offspring);
        evaluations = evaluate_current();
        update_best();
    }

    result.best_evaluation = best;
    result.best_coordinates = std::move(best_coordinates);
    result.end_to_end_seconds = std::chrono::duration<double>(
        Clock::now() - run_started
    ).count();
    result.algorithm_seconds = std::max(
        0.0, result.end_to_end_seconds - result.evaluator_seconds
    );
    std::uint64_t hash = 1469598103934665603ULL;
    for (const int coordinate : result.best_coordinates) {
        hash = mix_hash(hash, static_cast<std::uint64_t>(coordinate));
    }
    hash = mix_hash(
        hash, std::bit_cast<std::uint64_t>(result.best_evaluation.objective)
    );
    hash = mix_hash(hash, result.physical_fes);
    result.scientific_hash = hash;
    return result;
}

}  // namespace core99::l0124
