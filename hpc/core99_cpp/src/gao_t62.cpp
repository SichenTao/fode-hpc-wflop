/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T62 pure-C++ wake evaluator and MPGA implementation
Paper title/DOI: Optimization of Wind Turbine Layout Position in a Wind Farm
Using a Newly-Developed Two-Dimensional Wake Model;
10.1016/j.apenergy.2016.04.098
Public source: none located; cited predecessor DOI
10.1016/j.jweia.2015.01.018 supplies disclosed MPGA fields
Missing and reconstruction: author source, seeds, layouts, ambient turbulence,
and several MPGA operators are absent; deterministic completion is declared
in include/core99/gao_t62.hpp
Semantic IDs: t62_gao_case_b_grid_jensen_gaussian_v1;
t62_mpga_declared_reconstruction_v1
Controlling contract: shared/contracts/core99_t62_gao_2016.json
Independent equation oracle: scripts/validate_core99_t62.py
HPC design: grid wake-pair terms are precomputed once; a persistent team
evaluates independent deme-individual layouts; each layout uses contiguous
direction-major data and fixed-order reductions; generation, elite, and
migration state commits remain ordered and deterministic
Claim boundary: academic declared reproduction, not author-code or exact
author-numerical replay
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "core99/gao_t62.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <limits>
#include <numbers>
#include <random>
#include <stdexcept>
#include <utility>

namespace core99::t62 {
namespace {

using Clock = std::chrono::steady_clock;
constexpr int kDirections = 36;
constexpr int kGridSide = 10;
constexpr int kGridSites = 100;
constexpr double kFarmSideM = 2000.0;
constexpr double kGridPitchM = 200.0;
constexpr double kRotorDiameterM = 40.0;
constexpr double kRotorRadiusM = 20.0;
constexpr double kHubHeightM = 60.0;
constexpr double kRoughnessM = 0.3;
constexpr double kThrustCoefficient = 0.88;
constexpr double kAmbientTurbulence = 0.10;
constexpr double kFreeSpeedMps = 12.0;
constexpr double kPowerCoefficient = 0.3;
constexpr std::uint32_t kGeneMask = (1U << 20U) - 1U;
constexpr double kGaussianScale =
    5.16 / 2.506628274631000502415765284811;

double wake_fraction(
    const double downstream_m,
    const double crosswind_m,
    const double thrust_coefficient,
    const double ambient_turbulence
) {
    if (!(downstream_m > 0.0) || !(ambient_turbulence > 0.0)) {
        return 0.0;
    }
    const double a =
        0.5 * (1.0 - std::sqrt(std::max(0.0, 1.0 - thrust_coefficient)));
    const double denominator = 1.0 - 2.0 * a;
    if (!(denominator > 0.0)) {
        return 0.0;
    }
    const double r1 =
        kRotorRadiusM * std::sqrt((1.0 - a) / denominator);
    const double x_over_d = downstream_m / kRotorDiameterM;
    const double wake_turbulence = std::pow(
        0.4 * thrust_coefficient / std::sqrt(x_over_d)
            + std::sqrt(ambient_turbulence),
        2.0
    );
    const double base_wake_decay =
        0.5 / std::log(kHubHeightM / kRoughnessM);
    const double wake_decay =
        base_wake_decay * wake_turbulence / ambient_turbulence;
    const double wake_radius = r1 + wake_decay * downstream_m;
    if (std::abs(crosswind_m) > wake_radius) {
        return 0.0;
    }
    const double centreline = 2.0 * a / std::pow(
        1.0 + wake_decay * downstream_m / r1,
        2.0
    );
    const double sigma = wake_radius / 2.58;
    const double gaussian = kGaussianScale * std::exp(
        -crosswind_m * crosswind_m / (2.0 * sigma * sigma)
    );
    return std::clamp(centreline * gaussian, 0.0, 1.0);
}

int nearest_grid_site(const double x_m, const double y_m) {
    const int column = std::clamp(
        static_cast<int>(std::llround((x_m - 100.0) / kGridPitchM)),
        0,
        kGridSide - 1
    );
    const int row = std::clamp(
        static_cast<int>(std::llround((y_m - 100.0) / kGridPitchM)),
        0,
        kGridSide - 1
    );
    return row * kGridSide + column;
}

Point grid_point(const int site) {
    return {
        100.0 + kGridPitchM * static_cast<double>(site % kGridSide),
        100.0 + kGridPitchM * static_cast<double>(site / kGridSide),
    };
}

double decode_gene(const std::uint32_t gene) {
    return kFarmSideM * static_cast<double>(gene & kGeneMask)
        / static_cast<double>(kGeneMask);
}

std::vector<int> layout_sites(const std::vector<Point>& layout) {
    std::vector<int> sites;
    sites.reserve(layout.size());
    for (const Point& point : layout) {
        sites.push_back(nearest_grid_site(point.x_m, point.y_m));
    }
    return sites;
}

std::uint64_t hash_result(
    const std::vector<Point>& layout,
    const Evaluation& evaluation
) {
    std::uint64_t hash = 1469598103934665603ULL;
    for (const Point& point : layout) {
        hash ^= std::bit_cast<std::uint64_t>(point.x_m);
        hash *= 1099511628211ULL;
        hash ^= std::bit_cast<std::uint64_t>(point.y_m);
        hash *= 1099511628211ULL;
    }
    hash ^= std::bit_cast<std::uint64_t>(evaluation.objective);
    hash *= 1099511628211ULL;
    return hash;
}

bool better(const Evaluation& left, const Evaluation& right) {
    const bool left_feasible = left.constraint_violation <= 1.0e-12;
    const bool right_feasible = right.constraint_violation <= 1.0e-12;
    if (left_feasible != right_feasible) return left_feasible;
    if (left.constraint_violation != right.constraint_violation) {
        return left.constraint_violation < right.constraint_violation;
    }
    return left.objective < right.objective;
}

struct Individual {
    std::vector<std::uint32_t> genes;
    Evaluation evaluation;
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

int worst_index(const std::vector<Individual>& population) {
    int result = 0;
    for (int index = 1; index < static_cast<int>(population.size()); ++index) {
        if (better(population[result].evaluation, population[index].evaluation)) {
            result = index;
        }
    }
    return result;
}

std::vector<std::uint32_t> crossover_and_mutate(
    const std::vector<std::uint32_t>& first,
    const std::vector<std::uint32_t>& second,
    const double crossover_probability,
    const double mutation_probability,
    std::mt19937_64& rng
) {
    std::vector<std::uint32_t> result = first;
    std::uniform_real_distribution<double> uniform(0.0, 1.0);
    const int bit_count = static_cast<int>(first.size()) * 20;
    if (bit_count > 1 && uniform(rng) < crossover_probability) {
        std::uniform_int_distribution<int> point_distribution(1, bit_count - 1);
        const int point = point_distribution(rng);
        for (int bit = point; bit < bit_count; ++bit) {
            const int gene_index = bit / 20;
            const int gene_bit = bit % 20;
            const std::uint32_t mask = 1U << static_cast<unsigned>(gene_bit);
            result[gene_index] =
                (result[gene_index] & ~mask) | (second[gene_index] & mask);
        }
    }
    for (std::uint32_t& gene : result) {
        for (int bit = 0; bit < 20; ++bit) {
            if (uniform(rng) < mutation_probability) {
                gene ^= 1U << static_cast<unsigned>(bit);
            }
        }
        gene &= kGeneMask;
    }
    return result;
}

}  // namespace

double paper_cost(const int turbine_count) {
    const double count = static_cast<double>(turbine_count);
    return count * (
        2.0 / 3.0 + std::exp(-0.00174 * count * count) / 3.0
    );
}

double improved_wake_speed_ratio(
    const double downstream_diameters,
    const double crosswind_diameters,
    const double thrust_coefficient,
    const double ambient_turbulence
) {
    return 1.0 - wake_fraction(
        downstream_diameters * kRotorDiameterM,
        crosswind_diameters * kRotorDiameterM,
        thrust_coefficient,
        ambient_turbulence
    );
}

Problem::Problem(const int turbine_count, const SiteMode mode)
    : turbine_count_(turbine_count), mode_(mode) {
    if (turbine_count_ < 1 || turbine_count_ > kGridSites) {
        throw std::invalid_argument("T62 turbine count must be in [1,100]");
    }
    semantic_id_ = mode_ == SiteMode::paper_grid
        ? "t62_gao_case_b_grid_jensen_gaussian_v1"
        : "t62_gao_case_b_continuous_jensen_gaussian_sensitivity_v1";
    if (mode_ != SiteMode::paper_grid) return;
    grid_pair_deficit_squared_.resize(
        static_cast<std::size_t>(kDirections * kGridSites * kGridSites),
        0.0
    );
    for (int direction = 0; direction < kDirections; ++direction) {
        const double radians =
            static_cast<double>(direction) * 10.0
            * std::numbers::pi / 180.0;
        const double cosine = std::cos(radians);
        const double sine = std::sin(radians);
        for (int target = 0; target < kGridSites; ++target) {
            const Point target_point = grid_point(target);
            for (int source = 0; source < kGridSites; ++source) {
                const Point source_point = grid_point(source);
                const double dx = target_point.x_m - source_point.x_m;
                const double dy = target_point.y_m - source_point.y_m;
                const double fraction = wake_fraction(
                    dx * cosine + dy * sine,
                    -dx * sine + dy * cosine,
                    kThrustCoefficient,
                    kAmbientTurbulence
                );
                const std::size_t index = static_cast<std::size_t>(
                    (direction * kGridSites + target) * kGridSites + source
                );
                grid_pair_deficit_squared_[index] = fraction * fraction;
            }
        }
    }
}

int Problem::turbine_count() const noexcept { return turbine_count_; }
SiteMode Problem::site_mode() const noexcept { return mode_; }
const std::string& Problem::semantic_id() const noexcept { return semantic_id_; }

std::vector<Point> Problem::decode(
    const std::vector<std::uint32_t>& genes
) const {
    if (genes.size() != static_cast<std::size_t>(2 * turbine_count_)) {
        throw std::invalid_argument("T62 gene count must equal 2N");
    }
    std::vector<Point> result;
    result.reserve(static_cast<std::size_t>(turbine_count_));
    if (mode_ == SiteMode::paper_grid) {
        std::array<bool, kGridSites> used{};
        for (int turbine = 0; turbine < turbine_count_; ++turbine) {
            const double x = decode_gene(genes[2 * turbine]);
            const double y = decode_gene(genes[2 * turbine + 1]);
            const int preferred = nearest_grid_site(x, y);
            int selected = -1;
            double best_distance = std::numeric_limits<double>::infinity();
            for (int site = 0; site < kGridSites; ++site) {
                if (used[site]) continue;
                const Point point = grid_point(site);
                const double distance =
                    (point.x_m - x) * (point.x_m - x)
                    + (point.y_m - y) * (point.y_m - y);
                if (site == preferred) {
                    selected = site;
                    break;
                }
                if (distance < best_distance) {
                    best_distance = distance;
                    selected = site;
                }
            }
            used[selected] = true;
            result.push_back(grid_point(selected));
        }
        return result;
    }
    for (int turbine = 0; turbine < turbine_count_; ++turbine) {
        Point point{decode_gene(genes[2 * turbine]),
                    decode_gene(genes[2 * turbine + 1])};
        for (int attempt = 0; attempt < kGridSites; ++attempt) {
            bool valid = true;
            for (const Point& accepted : result) {
                if (std::hypot(
                        point.x_m - accepted.x_m,
                        point.y_m - accepted.y_m
                    ) < 5.0 * kRotorDiameterM) {
                    valid = false;
                    break;
                }
            }
            if (valid) break;
            point = grid_point(
                (nearest_grid_site(point.x_m, point.y_m) + attempt + 1)
                % kGridSites
            );
        }
        result.push_back(point);
    }
    return result;
}

Evaluation Problem::evaluate_genes(
    const std::vector<std::uint32_t>& genes
) const {
    return evaluate_layout(decode(genes));
}

Evaluation Problem::evaluate_layout(const std::vector<Point>& layout) const {
    if (layout.size() != static_cast<std::size_t>(turbine_count_)) {
        throw std::invalid_argument("T62 layout size must equal N");
    }
    double violation = 0.0;
    for (const Point& point : layout) {
        violation += std::max({0.0, -point.x_m, point.x_m - kFarmSideM});
        violation += std::max({0.0, -point.y_m, point.y_m - kFarmSideM});
    }
    for (std::size_t left = 0; left < layout.size(); ++left) {
        for (std::size_t right = left + 1; right < layout.size(); ++right) {
            violation += std::max(
                0.0,
                5.0 * kRotorDiameterM - std::hypot(
                    layout[left].x_m - layout[right].x_m,
                    layout[left].y_m - layout[right].y_m
                )
            );
        }
    }

    const std::vector<int> sites = mode_ == SiteMode::paper_grid
        ? layout_sites(layout) : std::vector<int>{};
    double total_power_kw = 0.0;
    for (int direction = 0; direction < kDirections; ++direction) {
        const double radians =
            static_cast<double>(direction) * 10.0
            * std::numbers::pi / 180.0;
        const double cosine = std::cos(radians);
        const double sine = std::sin(radians);
        for (int target = 0; target < turbine_count_; ++target) {
            double deficit_squared = 0.0;
            if (mode_ == SiteMode::paper_grid) {
                const int target_site = sites[target];
                const std::size_t base = static_cast<std::size_t>(
                    (direction * kGridSites + target_site) * kGridSites
                );
                for (int source = 0; source < turbine_count_; ++source) {
                    deficit_squared +=
                        grid_pair_deficit_squared_[base + sites[source]];
                }
            } else {
                for (int source = 0; source < turbine_count_; ++source) {
                    if (source == target) continue;
                    const double dx = layout[target].x_m - layout[source].x_m;
                    const double dy = layout[target].y_m - layout[source].y_m;
                    const double fraction = wake_fraction(
                        dx * cosine + dy * sine,
                        -dx * sine + dy * cosine,
                        kThrustCoefficient,
                        kAmbientTurbulence
                    );
                    deficit_squared += fraction * fraction;
                }
            }
            const double speed = kFreeSpeedMps * std::max(
                0.0, 1.0 - std::sqrt(deficit_squared)
            );
            total_power_kw += kPowerCoefficient * speed * speed * speed;
        }
    }
    const double average_power_kw =
        total_power_kw / static_cast<double>(kDirections);
    const double cost = paper_cost(turbine_count_);
    Evaluation result;
    result.average_power_kw = average_power_kw;
    result.efficiency = average_power_kw / (
        static_cast<double>(turbine_count_) * kPowerCoefficient
        * kFreeSpeedMps * kFreeSpeedMps * kFreeSpeedMps
    );
    result.cost = cost;
    result.constraint_violation = violation;
    result.objective = violation > 0.0
        ? 1.0e6 + violation
        : cost / std::max(average_power_kw, 1.0e-12);
    return result;
}

std::vector<Evaluation> Problem::evaluate_population(
    const std::vector<std::vector<std::uint32_t>>& population,
    fode::PersistentExecutor& executor
) const {
    std::vector<Evaluation> result(population.size());
    executor.parallel_for(0, static_cast<int>(population.size()), [&](int i) {
        result[i] = evaluate_genes(population[i]);
    });
    return result;
}

RunResult run_mpga(
    const Problem& problem,
    const std::uint64_t seed,
    const int workers,
    const MpgaConfig& config
) {
    if (workers <= 0 || config.demes <= 0
        || config.individuals_per_deme < 2
        || config.unchanged_generations <= 0
        || config.maximum_generations <= 0
        || config.migration_period <= 0) {
        throw std::invalid_argument("invalid T62 MPGA configuration");
    }
    const auto total_start = Clock::now();
    fode::PersistentExecutor executor(workers);
    executor.reset_work_receipt();
    std::mt19937_64 rng(seed);
    std::uniform_int_distribution<std::uint32_t> gene_distribution(
        0U, kGeneMask
    );
    const int population_size =
        config.demes * config.individuals_per_deme;
    const int gene_count = 2 * problem.turbine_count();
    std::vector<Individual> population(population_size);
    for (Individual& individual : population) {
        individual.genes.resize(gene_count);
        for (std::uint32_t& gene : individual.genes) {
            gene = gene_distribution(rng);
        }
    }

    double evaluator_seconds = 0.0;
    std::uint64_t physical_fes = 0;
    auto evaluate_all = [&](std::vector<Individual>& values) {
        std::vector<std::vector<std::uint32_t>> genes;
        genes.reserve(values.size());
        for (const Individual& individual : values) {
            genes.push_back(individual.genes);
        }
        const auto start = Clock::now();
        const auto evaluations = problem.evaluate_population(genes, executor);
        evaluator_seconds += std::chrono::duration<double>(
            Clock::now() - start
        ).count();
        physical_fes += evaluations.size();
        for (std::size_t index = 0; index < values.size(); ++index) {
            values[index].evaluation = evaluations[index];
        }
    };
    evaluate_all(population);

    int global_best = best_index(population);
    double best_objective = population[global_best].evaluation.objective;
    int unchanged = 0;
    int generations = 0;
    std::uniform_int_distribution<int> tournament_pick(
        0, config.individuals_per_deme - 1
    );
    while (generations < config.maximum_generations
           && unchanged < config.unchanged_generations) {
        std::vector<Individual> next(population_size);
        for (int deme = 0; deme < config.demes; ++deme) {
            const int offset = deme * config.individuals_per_deme;
            int elite_local = 0;
            for (int index = 1; index < config.individuals_per_deme; ++index) {
                if (better(
                        population[offset + index].evaluation,
                        population[offset + elite_local].evaluation
                    )) {
                    elite_local = index;
                }
            }
            next[offset] = population[offset + elite_local];
            const double fraction = config.demes == 1 ? 0.0
                : static_cast<double>(deme)
                    / static_cast<double>(config.demes - 1);
            const double crossover_probability = 0.7 + 0.2 * fraction;
            const double mutation_probability = 0.001 + 0.049 * fraction;
            auto select = [&]() -> const Individual& {
                const int first = tournament_pick(rng);
                const int second = tournament_pick(rng);
                const int chosen = better(
                    population[offset + first].evaluation,
                    population[offset + second].evaluation
                ) ? first : second;
                return population[offset + chosen];
            };
            for (int index = 1; index < config.individuals_per_deme; ++index) {
                const Individual& first = select();
                const Individual& second = select();
                next[offset + index].genes = crossover_and_mutate(
                    first.genes, second.genes, crossover_probability,
                    mutation_probability, rng
                );
            }
        }
        evaluate_all(next);
        ++generations;
        if (generations % config.migration_period == 0) {
            std::vector<Individual> emigrants;
            emigrants.reserve(config.demes);
            for (int deme = 0; deme < config.demes; ++deme) {
                const auto begin =
                    next.begin() + deme * config.individuals_per_deme;
                std::vector<Individual> local(
                    begin, begin + config.individuals_per_deme
                );
                emigrants.push_back(local[best_index(local)]);
            }
            for (int deme = 0; deme < config.demes; ++deme) {
                const int destination = (deme + 1) % config.demes;
                const int offset =
                    destination * config.individuals_per_deme;
                const auto begin = next.begin() + offset;
                std::vector<Individual> local(
                    begin, begin + config.individuals_per_deme
                );
                next[offset + worst_index(local)] = emigrants[deme];
            }
        }
        population = std::move(next);
        global_best = best_index(population);
        const double objective = population[global_best].evaluation.objective;
        if (objective + 1.0e-15 < best_objective) {
            best_objective = objective;
            unchanged = 0;
        } else {
            ++unchanged;
        }
    }

    global_best = best_index(population);
    const double end_to_end_seconds = std::chrono::duration<double>(
        Clock::now() - total_start
    ).count();
    const Individual& winner = population[global_best];
    const std::vector<Point> layout = problem.decode(winner.genes);
    const auto receipt = executor.work_receipt();
    RunResult result;
    result.problem_semantic_id = problem.semantic_id();
    result.method_semantic_id = "t62_mpga_declared_reconstruction_v1";
    result.turbine_count = problem.turbine_count();
    result.seed = seed;
    result.generations = generations;
    result.unchanged_generations = unchanged;
    result.physical_fes = physical_fes;
    result.requested_workers = workers;
    result.observed_workers = receipt.distinct_participants;
    result.evaluator_seconds = evaluator_seconds;
    result.end_to_end_seconds = end_to_end_seconds;
    result.algorithm_seconds = std::max(
        0.0, end_to_end_seconds - evaluator_seconds
    );
    result.best_evaluation = winner.evaluation;
    result.best_layout = layout;
    result.scientific_hash = hash_result(layout, winner.evaluation);
    return result;
}

}  // namespace core99::t62
