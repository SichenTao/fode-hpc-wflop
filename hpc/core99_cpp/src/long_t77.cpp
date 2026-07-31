/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T77 pure-C++ ADE-GRNN, evaluator, and HPC execution
Paper title: A Data-Driven Evolutionary Algorithm for Wind Farm Layout
Optimization
Paper DOI: 10.1016/j.energy.2020.118310
Public source: no paper-linked author code or data archive found.
Missing, conflicts, declared completions, semantic identities, production
parallelism, and claim boundary: include/core99/long_t77.hpp.
Independent equation oracle: scripts/validate_core99_t77.py.
Contract: shared/contracts/core99_t77_long_ade_grnn_2020.json.
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "core99/long_t77.hpp"

#include "fode/rng.hpp"

#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <limits>
#include <numeric>
#include <numbers>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace core99::t77 {
namespace {

using Clock = std::chrono::steady_clock;
constexpr double rotor_radius_m = 40.0;
constexpr double minimum_spacing_m = 5.0 * rotor_radius_m;
constexpr double thrust_coefficient = 0.8;
constexpr double wake_expansion = 0.01;
constexpr double cut_in_mps = 3.5;
constexpr double rated_mps = 14.0;
constexpr double cut_out_mps = 25.0;
constexpr double rated_power_kw = 1500.0;
constexpr double power_a = 6.0268;
constexpr double power_b = 0.0007;
constexpr double grnn_sigma = 0.01;
constexpr double normal_standard_deviation = 1.0;
constexpr const char* method_id =
    "t77_ade_grnn_paper_first_declared_v1";
constexpr const char* protocol_id =
    "t77_18_cases_5_runs_pop40_gen3750_v1";

double elapsed(const Clock::time_point start) {
    return std::chrono::duration<double>(Clock::now() - start).count();
}

double logistic_power(const double speed_mps) {
    const double exponential = std::exp(speed_mps);
    return exponential / (power_a + power_b * exponential);
}

double weibull_survival(
    const double speed_mps,
    const double shape,
    const double scale
) {
    if (!(scale > 0.0)) return 0.0;
    return std::exp(-std::pow(speed_mps / scale, shape));
}

double expected_turbine_power(
    const double shape,
    const double scale
) {
    if (!(scale > 0.0)) return 0.0;
    double power = 0.0;
    constexpr int speed_bins = 21;
    constexpr double width = (rated_mps - cut_in_mps) / speed_bins;
    for (int bin = 0; bin < speed_bins; ++bin) {
        const double low = cut_in_mps + width * bin;
        const double high = low + width;
        const double probability =
            weibull_survival(low, shape, scale)
            - weibull_survival(high, shape, scale);
        power += probability * logistic_power(0.5 * (low + high));
    }
    power += rated_power_kw * (
        weibull_survival(rated_mps, shape, scale)
        - weibull_survival(cut_out_mps, shape, scale)
    );
    return power;
}

bool better(const Evaluation& left, const Evaluation& right) {
    const bool left_feasible = left.constraint_violation_m <= 1.0e-9;
    const bool right_feasible = right.constraint_violation_m <= 1.0e-9;
    if (left_feasible != right_feasible) return left_feasible;
    if (left.constraint_violation_m != right.constraint_violation_m) {
        return left.constraint_violation_m < right.constraint_violation_m;
    }
    return left.expected_power_kw > right.expected_power_kw;
}

std::uint64_t mix_hash(std::uint64_t h, const std::uint64_t value) {
    h ^= value + 0x9e3779b97f4a7c15ULL + (h << 6U) + (h >> 2U);
    return h;
}

std::vector<Point> random_layout(
    const Problem& problem,
    const fode::CounterRng& random,
    const std::uint64_t individual
) {
    std::vector<Point> result;
    result.reserve(static_cast<std::size_t>(problem.turbine_count()));
    const double low = rotor_radius_m;
    const double width = problem.farm_side_m() - 2.0 * rotor_radius_m;
    for (int turbine = 0; turbine < problem.turbine_count(); ++turbine) {
        bool placed = false;
        for (int attempt = 0; attempt < 20000; ++attempt) {
            const Point candidate{
                low + width * random.uniform(
                    0, 100, individual, turbine, 2 * attempt
                ),
                low + width * random.uniform(
                    0, 100, individual, turbine, 2 * attempt + 1
                ),
            };
            bool valid = true;
            for (const Point& existing : result) {
                if (std::hypot(
                        candidate.x_m - existing.x_m,
                        candidate.y_m - existing.y_m
                    ) < minimum_spacing_m) {
                    valid = false;
                    break;
                }
            }
            if (valid) {
                result.push_back(candidate);
                placed = true;
                break;
            }
        }
        if (!placed) {
            throw std::runtime_error("T77 feasible initialization exhausted");
        }
    }
    return result;
}

std::vector<int> random_permutation(
    const int count,
    const fode::CounterRng& random,
    const std::uint64_t generation,
    const std::uint64_t phase,
    const std::uint64_t individual
) {
    std::vector<int> result(static_cast<std::size_t>(count));
    std::iota(result.begin(), result.end(), 0);
    for (int index = count - 1; index > 0; --index) {
        const int other = random.integer(
            0,
            index + 1,
            generation,
            phase,
            individual,
            static_cast<std::uint64_t>(index)
        );
        std::swap(result[index], result[other]);
    }
    return result;
}

class Grnn {
public:
    Grnn(const int capacity, const int dimensions)
        : capacity_(capacity),
          dimensions_(dimensions),
          features_(
              static_cast<std::size_t>(capacity) * dimensions,
              0.0
          ),
          outputs_(static_cast<std::size_t>(capacity), 0.0) {}

    void append(const std::vector<double>& features, const double output) {
        if (static_cast<int>(features.size()) != dimensions_) {
            throw std::invalid_argument("T77 GRNN feature dimension");
        }
        int slot = 0;
        if (size_ < capacity_) {
            slot = size_;
            ++size_;
        } else {
            slot = next_;
            next_ = (next_ + 1) % capacity_;
        }
        std::copy(
            features.begin(),
            features.end(),
            features_.begin()
                + static_cast<std::ptrdiff_t>(slot) * dimensions_
        );
        outputs_[slot] = output;
    }

    [[nodiscard]] int size() const noexcept { return size_; }

    [[nodiscard]] double predict(
        const std::vector<double>& query
    ) const {
        if (size_ < 1 || static_cast<int>(query.size()) != dimensions_) {
            throw std::runtime_error("T77 GRNN unavailable");
        }
        constexpr double denominator = 2.0 * grnn_sigma * grnn_sigma;
        double minimum_distance = std::numeric_limits<double>::infinity();
        double sum_weight = 0.0;
        double sum_weighted_output = 0.0;
        for (int sample = 0; sample < size_; ++sample) {
            const double* row = features_.data()
                + static_cast<std::ptrdiff_t>(sample) * dimensions_;
            double squared = 0.0;
            #if defined(__GNUC__)
            #pragma GCC ivdep
            #endif
            for (int coordinate = 0;
                 coordinate < dimensions_;
                 ++coordinate) {
                const double difference = query[coordinate] - row[coordinate];
                squared += difference * difference;
            }
            if (squared < minimum_distance) {
                if (std::isfinite(minimum_distance)) {
                    const double rescale = std::exp(
                        -(minimum_distance - squared) / denominator
                    );
                    sum_weight *= rescale;
                    sum_weighted_output *= rescale;
                }
                minimum_distance = squared;
                sum_weight += 1.0;
                sum_weighted_output += outputs_[sample];
            } else {
                const double weight = std::exp(
                    -(squared - minimum_distance) / denominator
                );
                sum_weight += weight;
                sum_weighted_output += weight * outputs_[sample];
            }
        }
        return sum_weighted_output / sum_weight;
    }

private:
    int capacity_ = 0;
    int dimensions_ = 0;
    int size_ = 0;
    int next_ = 0;
    std::vector<double> features_;
    std::vector<double> outputs_;
};

struct Individual {
    std::vector<Point> layout;
    Evaluation evaluation;
};

struct Trial {
    std::vector<Point> layout;
    Evaluation evaluation;
    std::vector<double> features;
    double prediction = -std::numeric_limits<double>::infinity();
    double f1 = 0.0;
    double f2 = 0.0;
    int moved = 0;
    bool exact = false;
};

int best_index(const std::vector<Individual>& population) {
    int result = 0;
    for (int index = 1; index < static_cast<int>(population.size()); ++index) {
        if (better(population[index].evaluation, population[result].evaluation)) {
            result = index;
        }
    }
    return result;
}

std::vector<Point> generate_trial(
    const Problem& problem,
    const std::vector<Individual>& population,
    const int best,
    const int target,
    const double f1,
    const double f2,
    const int moved,
    const fode::CounterRng& random,
    const std::uint64_t generation
) {
    const int count = static_cast<int>(population.size());
    const int source = random.integer(
        0, count, generation, 201, target
    );
    const auto disrupted = random_permutation(
        problem.turbine_count(), random, generation, 202, target
    );
    const auto selected = random_permutation(
        problem.turbine_count(), random, generation, 203, target
    );
    const double sign = random.uniform(generation, 204, target) < 0.5
        ? -1.0 : 1.0;
    std::vector<Point> displacement(
        static_cast<std::size_t>(problem.turbine_count())
    );
    for (int turbine = 0; turbine < problem.turbine_count(); ++turbine) {
        displacement[turbine] = {
            sign * (
                f1 * population[source].layout[turbine].x_m
                - f2
                    * population[source].layout[disrupted[turbine]].x_m
            ),
            sign * (
                f1 * population[source].layout[turbine].y_m
                - f2
                    * population[source].layout[disrupted[turbine]].y_m
            ),
        };
    }
    double scale = 1.0;
    for (int attempt = 0; attempt < 40; ++attempt) {
        std::vector<Point> candidate = population[target].layout;
        for (int slot = 0; slot < moved; ++slot) {
            const int turbine = selected[slot];
            candidate[turbine] = {
                population[best].layout[turbine].x_m
                    + scale * displacement[turbine].x_m,
                population[best].layout[turbine].y_m
                    + scale * displacement[turbine].y_m,
            };
        }
        if (problem.feasible(candidate)) return candidate;
        scale *= 0.5;
    }
    return population[target].layout;
}

template<typename Value>
void append_fifo(
    std::deque<Value>& values,
    const Value value,
    const int capacity
) {
    values.push_back(value);
    while (static_cast<int>(values.size()) > capacity) values.pop_front();
}

double mean_or(
    const std::deque<double>& values,
    const double fallback
) {
    if (values.empty()) return fallback;
    return std::accumulate(values.begin(), values.end(), 0.0)
        / static_cast<double>(values.size());
}

}  // namespace

Problem::Problem(const int scenario, const int turbine_count)
    : scenario_(scenario), turbine_count_(turbine_count) {
    if ((scenario_ != 1 && scenario_ != 2)
        || turbine_count_ < 15 || turbine_count_ > 100) {
        throw std::invalid_argument("T77 case outside paper range");
    }
    switch (turbine_count_) {
    case 15:
    case 20:
    case 25: farm_side_m_ = 2000.0; break;
    case 30: farm_side_m_ = 2200.0; break;
    case 35: farm_side_m_ = 2400.0; break;
    case 40: farm_side_m_ = 2600.0; break;
    case 60: farm_side_m_ = 3100.0; break;
    case 80: farm_side_m_ = 3600.0; break;
    case 100: farm_side_m_ = 4000.0; break;
    default:
        throw std::invalid_argument("T77 unsupported paper turbine count");
    }
    semantic_id_ = scenario_ == 1
        ? "t77_long_ws1_continuous_v1"
        : "t77_long_ws2_continuous_v1";
    constexpr double ws1_scale[24]{
        7,5,5,5,5,4,5,6,7,7,8,9.5,
        10,8.5,8.5,6.5,4.6,2.6,8,5,6.4,5.2,4.5,3.9
    };
    constexpr double ws1_probability[24]{
        .0003,.0072,.0237,.0242,.0222,.0301,
        .0397,.0268,.0626,.0801,.1025,.1445,
        .1909,.1162,.0793,.0082,.0041,.0008,
        .001,.0005,.0013,.0031,.0085,.0222
    };
    constexpr double ws2_probability[24]{
        0,.01,.01,.01,.01,.20,.60,.01,.01,.01,.01,.01,
        .01,.01,.01,.01,.01,.01,.01,.01,.01,.01,.01,0
    };
    for (int bin = 0; bin < 24; ++bin) {
        wind_.push_back({
            7.5 + 15.0 * bin,
            2.0,
            scenario_ == 1 ? ws1_scale[bin] : 13.0,
            scenario_ == 1
                ? ws1_probability[bin]
                : ws2_probability[bin],
        });
    }
}

int Problem::scenario() const noexcept { return scenario_; }
int Problem::turbine_count() const noexcept { return turbine_count_; }
double Problem::farm_side_m() const noexcept { return farm_side_m_; }
const std::string& Problem::semantic_id() const noexcept {
    return semantic_id_;
}

bool Problem::feasible(const std::vector<Point>& layout) const {
    if (static_cast<int>(layout.size()) != turbine_count_) return false;
    for (int target = 0; target < turbine_count_; ++target) {
        if (
            layout[target].x_m < rotor_radius_m
            || layout[target].x_m > farm_side_m_ - rotor_radius_m
            || layout[target].y_m < rotor_radius_m
            || layout[target].y_m > farm_side_m_ - rotor_radius_m
        ) {
            return false;
        }
        for (int source = 0; source < target; ++source) {
            if (std::hypot(
                    layout[target].x_m - layout[source].x_m,
                    layout[target].y_m - layout[source].y_m
                ) < minimum_spacing_m) {
                return false;
            }
        }
    }
    return true;
}

Evaluation Problem::evaluate(const std::vector<Point>& layout) const {
    if (static_cast<int>(layout.size()) != turbine_count_) {
        throw std::invalid_argument("T77 layout size");
    }
    double violation = 0.0;
    for (int target = 0; target < turbine_count_; ++target) {
        violation += std::max(
            0.0, rotor_radius_m - layout[target].x_m
        );
        violation += std::max(
            0.0, layout[target].x_m - (farm_side_m_ - rotor_radius_m)
        );
        violation += std::max(
            0.0, rotor_radius_m - layout[target].y_m
        );
        violation += std::max(
            0.0, layout[target].y_m - (farm_side_m_ - rotor_radius_m)
        );
        for (int source = 0; source < target; ++source) {
            violation += std::max(
                0.0,
                minimum_spacing_m - std::hypot(
                    layout[target].x_m - layout[source].x_m,
                    layout[target].y_m - layout[source].y_m
                )
            );
        }
    }
    const double induction = 0.5 * (
        1.0 - std::sqrt(1.0 - thrust_coefficient)
    );
    double total_power = 0.0;
    for (const auto& wind : wind_) {
        if (!(wind.probability > 0.0)) continue;
        const double angle =
            wind.direction_degrees * std::numbers::pi / 180.0;
        const double flow_x = std::cos(angle);
        const double flow_y = std::sin(angle);
        const double cross_x = -flow_y;
        const double cross_y = flow_x;
        double direction_power = 0.0;
        for (int target = 0; target < turbine_count_; ++target) {
            double deficit_squared = 0.0;
            for (int source = 0; source < turbine_count_; ++source) {
                if (source == target) continue;
                const double dx =
                    layout[target].x_m - layout[source].x_m;
                const double dy =
                    layout[target].y_m - layout[source].y_m;
                const double downstream = dx * flow_x + dy * flow_y;
                if (!(downstream > 0.0)) continue;
                const double crosswind =
                    std::abs(dx * cross_x + dy * cross_y);
                if (
                    crosswind
                    > rotor_radius_m + wake_expansion * downstream
                ) {
                    continue;
                }
                const double deficit = 2.0 * induction / std::pow(
                    1.0
                        + wake_expansion * downstream / rotor_radius_m,
                    2.0
                );
                deficit_squared += deficit * deficit;
            }
            const double retained = std::max(
                0.0,
                1.0 - std::min(1.0, std::sqrt(deficit_squared))
            );
            direction_power += expected_turbine_power(
                wind.shape, wind.scale * retained
            );
        }
        total_power += wind.probability * direction_power;
    }
    return {total_power, violation};
}

std::vector<double> Problem::grnn_features(
    const std::vector<Point>& layout
) const {
    if (static_cast<int>(layout.size()) != turbine_count_) {
        throw std::invalid_argument("T77 GRNN layout size");
    }
    std::vector<Point> ordered = layout;
    std::stable_sort(
        ordered.begin(),
        ordered.end(),
        [](const Point& left, const Point& right) {
            if (left.x_m != right.x_m) return left.x_m < right.x_m;
            return left.y_m < right.y_m;
        }
    );
    std::vector<double> result(
        static_cast<std::size_t>(2 * turbine_count_)
    );
    for (int turbine = 0; turbine < turbine_count_; ++turbine) {
        result[2 * turbine] = ordered[turbine].x_m / farm_side_m_;
        result[2 * turbine + 1] =
            ordered[turbine].y_m / farm_side_m_;
    }
    return result;
}

RunResult run(
    const Problem& problem,
    const std::uint64_t seed,
    const RunConfig& config
) {
    if (
        config.population != 40
        || config.generations < 1
        || config.exact_stage_generations < 1
        || config.exact_stage_generations > config.generations
        || config.training_capacity
            != config.population * config.exact_stage_generations
        || config.success_history_capacity < 1
        || config.workers < 1
    ) {
        throw std::invalid_argument("T77 invalid run config");
    }
    const auto total_start = Clock::now();
    fode::PersistentExecutor executor(config.workers);
    executor.reset_work_receipt();
    const fode::CounterRng random(seed ^ 0x77118310ULL);
    std::vector<Individual> population(
        static_cast<std::size_t>(config.population)
    );
    executor.parallel_for(0, config.population, [&](const int individual) {
        population[individual].layout = random_layout(
            problem, random, individual
        );
    });
    double exact_seconds = 0.0;
    auto exact_start = Clock::now();
    executor.parallel_for(0, config.population, [&](const int individual) {
        population[individual].evaluation =
            problem.evaluate(population[individual].layout);
    });
    exact_seconds += elapsed(exact_start);
    std::uint64_t physical_fes = config.population;
    std::uint64_t proposals = 0;
    std::uint64_t surrogate_inferences = 0;
    int best = best_index(population);
    const double initial_best =
        population[best].evaluation.expected_power_kw;
    double mean_f1 = 1.0;
    double mean_f2 = 1.0;
    double mean_moved = 5.0;
    std::deque<double> successful_f1;
    std::deque<double> successful_f2;
    std::deque<double> successful_moved;
    Grnn grnn(
        config.training_capacity,
        2 * problem.turbine_count()
    );
    std::vector<double> best_history;
    best_history.reserve(static_cast<std::size_t>(config.generations));
    double surrogate_seconds = 0.0;
    for (int generation = 0;
         generation < config.generations;
         ++generation) {
        std::vector<Trial> trials(
            static_cast<std::size_t>(config.population)
        );
        const double frozen_f1 = mean_f1;
        const double frozen_f2 = mean_f2;
        const double frozen_moved = mean_moved;
        executor.parallel_for(0, config.population, [&](const int target) {
            Trial& trial = trials[target];
            trial.f1 = frozen_f1 + normal_standard_deviation * random.normal(
                generation + 1, 210, target
            );
            trial.f2 = frozen_f2 + normal_standard_deviation * random.normal(
                generation + 1, 211, target
            );
            trial.moved = std::clamp(
                static_cast<int>(std::llround(
                    frozen_moved
                    + normal_standard_deviation * random.normal(
                        generation + 1, 212, target
                    )
                )),
                1,
                problem.turbine_count()
            );
            trial.layout = generate_trial(
                problem,
                population,
                best,
                target,
                trial.f1,
                trial.f2,
                trial.moved,
                random,
                generation + 1
            );
        });
        proposals += config.population;
        std::vector<int> exact_indices;
        if (generation < config.exact_stage_generations) {
            exact_indices.resize(static_cast<std::size_t>(config.population));
            std::iota(exact_indices.begin(), exact_indices.end(), 0);
        } else {
            const auto surrogate_start = Clock::now();
            executor.parallel_for(
                0, config.population, [&](const int target) {
                    trials[target].features =
                        problem.grnn_features(trials[target].layout);
                    trials[target].prediction = grnn.predict(
                        trials[target].features
                    );
                }
            );
            surrogate_seconds += elapsed(surrogate_start);
            surrogate_inferences += config.population;
            exact_indices.resize(
                static_cast<std::size_t>(config.population / 2)
            );
            std::vector<int> ranking(
                static_cast<std::size_t>(config.population)
            );
            std::iota(ranking.begin(), ranking.end(), 0);
            std::stable_sort(
                ranking.begin(),
                ranking.end(),
                [&](const int left, const int right) {
                    if (
                        trials[left].prediction
                        != trials[right].prediction
                    ) {
                        return trials[left].prediction
                            > trials[right].prediction;
                    }
                    return left < right;
                }
            );
            std::copy_n(
                ranking.begin(), exact_indices.size(), exact_indices.begin()
            );
        }
        exact_start = Clock::now();
        executor.parallel_for(
            0,
            static_cast<int>(exact_indices.size()),
            [&](const int slot) {
                Trial& trial = trials[exact_indices[slot]];
                trial.evaluation = problem.evaluate(trial.layout);
                trial.exact = true;
            }
        );
        exact_seconds += elapsed(exact_start);
        physical_fes += exact_indices.size();
        for (const int index : exact_indices) {
            Trial& trial = trials[index];
            if (trial.features.empty()) {
                trial.features = problem.grnn_features(trial.layout);
            }
            grnn.append(
                trial.features, trial.evaluation.expected_power_kw
            );
            if (better(trial.evaluation, population[index].evaluation)) {
                population[index] = {
                    std::move(trial.layout), trial.evaluation
                };
                append_fifo(
                    successful_f1,
                    trial.f1,
                    config.success_history_capacity
                );
                append_fifo(
                    successful_f2,
                    trial.f2,
                    config.success_history_capacity
                );
                append_fifo(
                    successful_moved,
                    static_cast<double>(trial.moved),
                    config.success_history_capacity
                );
            }
        }
        mean_f1 = mean_or(successful_f1, mean_f1);
        mean_f2 = mean_or(successful_f2, mean_f2);
        mean_moved = mean_or(successful_moved, mean_moved);
        best = best_index(population);
        best_history.push_back(
            population[best].evaluation.expected_power_kw
        );
    }
    const auto receipt = executor.work_receipt();
    const double total_seconds = elapsed(total_start);
    std::uint64_t hash = 1469598103934665603ULL;
    for (const auto& point : population[best].layout) {
        hash = mix_hash(hash, std::bit_cast<std::uint64_t>(point.x_m));
        hash = mix_hash(hash, std::bit_cast<std::uint64_t>(point.y_m));
    }
    hash = mix_hash(
        hash,
        std::bit_cast<std::uint64_t>(
            population[best].evaluation.expected_power_kw
        )
    );
    hash = mix_hash(hash, physical_fes);
    return {
        .problem_semantic_id = problem.semantic_id(),
        .method_semantic_id = method_id,
        .protocol_semantic_id = protocol_id,
        .scenario = problem.scenario(),
        .turbine_count = problem.turbine_count(),
        .farm_side_m = problem.farm_side_m(),
        .seed = seed,
        .generations = config.generations,
        .candidate_proposals = proposals,
        .physical_exact_fes = physical_fes,
        .surrogate_inferences = surrogate_inferences,
        .requested_workers = config.workers,
        .observed_workers = receipt.distinct_participants,
        .initial_best_power_kw = initial_best,
        .best_evaluation = population[best].evaluation,
        .best_layout = population[best].layout,
        .best_history_kw = std::move(best_history),
        .final_mean_f1 = mean_f1,
        .final_mean_f2 = mean_f2,
        .final_mean_moved_turbines = mean_moved,
        .exact_evaluator_seconds = exact_seconds,
        .surrogate_seconds = surrogate_seconds,
        .operator_seconds = std::max(
            0.0, total_seconds - exact_seconds - surrogate_seconds
        ),
        .end_to_end_seconds = total_seconds,
        .scientific_hash = hash,
    };
}

std::vector<std::string> paper_case_ids() {
    std::vector<std::string> result;
    constexpr int counts[]{15,20,25,30,35,40,60,80,100};
    for (int scenario = 1; scenario <= 2; ++scenario) {
        for (const int count : counts) {
            result.push_back(
                "t77_ws" + std::to_string(scenario)
                + "_n" + std::to_string(count)
            );
        }
    }
    return result;
}

}  // namespace core99::t77
