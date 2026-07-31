/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T03 evaluator, strength-Pareto ES, and HPC execution
Paper title and DOI: Design of Wind Farm Layout for Maximum Wind Energy
Capture, 10.1016/j.renene.2009.08.019.
Public source: no author implementation was located.
Missing fields and Reconstruction:
include/core99/kusiak_t03.hpp
Semantic IDs and Contract: shared/contracts/core99_t03_kusiak_cases.json.
Independent equation oracle: scripts/validate_core99_t03.py
HPC design: schedule-independent parallel initialization/mutation/evaluation,
persistent workers, stack-local wake reductions, fixed-order Pareto commits
Claim boundary: academic declared reconstruction, not author-source or
author-exact numerical reproduction.
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "core99/kusiak_t03.hpp"

#include "fode/rng.hpp"

#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <utility>

namespace core99::t03 {
namespace {

using Clock = std::chrono::steady_clock;

constexpr double kFarmRadius = 500.0;
constexpr double kRotorRadius = 38.5;
constexpr double kMinimumSpacing = 8.0 * kRotorRadius;
constexpr double kThrustCoefficient = 0.8;
constexpr double kWakeExpansion = 0.075;
constexpr int kChildren = 120;
constexpr int kParents = 20;
constexpr int kArchiveLimit = 50;
constexpr int kTournament = 4;

struct Genome {
    std::vector<Point> points;
    std::vector<double> sigma;
};

struct Individual {
    Genome genome;
    Evaluation value;
};

double weibull_survival(double speed, double shape, double scale) {
    return std::exp(-std::pow(speed / scale, shape));
}

double expected_single_turbine_power(
    double shape,
    double scale
) {
    if (!(scale > 0.0)) {
        return 0.0;
    }
    double result = 0.0;
    for (int bin = 0; bin < 21; ++bin) {
        const double low = 3.5 + 0.5 * static_cast<double>(bin);
        const double high = low + 0.5;
        const double midpoint = 0.5 * (low + high);
        const double probability =
            weibull_survival(low, shape, scale)
            - weibull_survival(high, shape, scale);
        result += probability * std::max(0.0, 140.86 * midpoint - 500.0);
    }
    result += 1500.0 * weibull_survival(14.0, shape, scale);
    return result;
}

bool dominates(const Evaluation& left, const Evaluation& right) {
    const bool no_worse =
        left.inverse_power <= right.inverse_power
        && left.constraint_violation <= right.constraint_violation;
    const bool strictly_better =
        left.inverse_power < right.inverse_power
        || left.constraint_violation < right.constraint_violation;
    return no_worse && strictly_better;
}

bool reporting_better(const Individual& left, const Individual& right) {
    const bool left_feasible = left.value.constraint_violation <= 1.0e-12;
    const bool right_feasible = right.value.constraint_violation <= 1.0e-12;
    if (left_feasible != right_feasible) {
        return left_feasible;
    }
    if (
        left.value.constraint_violation
        != right.value.constraint_violation
    ) {
        return left.value.constraint_violation
            < right.value.constraint_violation;
    }
    if (left.value.inverse_power != right.value.inverse_power) {
        return left.value.inverse_power < right.value.inverse_power;
    }
    for (std::size_t index = 0; index < left.genome.points.size(); ++index) {
        if (left.genome.points[index].x != right.genome.points[index].x) {
            return left.genome.points[index].x
                < right.genome.points[index].x;
        }
        if (left.genome.points[index].y != right.genome.points[index].y) {
            return left.genome.points[index].y
                < right.genome.points[index].y;
        }
    }
    return false;
}

std::vector<int> raw_strength_scores(
    const std::vector<Individual>& pool
) {
    std::vector<int> strength(pool.size(), 0);
    std::vector<int> raw(pool.size(), 0);
    for (std::size_t left = 0; left < pool.size(); ++left) {
        for (std::size_t right = 0; right < pool.size(); ++right) {
            if (dominates(pool[left].value, pool[right].value)) {
                ++strength[left];
            }
        }
    }
    for (std::size_t target = 0; target < pool.size(); ++target) {
        for (std::size_t source = 0; source < pool.size(); ++source) {
            if (dominates(pool[source].value, pool[target].value)) {
                raw[target] += strength[source];
            }
        }
    }
    return raw;
}

std::vector<Individual> archive_from(
    const std::vector<Individual>& pool
) {
    std::vector<Individual> nondominated;
    for (std::size_t candidate = 0; candidate < pool.size(); ++candidate) {
        bool dominated = false;
        for (std::size_t other = 0; other < pool.size(); ++other) {
            if (
                candidate != other
                && dominates(pool[other].value, pool[candidate].value)
            ) {
                dominated = true;
                break;
            }
        }
        if (!dominated) {
            nondominated.push_back(pool[candidate]);
        }
    }
    std::stable_sort(
        nondominated.begin(),
        nondominated.end(),
        [](const Individual& left, const Individual& right) {
            if (left.value.inverse_power != right.value.inverse_power) {
                return left.value.inverse_power < right.value.inverse_power;
            }
            return left.value.constraint_violation
                < right.value.constraint_violation;
        }
    );
    if (nondominated.size() <= kArchiveLimit) {
        return nondominated;
    }
    std::vector<Individual> spread;
    spread.reserve(kArchiveLimit);
    for (int slot = 0; slot < kArchiveLimit; ++slot) {
        const std::size_t source = static_cast<std::size_t>(
            std::llround(
                static_cast<double>(slot)
                * static_cast<double>(nondominated.size() - 1)
                / static_cast<double>(kArchiveLimit - 1)
            )
        );
        spread.push_back(nondominated[source]);
    }
    return spread;
}

std::uint64_t layout_hash(const Individual& best) {
    std::uint64_t hash = 1469598103934665603ULL;
    auto mix = [&](std::uint64_t value) {
        hash ^= value;
        hash *= 1099511628211ULL;
    };
    for (const Point& point : best.genome.points) {
        mix(std::bit_cast<std::uint64_t>(point.x));
        mix(std::bit_cast<std::uint64_t>(point.y));
    }
    mix(std::bit_cast<std::uint64_t>(best.value.expected_power_kw));
    return hash;
}

}  // namespace

Problem::Problem(std::string problem_id) : id_(std::move(problem_id)) {
    const std::string prefix = "t03_kusiak_s";
    const std::string marker = "_n";
    if (!id_.starts_with(prefix)) {
        throw std::invalid_argument("unknown T03 problem: " + id_);
    }
    const std::size_t marker_position = id_.find(marker, prefix.size());
    if (marker_position == std::string::npos) {
        throw std::invalid_argument("malformed T03 problem: " + id_);
    }
    scenario_ = std::stoi(
        id_.substr(prefix.size(), marker_position - prefix.size())
    );
    turbine_count_ = std::stoi(id_.substr(marker_position + marker.size()));
    if (
        (scenario_ != 1 && scenario_ != 2)
        || turbine_count_ < 2
        || turbine_count_ > 6
    ) {
        throw std::invalid_argument("T03 case outside paper range");
    }
    const double scenario1_probability[24]{
        0.0, 0.01, 0.01, 0.01, 0.01, 0.2,
        0.6, 0.01, 0.01, 0.01, 0.01, 0.01,
        0.01, 0.01, 0.01, 0.01, 0.01, 0.01,
        0.01, 0.01, 0.01, 0.01, 0.01, 0.0
    };
    const double scenario2_scale[24]{
        7.0, 5.0, 5.0, 5.0, 5.0, 4.0,
        5.0, 6.0, 7.0, 7.0, 8.0, 9.5,
        10.0, 8.5, 8.5, 6.5, 4.6, 2.6,
        8.0, 5.0, 6.4, 5.2, 4.5, 3.9
    };
    const double scenario2_probability[24]{
        0.0002, 0.008, 0.0227, 0.0242, 0.0225, 0.0339,
        0.0423, 0.029, 0.0617, 0.0813, 0.0994, 0.1394,
        0.1839, 0.1115, 0.0765, 0.008, 0.0051, 0.0019,
        0.0012, 0.001, 0.0017, 0.0031, 0.0097, 0.0317
    };
    for (int bin = 0; bin < 24; ++bin) {
        wind_.push_back({
            7.5 + 15.0 * static_cast<double>(bin),
            2.0,
            scenario_ == 1 ? 13.0 : scenario2_scale[bin],
            scenario_ == 1
                ? scenario1_probability[bin]
                : scenario2_probability[bin]
        });
    }
}

const std::string& Problem::id() const noexcept {
    return id_;
}

int Problem::turbine_count() const noexcept {
    return turbine_count_;
}

int Problem::scenario() const noexcept {
    return scenario_;
}

Evaluation Problem::evaluate(const std::vector<Point>& layout) const {
    if (static_cast<int>(layout.size()) != turbine_count_) {
        throw std::invalid_argument("T03 layout size mismatch");
    }
    double violation = 0.0;
    for (const Point& point : layout) {
        violation += std::max(
            0.0,
            point.x * point.x + point.y * point.y
                - kFarmRadius * kFarmRadius
        );
    }
    for (int left = 0; left < turbine_count_; ++left) {
        for (int right = left + 1; right < turbine_count_; ++right) {
            const double dx = layout[left].x - layout[right].x;
            const double dy = layout[left].y - layout[right].y;
            violation += std::max(
                0.0,
                kMinimumSpacing * kMinimumSpacing - dx * dx - dy * dy
            );
        }
    }
    const double induction =
        0.5 * (1.0 - std::sqrt(1.0 - kThrustCoefficient));
    double expected_power = 0.0;
    for (const WindBin& bin : wind_) {
        if (!(bin.probability > 0.0)) {
            continue;
        }
        const double angle =
            bin.direction_degrees * std::numbers::pi / 180.0;
        const double cosine = std::cos(angle);
        const double sine = std::sin(angle);
        double bin_power = 0.0;
        for (int target = 0; target < turbine_count_; ++target) {
            double deficit_squared = 0.0;
            for (int source = 0; source < turbine_count_; ++source) {
                if (source == target) {
                    continue;
                }
                const double dx = layout[target].x - layout[source].x;
                const double dy = layout[target].y - layout[source].y;
                const double downstream = cosine * dx + sine * dy;
                if (!(downstream > 0.0)) {
                    continue;
                }
                const double crosswind = std::abs(-sine * dx + cosine * dy);
                if (crosswind > kRotorRadius + kWakeExpansion * downstream) {
                    continue;
                }
                const double deficit =
                    2.0 * induction
                    / std::pow(
                        1.0 + kWakeExpansion * downstream / kRotorRadius,
                        2.0
                    );
                deficit_squared += deficit * deficit;
            }
            const double retained = std::max(
                0.0,
                1.0 - std::sqrt(deficit_squared)
            );
            bin_power += expected_single_turbine_power(
                bin.weibull_shape,
                bin.weibull_scale * retained
            );
        }
        expected_power += bin.probability * bin_power;
    }
    return {
        expected_power,
        expected_power > 0.0
            ? 1.0 / expected_power
            : std::numeric_limits<double>::infinity(),
        violation
    };
}

std::vector<Evaluation> Problem::evaluate_population(
    const std::vector<std::vector<Point>>& layouts,
    fode::PersistentExecutor& executor
) const {
    std::vector<Evaluation> values(layouts.size());
    executor.parallel_for(
        0,
        static_cast<int>(layouts.size()),
        [&](int index) {
            values[static_cast<std::size_t>(index)] = evaluate(
                layouts[static_cast<std::size_t>(index)]
            );
        }
    );
    return values;
}

RunResult run(
    const Problem& problem,
    std::uint64_t seed,
    std::uint64_t physical_fes,
    int workers
) {
    if (physical_fes < kChildren || workers <= 0) {
        throw std::invalid_argument("invalid T03 run request");
    }
    const auto start = Clock::now();
    fode::PersistentExecutor executor(workers);
    const fode::CounterRng rng(seed ^ 0x5403a912ULL);
    std::vector<Individual> offspring(kChildren);
    executor.parallel_for(0, kChildren, [&](int child) {
        Genome genome;
        genome.points.resize(problem.turbine_count());
        genome.sigma.resize(
            static_cast<std::size_t>(2 * problem.turbine_count())
        );
        for (int turbine = 0; turbine < problem.turbine_count(); ++turbine) {
            genome.points[static_cast<std::size_t>(turbine)] = {
                -kFarmRadius + 2.0 * kFarmRadius
                    * rng.uniform(0, 300, child, 2 * turbine),
                -kFarmRadius + 2.0 * kFarmRadius
                    * rng.uniform(0, 300, child, 2 * turbine + 1)
            };
            genome.sigma[static_cast<std::size_t>(2 * turbine)] =
                1.0 + 24.0 * rng.uniform(0, 301, child, 2 * turbine);
            genome.sigma[static_cast<std::size_t>(2 * turbine + 1)] =
                1.0 + 24.0 * rng.uniform(0, 301, child, 2 * turbine + 1);
        }
        offspring[static_cast<std::size_t>(child)].genome =
            std::move(genome);
    });
    std::uint64_t fes = kChildren;
    double evaluator_seconds = 0.0;
    auto eval_start = Clock::now();
    {
        std::vector<std::vector<Point>> layouts;
        layouts.reserve(offspring.size());
        for (const Individual& individual : offspring) {
            layouts.push_back(individual.genome.points);
        }
        const auto values = problem.evaluate_population(layouts, executor);
        for (std::size_t index = 0; index < offspring.size(); ++index) {
            offspring[index].value = values[index];
        }
    }
    evaluator_seconds += std::chrono::duration<double>(
        Clock::now() - eval_start
    ).count();
    std::vector<Individual> archive;
    Individual best = *std::min_element(
        offspring.begin(),
        offspring.end(),
        [](const Individual& left, const Individual& right) {
            return reporting_better(left, right);
        }
    );
    std::uint64_t generation = 0;
    while (fes < physical_fes) {
        std::vector<Individual> pool = offspring;
        pool.insert(pool.end(), archive.begin(), archive.end());
        archive = archive_from(pool);
        const std::vector<int> score = raw_strength_scores(pool);
        std::vector<int> parents(kParents);
        for (int slot = 0; slot < kParents; ++slot) {
            int winner = rng.integer(
                0,
                static_cast<int>(pool.size()),
                generation + 1,
                400,
                slot,
                0
            );
            for (int draw = 1; draw < kTournament; ++draw) {
                const int candidate = rng.integer(
                    0,
                    static_cast<int>(pool.size()),
                    generation + 1,
                    400,
                    slot,
                    draw
                );
                if (
                    score[static_cast<std::size_t>(candidate)]
                        < score[static_cast<std::size_t>(winner)]
                    || (
                        score[static_cast<std::size_t>(candidate)]
                            == score[static_cast<std::size_t>(winner)]
                        && reporting_better(
                            pool[static_cast<std::size_t>(candidate)],
                            pool[static_cast<std::size_t>(winner)]
                        )
                    )
                ) {
                    winner = candidate;
                }
            }
            parents[static_cast<std::size_t>(slot)] = winner;
        }
        const int count = static_cast<int>(
            std::min<std::uint64_t>(kChildren, physical_fes - fes)
        );
        std::vector<Individual> next(static_cast<std::size_t>(count));
        const double tau0 = 1.0 / std::sqrt(
            4.0 * static_cast<double>(problem.turbine_count())
        );
        const double tau = 1.0 / std::sqrt(
            2.0 * std::sqrt(
                2.0 * static_cast<double>(problem.turbine_count())
            )
        );
        executor.parallel_for(0, count, [&](int child) {
            const int parent_a = parents[static_cast<std::size_t>(
                rng.integer(0, kParents, generation + 1, 401, child, 0)
            )];
            const int parent_b = parents[static_cast<std::size_t>(
                rng.integer(0, kParents, generation + 1, 401, child, 1)
            )];
            Genome genome;
            genome.points.resize(problem.turbine_count());
            genome.sigma.resize(
                static_cast<std::size_t>(2 * problem.turbine_count())
            );
            const double global_normal =
                rng.normal(generation + 1, 402, child, 0);
            for (int coordinate = 0;
                 coordinate < 2 * problem.turbine_count();
                 ++coordinate) {
                const int turbine = coordinate / 2;
                const bool x_axis = coordinate % 2 == 0;
                const double parent_coordinate_a = x_axis
                    ? pool[static_cast<std::size_t>(parent_a)]
                          .genome.points[static_cast<std::size_t>(turbine)].x
                    : pool[static_cast<std::size_t>(parent_a)]
                          .genome.points[static_cast<std::size_t>(turbine)].y;
                const double parent_coordinate_b = x_axis
                    ? pool[static_cast<std::size_t>(parent_b)]
                          .genome.points[static_cast<std::size_t>(turbine)].x
                    : pool[static_cast<std::size_t>(parent_b)]
                          .genome.points[static_cast<std::size_t>(turbine)].y;
                const double parent_sigma =
                    0.5 * (
                        pool[static_cast<std::size_t>(parent_a)]
                            .genome.sigma[static_cast<std::size_t>(coordinate)]
                        + pool[static_cast<std::size_t>(parent_b)]
                            .genome.sigma[static_cast<std::size_t>(coordinate)]
                    );
                const double sigma = std::clamp(
                    parent_sigma * std::exp(
                        tau0 * global_normal
                        + tau * rng.normal(
                            generation + 1,
                            403,
                            child,
                            coordinate
                        )
                    ),
                    1.0,
                    25.0
                );
                const double coordinate_value = std::clamp(
                    0.5 * (parent_coordinate_a + parent_coordinate_b)
                        + sigma * rng.normal(
                            generation + 1,
                            404,
                            child,
                            coordinate
                        ),
                    -kFarmRadius,
                    kFarmRadius
                );
                genome.sigma[static_cast<std::size_t>(coordinate)] = sigma;
                if (x_axis) {
                    genome.points[static_cast<std::size_t>(turbine)].x =
                        coordinate_value;
                } else {
                    genome.points[static_cast<std::size_t>(turbine)].y =
                        coordinate_value;
                }
            }
            next[static_cast<std::size_t>(child)].genome =
                std::move(genome);
        });
        eval_start = Clock::now();
        {
            std::vector<std::vector<Point>> layouts;
            layouts.reserve(next.size());
            for (const Individual& individual : next) {
                layouts.push_back(individual.genome.points);
            }
            const auto values = problem.evaluate_population(layouts, executor);
            for (std::size_t index = 0; index < next.size(); ++index) {
                next[index].value = values[index];
            }
        }
        evaluator_seconds += std::chrono::duration<double>(
            Clock::now() - eval_start
        ).count();
        for (const Individual& individual : next) {
            if (reporting_better(individual, best)) {
                best = individual;
            }
        }
        offspring = std::move(next);
        fes += static_cast<std::uint64_t>(count);
        ++generation;
    }
    const double end_to_end = std::chrono::duration<double>(
        Clock::now() - start
    ).count();
    return {
        problem.id(),
        best.genome.points,
        best.value,
        seed,
        fes,
        workers,
        executor.thread_count(),
        evaluator_seconds,
        std::max(0.0, end_to_end - evaluator_seconds),
        end_to_end,
        layout_hash(best) ^ fes
    };
}

}  // namespace core99::t03
