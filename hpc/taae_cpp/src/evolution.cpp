/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: TAAE end-to-end declared-reconstruction evolutionary method
Paper title: Transformer Autoencoder-Assisted Evolutionary Framework for Constrained Multiobjective 3D Wind Farm Layout Optimization
DOI: 10.1109/JAS.2026.126233
Public author method source/checkpoint: unavailable as recorded in docs/source-dossiers/Y36.json
Missing choices completed here: SPEA2 density k, CDP and tournament ties, raw-latent mutation bounds, pre-repair decoded-solution filtering, Gaussian covariance regularization, post-repair guards, no-feasible front output, partial batches, and checkpoint admission
Reconstruction status: bounded executable M3 engineering reconstruction on the declared P3 problem proxy
Method evidence tier: M3_DECLARED_COMPLETION
Method semantic ID: taae_transformer_evolution_declared_reconstruction_v1
Kernel semantic ID: taae_transformer_declared_reconstruction_v1
Problem semantic ID: taae_zhangbei_structured_declared_proxy_v1
Controlling contract: shared/contracts/taae_transformer_evolution_declared_reconstruction_contract.json
Claim boundary: distinct bounded end-to-end reconstruction only; original taae remains blocked, paper-scale state requires an immutable checkpoint, and no Zhangbei, reported-front, formal, performance, or GPU claim is made
END WFLOP IMPLEMENTATION FACT DECLARATION
*/

#include "taae/evolution.hpp"

#include "fode/executor.hpp"

#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <limits>
#include <numeric>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace taae::evolution {
namespace {

using Clock = std::chrono::steady_clock;

constexpr int kPaperPopulationSize = 100;
constexpr std::uint64_t kPaperMaximumFes = 10000;
constexpr double kDifferentialWeight = 0.3;
constexpr double kPolynomialDistributionIndex = 20.0;
constexpr int kGaussianAttemptCap = 64;
constexpr int kProposalMultiplierCap = 10;
constexpr int kRefillMultiplierCap = 10;

struct DeterministicRng {
    std::uint64_t state;

    explicit DeterministicRng(std::uint64_t seed) : state(seed) {}

    std::uint64_t next_u64() {
        state += 0x9e3779b97f4a7c15ULL;
        std::uint64_t value = state;
        value = (value ^ (value >> 30U)) * 0xbf58476d1ce4e5b9ULL;
        value = (value ^ (value >> 27U)) * 0x94d049bb133111ebULL;
        return value ^ (value >> 31U);
    }

    double unit() {
        constexpr double scale =
            1.0 / static_cast<double>(std::uint64_t{1} << 53U);
        return static_cast<double>(next_u64() >> 11U) * scale;
    }

    std::size_t index(std::size_t bound) {
        if (bound == 0) {
            throw std::invalid_argument("RNG bound is zero");
        }
        return static_cast<std::size_t>(
            next_u64() % static_cast<std::uint64_t>(bound)
        );
    }
};

struct Individual {
    std::vector<int> layout;
    wflop::taae::CompleteEvaluation evaluation;
    std::vector<double> raw_latent;
    double spea2 = 0.0;
    double relative_fitness = 0.0;
    int rank = 0;
    double crowding = 0.0;
    std::size_t source_index = 0;
};

double objective(const Individual& value, int index) {
    return index == 0
        ? value.evaluation.reciprocal_expected_power_per_kw
        : value.evaluation.average_a_weighted_noise_dba;
}

bool feasible(const Individual& value) {
    return value.evaluation.normalized_constraint_violation <= 0.0;
}

bool pareto_dominates(const Individual& left, const Individual& right) {
    bool strict = false;
    for (int objective_index = 0; objective_index < 2; ++objective_index) {
        const double lhs = objective(left, objective_index);
        const double rhs = objective(right, objective_index);
        if (lhs > rhs) {
            return false;
        }
        strict = strict || lhs < rhs;
    }
    return strict;
}

bool cdp_dominates(const Individual& left, const Individual& right) {
    const bool left_feasible = feasible(left);
    const bool right_feasible = feasible(right);
    if (left_feasible != right_feasible) {
        return left_feasible;
    }
    if (!left_feasible) {
        const double lhs =
            left.evaluation.normalized_constraint_violation;
        const double rhs =
            right.evaluation.normalized_constraint_violation;
        return lhs < rhs;
    }
    return pareto_dominates(left, right);
}

std::vector<std::vector<std::size_t>> assign_nondomination(
    std::vector<Individual>& population
) {
    const std::size_t count = population.size();
    std::vector<int> domination_count(count, 0);
    std::vector<std::vector<std::size_t>> dominates(count);
    std::vector<std::vector<std::size_t>> fronts(1);
    for (std::size_t left = 0; left < count; ++left) {
        for (std::size_t right = 0; right < count; ++right) {
            if (left == right) {
                continue;
            }
            if (cdp_dominates(population[left], population[right])) {
                dominates[left].push_back(right);
            } else if (
                cdp_dominates(population[right], population[left])
            ) {
                ++domination_count[left];
            }
        }
        if (domination_count[left] == 0) {
            population[left].rank = 0;
            fronts.front().push_back(left);
        }
    }
    std::size_t front_index = 0;
    while (
        front_index < fronts.size() &&
        !fronts[front_index].empty()
    ) {
        std::vector<std::size_t> next;
        for (std::size_t left : fronts[front_index]) {
            for (std::size_t right : dominates[left]) {
                --domination_count[right];
                if (domination_count[right] == 0) {
                    population[right].rank =
                        static_cast<int>(front_index + 1);
                    next.push_back(right);
                }
            }
        }
        if (!next.empty()) {
            fronts.push_back(std::move(next));
        }
        ++front_index;
    }
    return fronts;
}

void assign_crowding(
    std::vector<Individual>& population,
    const std::vector<std::size_t>& front
) {
    for (std::size_t index : front) {
        population[index].crowding = 0.0;
    }
    if (front.size() <= 2) {
        for (std::size_t index : front) {
            population[index].crowding =
                std::numeric_limits<double>::infinity();
        }
        return;
    }
    for (int objective_index = 0;
         objective_index < 2;
         ++objective_index) {
        std::vector<std::size_t> ordered = front;
        std::stable_sort(
            ordered.begin(),
            ordered.end(),
            [&](std::size_t left, std::size_t right) {
                const double lhs =
                    objective(population[left], objective_index);
                const double rhs =
                    objective(population[right], objective_index);
                if (lhs != rhs) {
                    return lhs < rhs;
                }
                return population[left].source_index <
                    population[right].source_index;
            }
        );
        population[ordered.front()].crowding =
            std::numeric_limits<double>::infinity();
        population[ordered.back()].crowding =
            std::numeric_limits<double>::infinity();
        const double minimum =
            objective(population[ordered.front()], objective_index);
        const double maximum =
            objective(population[ordered.back()], objective_index);
        if (maximum == minimum) {
            continue;
        }
        for (std::size_t position = 1;
             position + 1 < ordered.size();
             ++position) {
            if (std::isinf(population[ordered[position]].crowding)) {
                continue;
            }
            const double previous =
                objective(
                    population[ordered[position - 1]],
                    objective_index
                );
            const double next =
                objective(
                    population[ordered[position + 1]],
                    objective_index
                );
            population[ordered[position]].crowding +=
                (next - previous) / (maximum - minimum);
        }
    }
}

void assign_selection_state(std::vector<Individual>& population) {
    const auto fronts = assign_nondomination(population);
    for (const auto& front : fronts) {
        assign_crowding(population, front);
    }
}

void assign_spea2_relative_fitness(
    std::vector<Individual>& population
) {
    const std::size_t count = population.size();
    std::vector<double> strength(count, 0.0);
    for (std::size_t left = 0; left < count; ++left) {
        for (std::size_t right = 0; right < count; ++right) {
            if (left != right &&
                cdp_dominates(population[left], population[right])) {
                strength[left] += 1.0;
            }
        }
    }
    std::vector<double> minimum(2, std::numeric_limits<double>::infinity());
    std::vector<double> maximum(
        2,
        -std::numeric_limits<double>::infinity()
    );
    for (const Individual& value : population) {
        for (int objective_index = 0;
             objective_index < 2;
             ++objective_index) {
            minimum[static_cast<std::size_t>(objective_index)] =
                std::min(
                    minimum[static_cast<std::size_t>(objective_index)],
                    objective(value, objective_index)
                );
            maximum[static_cast<std::size_t>(objective_index)] =
                std::max(
                    maximum[static_cast<std::size_t>(objective_index)],
                    objective(value, objective_index)
                );
        }
    }
    const std::size_t density_k = std::max<std::size_t>(
        1,
        static_cast<std::size_t>(
            std::floor(std::sqrt(static_cast<double>(count)))
        )
    );
    for (std::size_t index = 0; index < count; ++index) {
        double raw = 0.0;
        for (std::size_t other = 0; other < count; ++other) {
            if (other != index &&
                cdp_dominates(population[other], population[index])) {
                raw += strength[other];
            }
        }
        std::vector<double> distances;
        distances.reserve(count > 0 ? count - 1 : 0);
        for (std::size_t other = 0; other < count; ++other) {
            if (other == index) {
                continue;
            }
            double squared = 0.0;
            for (int objective_index = 0;
                 objective_index < 2;
                 ++objective_index) {
                const std::size_t objective_position =
                    static_cast<std::size_t>(objective_index);
                const double range =
                    maximum[objective_position] -
                    minimum[objective_position];
                const double difference =
                    range == 0.0
                        ? 0.0
                        : (
                            objective(population[index], objective_index) -
                            objective(population[other], objective_index)
                        ) / range;
                squared += difference * difference;
            }
            distances.push_back(std::sqrt(squared));
        }
        std::sort(distances.begin(), distances.end());
        const std::size_t neighbor =
            std::min(density_k, distances.size()) - 1;
        const double density =
            distances.empty() ? 0.5 : 1.0 / (distances[neighbor] + 2.0);
        population[index].spea2 = raw + density;
    }
    double minimum_fitness = std::numeric_limits<double>::infinity();
    double maximum_fitness = -std::numeric_limits<double>::infinity();
    for (const Individual& value : population) {
        minimum_fitness = std::min(minimum_fitness, value.spea2);
        maximum_fitness = std::max(maximum_fitness, value.spea2);
    }
    const double range = maximum_fitness - minimum_fitness;
    for (Individual& value : population) {
        value.relative_fitness =
            range == 0.0 ? 0.0 : (value.spea2 - minimum_fitness) / range;
    }
}

bool tournament_better(const Individual& left, const Individual& right) {
    if (cdp_dominates(left, right)) {
        return true;
    }
    if (cdp_dominates(right, left)) {
        return false;
    }
    if (left.rank != right.rank) {
        return left.rank < right.rank;
    }
    if (left.crowding != right.crowding) {
        return left.crowding > right.crowding;
    }
    return left.source_index < right.source_index;
}

std::size_t tournament(
    const std::vector<Individual>& population,
    DeterministicRng& rng
) {
    const std::size_t first = rng.index(population.size());
    const std::size_t second = rng.index(population.size());
    return tournament_better(population[first], population[second])
        ? first
        : second;
}

double polynomial_mutation(
    double value,
    double lower,
    double upper,
    DeterministicRng& rng
) {
    if (upper <= lower) {
        return value;
    }
    const double delta1 = (value - lower) / (upper - lower);
    const double delta2 = (upper - value) / (upper - lower);
    const double random = rng.unit();
    const double exponent = 1.0 / (kPolynomialDistributionIndex + 1.0);
    double delta = 0.0;
    if (random <= 0.5) {
        const double xy = 1.0 - delta1;
        const double value_term =
            2.0 * random +
            (1.0 - 2.0 * random) *
                std::pow(xy, kPolynomialDistributionIndex + 1.0);
        delta = std::pow(value_term, exponent) - 1.0;
    } else {
        const double xy = 1.0 - delta2;
        const double value_term =
            2.0 * (1.0 - random) +
            2.0 * (random - 0.5) *
                std::pow(xy, kPolynomialDistributionIndex + 1.0);
        delta = 1.0 - std::pow(value_term, exponent);
    }
    return std::clamp(value + delta * (upper - lower), lower, upper);
}

std::vector<double> latent_offspring(
    const std::vector<double>& current,
    const std::vector<double>& first,
    const std::vector<double>& second,
    const std::vector<double>& lower,
    const std::vector<double>& upper,
    DeterministicRng& rng,
    bool apply_mutation,
    std::size_t* crossover_dimension
) {
    if (current.empty() || current.size() != first.size() ||
        current.size() != second.size() ||
        current.size() != lower.size() ||
        current.size() != upper.size()) {
        throw std::invalid_argument("latent operator shape mismatch");
    }
    const std::size_t selected = rng.index(current.size());
    if (crossover_dimension != nullptr) {
        *crossover_dimension = selected;
    }
    std::vector<double> offspring = current;
    const double mutant =
        current[selected] +
        kDifferentialWeight * (first[selected] - second[selected]);
    offspring[selected] =
        std::clamp(mutant, lower[selected], upper[selected]);
    if (apply_mutation) {
        const double probability =
            1.0 / static_cast<double>(offspring.size());
        for (std::size_t dimension = 0;
             dimension < offspring.size();
             ++dimension) {
            if (rng.unit() < probability) {
                offspring[dimension] = polynomial_mutation(
                    offspring[dimension],
                    lower[dimension],
                    upper[dimension],
                    rng
                );
            }
        }
    }
    return offspring;
}

std::set<int> unavailable_cells(const fode::CaseData& problem) {
    return std::set<int>(
        problem.unavailable_cells_1based.begin(),
        problem.unavailable_cells_1based.end()
    );
}

std::vector<int> uniform_feasible_layout(
    const fode::CaseData& problem,
    DeterministicRng& rng
) {
    const std::set<int> unavailable = unavailable_cells(problem);
    std::vector<int> available;
    const int cells = problem.rows * problem.cols;
    for (int cell = 1; cell <= cells; ++cell) {
        if (!unavailable.contains(cell)) {
            available.push_back(cell);
        }
    }
    if (available.size() <
        static_cast<std::size_t>(problem.turbine_count)) {
        throw std::invalid_argument("too few admissible problem cells");
    }
    for (int position = 0;
         position < problem.turbine_count;
         ++position) {
        const std::size_t begin = static_cast<std::size_t>(position);
        const std::size_t selected =
            begin + rng.index(available.size() - begin);
        std::swap(available[begin], available[selected]);
    }
    available.resize(static_cast<std::size_t>(problem.turbine_count));
    std::sort(available.begin(), available.end());
    return available;
}

std::pair<double, double> cell_xy(
    const fode::CaseData& problem,
    int cell
) {
    const int zero_based = cell - 1;
    const int row = zero_based / problem.cols;
    const int column = zero_based - row * problem.cols;
    return {
        (static_cast<double>(column) + 0.5) * problem.cell_width,
        (static_cast<double>(row) + 0.5) * problem.cell_width,
    };
}

int nearest_admissible_cell(
    const fode::CaseData& problem,
    double x,
    double y,
    const std::set<int>& occupied
) {
    const std::set<int> unavailable = unavailable_cells(problem);
    int best = -1;
    double best_distance = std::numeric_limits<double>::infinity();
    for (int cell = 1; cell <= problem.rows * problem.cols; ++cell) {
        if (unavailable.contains(cell) || occupied.contains(cell)) {
            continue;
        }
        const auto [cell_x, cell_y] = cell_xy(problem, cell);
        const double dx = cell_x - x;
        const double dy = cell_y - y;
        const double distance = dx * dx + dy * dy;
        if (distance < best_distance) {
            best_distance = distance;
            best = cell;
        }
    }
    return best;
}

std::pair<double, double> standard_normal_pair(
    DeterministicRng& rng
) {
    const double first =
        std::max(rng.unit(), std::numeric_limits<double>::min());
    const double second = rng.unit();
    const double radius = std::sqrt(-2.0 * std::log(first));
    const double angle = 2.0 * std::acos(-1.0) * second;
    return {radius * std::cos(angle), radius * std::sin(angle)};
}

std::vector<int> repair_layout(
    const std::vector<int>& decoded,
    const fode::CaseData& problem,
    DeterministicRng& rng,
    bool* full_reinitialized
) {
    if (decoded.size() !=
        static_cast<std::size_t>(problem.turbine_count)) {
        throw std::invalid_argument("decoded layout length mismatch");
    }
    const std::set<int> unavailable = unavailable_cells(problem);
    std::set<int> retained;
    std::vector<int> conflicts;
    for (int cell : decoded) {
        const bool invalid =
            cell < 1 || cell > problem.rows * problem.cols ||
            unavailable.contains(cell) || retained.contains(cell);
        if (invalid) {
            conflicts.push_back(cell);
        } else {
            retained.insert(cell);
        }
    }
    if (conflicts.size() > 2) {
        if (full_reinitialized != nullptr) {
            *full_reinitialized = true;
        }
        return uniform_feasible_layout(problem, rng);
    }
    if (full_reinitialized != nullptr) {
        *full_reinitialized = false;
    }
    if (conflicts.empty()) {
        return std::vector<int>(retained.begin(), retained.end());
    }

    double mean_x = 0.0;
    double mean_y = 0.0;
    for (int cell : retained) {
        const auto [x, y] = cell_xy(problem, cell);
        mean_x += x;
        mean_y += y;
    }
    mean_x /= static_cast<double>(retained.size());
    mean_y /= static_cast<double>(retained.size());
    double covariance_xx = 0.0;
    double covariance_xy = 0.0;
    double covariance_yy = 0.0;
    for (int cell : retained) {
        const auto [x, y] = cell_xy(problem, cell);
        const double dx = x - mean_x;
        const double dy = y - mean_y;
        covariance_xx += dx * dx;
        covariance_xy += dx * dy;
        covariance_yy += dy * dy;
    }
    const double divisor =
        static_cast<double>(std::max<std::size_t>(retained.size() - 1, 1));
    covariance_xx /= divisor;
    covariance_xy /= divisor;
    covariance_yy /= divisor;
    const double regularization = std::max(
        problem.cell_width * problem.cell_width * 1.0e-6,
        1.0e-12
    );
    covariance_xx += regularization;
    covariance_yy += regularization;
    const double first_cholesky = std::sqrt(covariance_xx);
    const double cross_cholesky = covariance_xy / first_cholesky;
    const double second_cholesky = std::sqrt(
        std::max(
            covariance_yy - cross_cholesky * cross_cholesky,
            regularization
        )
    );
    for (std::size_t conflict = 0;
         conflict < conflicts.size();
         ++conflict) {
        int selected = -1;
        for (int attempt = 0;
             attempt < kGaussianAttemptCap && selected < 0;
             ++attempt) {
            const auto [z1, z2] = standard_normal_pair(rng);
            const double x = mean_x + first_cholesky * z1;
            const double y =
                mean_y + cross_cholesky * z1 + second_cholesky * z2;
            selected = nearest_admissible_cell(
                problem,
                x,
                y,
                retained
            );
        }
        if (selected < 0) {
            selected = nearest_admissible_cell(
                problem,
                0.0,
                0.0,
                retained
            );
        }
        if (selected < 0) {
            throw std::runtime_error("repair exhausted admissible cells");
        }
        retained.insert(selected);
    }
    return std::vector<int>(retained.begin(), retained.end());
}

std::string canonical_solution_key(const std::vector<int>& tokens) {
    std::vector<int> canonical = tokens;
    std::sort(canonical.begin(), canonical.end());
    std::ostringstream stream;
    stream << canonical.size() << ':';
    for (int token : canonical) {
        stream << token << ',';
    }
    return stream.str();
}

bool is_valid_decoded_solution(
    const std::vector<int>& decoded,
    const fode::CaseData& problem
) {
    if (decoded.size() !=
        static_cast<std::size_t>(problem.turbine_count)) {
        return false;
    }
    const std::set<int> unavailable = unavailable_cells(problem);
    std::set<int> unique;
    for (int cell : decoded) {
        if (cell < 1 || cell > problem.rows * problem.cols ||
            unavailable.contains(cell) || !unique.insert(cell).second) {
            return false;
        }
    }
    return true;
}

enum class DecodedProposalStatus {
    accepted,
    duplicate_raw_before_repair,
    parent_identical_before_repair,
    duplicate_after_repair,
};

struct DecodedProposalResult {
    DecodedProposalStatus status =
        DecodedProposalStatus::duplicate_after_repair;
    std::vector<int> repaired;
};

DecodedProposalResult filter_and_repair_decoded(
    const std::vector<int>& decoded,
    const fode::CaseData& problem,
    DeterministicRng& rng,
    const std::set<std::string>& parent_keys,
    std::set<std::string>& raw_decoded_keys,
    std::set<std::string>& accepted_repaired_keys
) {
    const std::string raw_key = canonical_solution_key(decoded);
    if (!raw_decoded_keys.insert(raw_key).second) {
        return {
            DecodedProposalStatus::duplicate_raw_before_repair,
            {},
        };
    }
    if (is_valid_decoded_solution(decoded, problem) &&
        parent_keys.contains(raw_key)) {
        return {
            DecodedProposalStatus::parent_identical_before_repair,
            {},
        };
    }
    std::vector<int> repaired =
        repair_layout(decoded, problem, rng, nullptr);
    const std::string repaired_key =
        canonical_solution_key(repaired);
    if (parent_keys.contains(repaired_key) ||
        !accepted_repaired_keys.insert(repaired_key).second) {
        return {
            DecodedProposalStatus::duplicate_after_repair,
            {},
        };
    }
    return {
        DecodedProposalStatus::accepted,
        std::move(repaired),
    };
}

double evaluate_population(
    std::vector<Individual>& population,
    const fode::CaseData& problem,
    fode::PersistentExecutor& executor
) {
    std::vector<int> flattened;
    flattened.reserve(
        population.size() *
        static_cast<std::size_t>(problem.turbine_count)
    );
    for (const Individual& value : population) {
        flattened.insert(
            flattened.end(),
            value.layout.begin(),
            value.layout.end()
        );
    }
    const auto batch = wflop::taae::evaluate_structured_proxy(
        flattened,
        static_cast<int>(population.size()),
        problem,
        executor
    );
    if (batch.values.size() != population.size() ||
        batch.complete_layout_evaluations != population.size()) {
        throw std::runtime_error("problem evaluator FES mismatch");
    }
    for (std::size_t index = 0; index < population.size(); ++index) {
        population[index].evaluation = batch.values[index];
    }
    return batch.elapsed_seconds;
}

std::vector<Individual> environmental_selection(
    std::vector<Individual> merged,
    std::size_t population_size
) {
    assign_selection_state(merged);
    std::stable_sort(
        merged.begin(),
        merged.end(),
        [](const Individual& left, const Individual& right) {
            if (left.rank != right.rank) {
                return left.rank < right.rank;
            }
            if (left.crowding != right.crowding) {
                return left.crowding > right.crowding;
            }
            if (left.source_index != right.source_index) {
                return left.source_index < right.source_index;
            }
            return left.layout < right.layout;
        }
    );
    if (merged.size() > population_size) {
        merged.resize(population_size);
    }
    for (std::size_t index = 0; index < merged.size(); ++index) {
        merged[index].source_index = index;
    }
    return merged;
}

std::uint64_t fnv_consume(
    std::uint64_t hash,
    const unsigned char* data,
    std::size_t size
) {
    for (std::size_t index = 0; index < size; ++index) {
        hash ^= data[index];
        hash *= 1099511628211ULL;
    }
    return hash;
}

template <typename Value>
std::uint64_t fnv_value(std::uint64_t hash, const Value& value) {
    const auto* bytes =
        reinterpret_cast<const unsigned char*>(&value);
    return fnv_consume(hash, bytes, sizeof(Value));
}

std::string hash_individuals(
    const std::vector<Individual>& population,
    bool include_evaluation
) {
    std::uint64_t hash = 14695981039346656037ULL;
    for (const Individual& value : population) {
        for (int cell : value.layout) {
            hash = fnv_value(hash, cell);
        }
        if (include_evaluation) {
            hash = fnv_value(
                hash,
                std::bit_cast<std::uint64_t>(
                    value.evaluation.reciprocal_expected_power_per_kw
                )
            );
            hash = fnv_value(
                hash,
                std::bit_cast<std::uint64_t>(
                    value.evaluation.average_a_weighted_noise_dba
                )
            );
            hash = fnv_value(
                hash,
                std::bit_cast<std::uint64_t>(
                    value.evaluation.normalized_constraint_violation
                )
            );
        }
    }
    std::ostringstream stream;
    stream << "fnv1a64:" << std::hex << std::setfill('0')
           << std::setw(16) << hash;
    return stream.str();
}

void deterministic_shuffle(
    std::vector<std::size_t>& order,
    DeterministicRng& rng
) {
    for (std::size_t end = order.size(); end > 1; --end) {
        const std::size_t selected = rng.index(end);
        std::swap(order[end - 1], order[selected]);
    }
}

void bounded_pretrain(
    TransformerAutoencoder& model,
    std::uint64_t seed,
    TrainingWork& work
) {
    constexpr std::uint64_t kLayouts = 64;
    constexpr int kEpochs = 2;
    constexpr std::size_t kBatchSize = 16;
    const auto corpus = deterministic_layout_corpus(
        kLayouts,
        model.config(),
        seed ^ 0x505245545241494eULL
    );
    std::vector<std::size_t> order(corpus.size(), 0);
    std::iota(order.begin(), order.end(), 0);
    DeterministicRng rng(seed ^ 0x42415443484f5244ULL);
    LossWeights weights;
    weights.reconstruction = 1.0;
    weights.regression = 0.0;
    weights.metric_smoothness = 0.0;
    work.corpus_samples = kLayouts;
    const auto start = Clock::now();
    for (int epoch = 0; epoch < kEpochs; ++epoch) {
        deterministic_shuffle(order, rng);
        for (std::size_t begin = 0;
             begin < order.size();
             begin += kBatchSize) {
            const std::size_t end =
                std::min(begin + kBatchSize, order.size());
            std::vector<std::vector<int>> batch;
            batch.reserve(end - begin);
            for (std::size_t position = begin;
                 position < end;
                 ++position) {
                batch.push_back(corpus[order[position]]);
            }
            model.train_batch(
                batch,
                std::vector<double>(batch.size(), 0.0),
                weights,
                1.0e-3,
                0.9,
                0.999,
                1.0e-8,
                false
            );
            ++work.optimizer_steps;
            work.token_operations +=
                batch.size() *
                static_cast<std::uint64_t>(
                    model.config().sequence_length
                );
        }
        ++work.pretraining_epochs;
    }
    work.wall_seconds += std::chrono::duration<double>(
        Clock::now() - start
    ).count();
    work.training_physical_fes = 0;
}

void fine_tune(
    TransformerAutoencoder& model,
    const std::vector<Individual>& population,
    const EvolutionConfig& config,
    std::uint64_t generation,
    TrainingWork& work
) {
    std::vector<std::vector<int>> layouts;
    std::vector<double> targets;
    layouts.reserve(population.size());
    targets.reserve(population.size());
    for (const Individual& value : population) {
        std::vector<int> zero_based = value.layout;
        for (int& cell : zero_based) {
            --cell;
        }
        layouts.push_back(std::move(zero_based));
        targets.push_back(value.relative_fitness);
    }
    std::vector<std::size_t> order(population.size(), 0);
    std::iota(order.begin(), order.end(), 0);
    DeterministicRng rng(
        config.seed ^
        0x46494e4554554e45ULL ^
        (generation * 0x9e3779b97f4a7c15ULL)
    );
    LossWeights weights;
    weights.metric_pair_seed =
        config.seed ^ generation ^ 0x5041495253454544ULL;
    const auto start = Clock::now();
    for (int epoch = 0; epoch < config.fine_tune_epochs; ++epoch) {
        deterministic_shuffle(order, rng);
        const std::size_t batch_size = static_cast<std::size_t>(
            config.fine_tune_batch_size
        );
        for (std::size_t begin = 0;
             begin < order.size();
             begin += batch_size) {
            const std::size_t end =
                std::min(begin + batch_size, order.size());
            std::vector<std::vector<int>> batch_layouts;
            std::vector<double> batch_targets;
            batch_layouts.reserve(end - begin);
            batch_targets.reserve(end - begin);
            for (std::size_t position = begin;
                 position < end;
                 ++position) {
                batch_layouts.push_back(layouts[order[position]]);
                batch_targets.push_back(targets[order[position]]);
            }
            model.train_batch(
                batch_layouts,
                batch_targets,
                weights,
                1.0e-3,
                0.9,
                0.999,
                1.0e-8,
                true
            );
            ++work.optimizer_steps;
            work.token_operations +=
                batch_layouts.size() *
                static_cast<std::uint64_t>(
                    model.config().sequence_length
                );
        }
        ++work.fine_tuning_epochs;
    }
    work.wall_seconds += std::chrono::duration<double>(
        Clock::now() - start
    ).count();
    work.training_physical_fes = 0;
}

std::vector<Individual> initialize_population(
    const EvolutionConfig& config,
    const fode::CaseData& problem,
    DeterministicRng& rng
) {
    std::vector<Individual> population;
    population.reserve(static_cast<std::size_t>(config.population_size));
    std::set<std::string> seen;
    const std::size_t cap =
        static_cast<std::size_t>(config.population_size) * 20U;
    for (std::size_t attempt = 0;
         attempt < cap &&
         population.size() <
             static_cast<std::size_t>(config.population_size);
         ++attempt) {
        std::vector<int> layout = uniform_feasible_layout(problem, rng);
        if (seen.insert(canonical_solution_key(layout)).second) {
            Individual value;
            value.layout = std::move(layout);
            value.source_index = population.size();
            population.push_back(std::move(value));
        }
    }
    if (population.size() !=
        static_cast<std::size_t>(config.population_size)) {
        throw std::runtime_error("initial population uniqueness cap exhausted");
    }
    return population;
}

std::vector<Individual> generate_offspring(
    std::vector<Individual>& population,
    std::size_t requested,
    const fode::CaseData& problem,
    TransformerAutoencoder& model,
    DeterministicRng& rng
) {
    for (Individual& value : population) {
        std::vector<int> zero_based = value.layout;
        for (int& cell : zero_based) {
            --cell;
        }
        value.raw_latent = model.encode(zero_based);
    }
    const std::size_t dimensions = population.front().raw_latent.size();
    std::vector<double> lower(
        dimensions,
        std::numeric_limits<double>::infinity()
    );
    std::vector<double> upper(
        dimensions,
        -std::numeric_limits<double>::infinity()
    );
    for (const Individual& value : population) {
        for (std::size_t dimension = 0;
             dimension < dimensions;
             ++dimension) {
            lower[dimension] = std::min(
                lower[dimension],
                value.raw_latent[dimension]
            );
            upper[dimension] = std::max(
                upper[dimension],
                value.raw_latent[dimension]
            );
        }
    }
    std::set<std::string> parent_keys;
    for (const Individual& value : population) {
        parent_keys.insert(canonical_solution_key(value.layout));
    }
    std::set<std::string> raw_decoded_keys;
    std::set<std::string> accepted_keys;
    std::vector<Individual> offspring;
    offspring.reserve(requested);
    const std::size_t proposal_cap =
        std::max<std::size_t>(requested * kProposalMultiplierCap, requested);
    for (std::size_t attempt = 0;
         attempt < proposal_cap && offspring.size() < requested;
         ++attempt) {
        const std::size_t current = attempt % population.size();
        const std::size_t first = tournament(population, rng);
        const std::size_t second = tournament(population, rng);
        const std::vector<double> latent = latent_offspring(
            population[current].raw_latent,
            population[first].raw_latent,
            population[second].raw_latent,
            lower,
            upper,
            rng,
            true,
            nullptr
        );
        std::vector<int> decoded = model.decode_argmax(latent);
        for (int& cell : decoded) {
            ++cell;
        }
        DecodedProposalResult filtered = filter_and_repair_decoded(
            decoded,
            problem,
            rng,
            parent_keys,
            raw_decoded_keys,
            accepted_keys
        );
        if (filtered.status != DecodedProposalStatus::accepted) {
            continue;
        }
        Individual value;
        value.layout = std::move(filtered.repaired);
        value.source_index = population.size() + offspring.size();
        offspring.push_back(std::move(value));
    }
    const std::size_t refill_cap =
        std::max<std::size_t>(requested * kRefillMultiplierCap, requested);
    for (std::size_t attempt = 0;
         attempt < refill_cap && offspring.size() < requested;
         ++attempt) {
        const std::vector<int> repaired =
            uniform_feasible_layout(problem, rng);
        const std::string key = canonical_solution_key(repaired);
        if (parent_keys.contains(key) || accepted_keys.contains(key)) {
            continue;
        }
        accepted_keys.insert(key);
        Individual value;
        value.layout = repaired;
        value.source_index = population.size() + offspring.size();
        offspring.push_back(std::move(value));
    }
    if (offspring.size() != requested) {
        throw std::runtime_error("duplicate refill cap exhausted");
    }
    return offspring;
}

TransformerAutoencoder initialize_model(
    const EvolutionConfig& config,
    TrainingWork& work,
    CheckpointMetadata& input_metadata
) {
    if (config.training_profile ==
            TrainingStateProfile::paper_scale_checkpoint &&
        (config.checkpoint_input.empty() ||
         config.checkpoint_sha256.empty())) {
        throw std::invalid_argument(
            "paper-scale profile requires checkpoint input and SHA-256"
        );
    }
    if (!config.checkpoint_input.empty()) {
        TransformerAutoencoder model =
            TransformerAutoencoder::load_checkpoint(
                config.checkpoint_input,
                input_metadata
            );
        if (!config.checkpoint_sha256.empty() &&
            input_metadata.file_sha256 != config.checkpoint_sha256) {
            throw std::invalid_argument("checkpoint SHA-256 mismatch");
        }
        if (config.training_profile ==
            TrainingStateProfile::paper_scale_checkpoint) {
            const ModelConfig& model_config = model.config();
            if (
                input_metadata.training_profile_id !=
                    "paper_scale_declared_reconstruction_v1" ||
                input_metadata.work.corpus_samples != 100000 ||
                input_metadata.work.pretraining_epochs < 500 ||
                model_config.encoder_layers != 6 ||
                model_config.decoder_layers != 6 ||
                model_config.heads != 4 ||
                model_config.model_dimension != 64 ||
                model_config.ffn_width != 256 ||
                model_config.latent_dimension != 64
            ) {
                throw std::invalid_argument(
                    "checkpoint is not immutable paper-scale state"
                );
            }
        }
        work = input_metadata.work;
        work.training_physical_fes = 0;
        return model;
    }
    TransformerAutoencoder model(config.model_config, config.seed);
    bounded_pretrain(model, config.seed, work);
    return model;
}

std::vector<Individual> nondominated_front(
    std::vector<Individual> population
) {
    assign_selection_state(population);
    std::vector<Individual> front;
    for (const Individual& value : population) {
        if (value.rank == 0) {
            front.push_back(value);
        }
    }
    std::stable_sort(
        front.begin(),
        front.end(),
        [](const Individual& left, const Individual& right) {
            if (left.source_index != right.source_index) {
                return left.source_index < right.source_index;
            }
            return left.layout < right.layout;
        }
    );
    return front;
}

}  // namespace

EvolutionResult run_declared_reconstruction(
    const EvolutionConfig& config,
    const fode::CaseData& problem
) {
    const auto total_start = Clock::now();
    if (config.backend != "cpu") {
        throw std::invalid_argument("unsupported backend: " + config.backend);
    }
    if (config.population_size != kPaperPopulationSize ||
        config.maximum_physical_fes <
            static_cast<std::uint64_t>(config.population_size) ||
        config.maximum_physical_fes > kPaperMaximumFes ||
        config.workers <= 0 ||
        config.fine_tune_epochs != 10 ||
        config.fine_tune_batch_size <= 0) {
        throw std::invalid_argument("evolution config violates contract");
    }
    if (config.model_config.vocabulary != problem.rows * problem.cols ||
        config.model_config.sequence_length != problem.turbine_count) {
        throw std::invalid_argument("model and problem shape mismatch");
    }
    fode::PersistentExecutor executor(config.workers);
    DeterministicRng rng(config.seed ^ 0x45564f4c5554494fULL);
    TrainingWork training_work;
    CheckpointMetadata input_metadata;
    TransformerAutoencoder model = initialize_model(
        config,
        training_work,
        input_metadata
    );
    std::vector<Individual> population =
        initialize_population(config, problem, rng);
    double evaluator_wall_seconds =
        evaluate_population(population, problem, executor);
    std::uint64_t physical_fes =
        static_cast<std::uint64_t>(population.size());
    std::uint64_t generation = 0;

    while (physical_fes < config.maximum_physical_fes) {
        assign_selection_state(population);
        assign_spea2_relative_fitness(population);
        fine_tune(
            model,
            population,
            config,
            generation,
            training_work
        );
        const std::size_t requested = static_cast<std::size_t>(
            std::min<std::uint64_t>(
                static_cast<std::uint64_t>(config.population_size),
                config.maximum_physical_fes - physical_fes
            )
        );
        std::vector<Individual> offspring = generate_offspring(
            population,
            requested,
            problem,
            model,
            rng
        );
        evaluator_wall_seconds +=
            evaluate_population(offspring, problem, executor);
        physical_fes += static_cast<std::uint64_t>(offspring.size());
        std::vector<Individual> merged = population;
        merged.insert(
            merged.end(),
            std::make_move_iterator(offspring.begin()),
            std::make_move_iterator(offspring.end())
        );
        population = environmental_selection(
            std::move(merged),
            static_cast<std::size_t>(config.population_size)
        );
        ++generation;
    }
    assign_selection_state(population);
    assign_spea2_relative_fitness(population);
    const std::vector<Individual> front = nondominated_front(population);
    if (front.empty()) {
        throw std::runtime_error("final CDP rank-0 front is empty");
    }

    EvolutionResult result;
    result.method_semantic_id = kMethodSemanticId;
    result.kernel_semantic_id =
        "taae_transformer_declared_reconstruction_v1";
    result.problem_semantic_id = kProblemSemanticId;
    result.problem_semantic_hash =
        wflop::taae::structured_proxy_semantic_hash(problem);
    result.case_id = problem.case_id;
    result.seed = config.seed;
    result.physical_fes = physical_fes;
    result.generations = generation;
    result.requested_workers = config.workers;
    result.training_state_profile_id =
        config.training_profile == TrainingStateProfile::bounded_smoke
            ? "taae_evolution_bounded_smoke_v1"
            : "paper_scale_declared_reconstruction_v1";
    result.model_config = model.config();
    result.training_work = training_work;
    result.training_work.training_physical_fes = 0;
    result.evaluator_wall_seconds = evaluator_wall_seconds;
    result.model_hash = model.parameter_hash();
    result.population_layout_hash =
        hash_individuals(population, false);
    result.front_hash = hash_individuals(front, true);
    result.front_feasibility = std::all_of(
        front.begin(),
        front.end(),
        feasible
    ) ? "all_feasible" : "least_violation_infeasible";
    result.front_minimum_normalized_constraint_violation =
        std::numeric_limits<double>::infinity();
    for (const Individual& value : front) {
        result.front_minimum_normalized_constraint_violation = std::min(
            result.front_minimum_normalized_constraint_violation,
            value.evaluation.normalized_constraint_violation
        );
        IndividualRecord record;
        record.layout_1based = value.layout;
        record.evaluation = value.evaluation;
        record.relative_fitness = value.relative_fitness;
        record.nondomination_rank = value.rank;
        record.crowding_distance = value.crowding;
        result.front.push_back(std::move(record));
    }
    if (!config.checkpoint_output.empty()) {
        result.checkpoint = model.save_checkpoint(
            config.checkpoint_output,
            config.training_profile ==
                    TrainingStateProfile::bounded_smoke
                ? "taae_evolution_bounded_smoke_v1"
                : "paper_scale_declared_reconstruction_v1",
            config.seed,
            result.training_work
        );
    }
    result.total_wall_seconds = std::chrono::duration<double>(
        Clock::now() - total_start
    ).count();
    return result;
}

bool run_scalar_selection_fixtures(std::string& report) {
    std::vector<Individual> population(4);
    population[0].evaluation.reciprocal_expected_power_per_kw = 1.0;
    population[0].evaluation.average_a_weighted_noise_dba = 3.0;
    population[1].evaluation.reciprocal_expected_power_per_kw = 2.0;
    population[1].evaluation.average_a_weighted_noise_dba = 2.0;
    population[2].evaluation.reciprocal_expected_power_per_kw = 3.0;
    population[2].evaluation.average_a_weighted_noise_dba = 1.0;
    population[3].evaluation.reciprocal_expected_power_per_kw = 0.5;
    population[3].evaluation.average_a_weighted_noise_dba = 0.5;
    population[3].evaluation.normalized_constraint_violation = 0.25;
    for (std::size_t index = 0; index < population.size(); ++index) {
        population[index].source_index = index;
    }
    assign_selection_state(population);
    assign_spea2_relative_fitness(population);
    if (population[0].rank != 0 || population[1].rank != 0 ||
        population[2].rank != 0 || population[3].rank == 0 ||
        !tournament_better(population[0], population[3])) {
        report = "CDP or NSGA-II scalar fixture failed";
        return false;
    }
    for (const Individual& value : population) {
        if (value.relative_fitness < 0.0 ||
            value.relative_fitness > 1.0) {
            report = "SPEA2 normalization fixture failed";
            return false;
        }
    }
    std::vector<Individual> zero_range(3);
    for (std::size_t index = 0; index < zero_range.size(); ++index) {
        zero_range[index].evaluation =
            population[1].evaluation;
        zero_range[index].source_index = index;
    }
    assign_spea2_relative_fitness(zero_range);
    for (const Individual& value : zero_range) {
        if (value.relative_fitness != 0.0) {
            report = "SPEA2 zero-range fixture failed";
            return false;
        }
    }
    Individual equal_cv_dominating;
    equal_cv_dominating
        .evaluation.reciprocal_expected_power_per_kw = 1.0;
    equal_cv_dominating
        .evaluation.average_a_weighted_noise_dba = 1.0;
    equal_cv_dominating
        .evaluation.normalized_constraint_violation = 0.3;
    equal_cv_dominating.source_index = 1;
    Individual equal_cv_dominated;
    equal_cv_dominated
        .evaluation.reciprocal_expected_power_per_kw = 2.0;
    equal_cv_dominated
        .evaluation.average_a_weighted_noise_dba = 2.0;
    equal_cv_dominated
        .evaluation.normalized_constraint_violation = 0.3;
    equal_cv_dominated.source_index = 0;
    if (!pareto_dominates(equal_cv_dominating, equal_cv_dominated) ||
        cdp_dominates(equal_cv_dominating, equal_cv_dominated) ||
        cdp_dominates(equal_cv_dominated, equal_cv_dominating)) {
        report = "equal-CV infeasible CDP fixture failed";
        return false;
    }
    std::vector<Individual> equal_cv{
        equal_cv_dominating,
        equal_cv_dominated,
    };
    assign_selection_state(equal_cv);
    if (equal_cv[0].rank != 0 || equal_cv[1].rank != 0 ||
        !tournament_better(equal_cv[1], equal_cv[0])) {
        report = "equal-CV NSGA-II deterministic tie fixture failed";
        return false;
    }
    std::vector<Individual> no_feasible(3);
    no_feasible[0].evaluation.reciprocal_expected_power_per_kw = 1.0;
    no_feasible[0].evaluation.average_a_weighted_noise_dba = 3.0;
    no_feasible[0].evaluation.normalized_constraint_violation = 0.1;
    no_feasible[1].evaluation.reciprocal_expected_power_per_kw = 3.0;
    no_feasible[1].evaluation.average_a_weighted_noise_dba = 1.0;
    no_feasible[1].evaluation.normalized_constraint_violation = 0.1;
    no_feasible[2].evaluation.reciprocal_expected_power_per_kw = 0.5;
    no_feasible[2].evaluation.average_a_weighted_noise_dba = 0.5;
    no_feasible[2].evaluation.normalized_constraint_violation = 0.2;
    for (std::size_t index = 0; index < no_feasible.size(); ++index) {
        no_feasible[index].source_index = index;
    }
    const std::vector<Individual> fallback =
        nondominated_front(no_feasible);
    if (fallback.size() != 2 ||
        fallback[0].evaluation.normalized_constraint_violation != 0.1 ||
        fallback[1].evaluation.normalized_constraint_violation != 0.1) {
        report = "CDP no-feasible rank-0 fallback fixture failed";
        return false;
    }
    report =
        "selection_fixture_pass cdp=pass spea2_k=floor_sqrt_N "
        "zero_range=0 equal_cv=no_cdp_dominance "
        "nsgaii_tie=stable_source no_feasible=least_violation_front";
    return true;
}

bool run_latent_operator_fixtures(std::string& report) {
    const std::vector<double> current{0.0, 0.0, 0.0, 0.0};
    const std::vector<double> first{1.0, 2.0, 3.0, 4.0};
    const std::vector<double> second{-1.0, -2.0, -3.0, -4.0};
    const std::vector<double> lower(4, -2.0);
    const std::vector<double> upper(4, 2.0);
    DeterministicRng rng(123);
    std::size_t selected = 0;
    const std::vector<double> crossed = latent_offspring(
        current,
        first,
        second,
        lower,
        upper,
        rng,
        false,
        &selected
    );
    std::size_t changed = 0;
    for (std::size_t index = 0; index < crossed.size(); ++index) {
        changed += crossed[index] != current[index] ? 1U : 0U;
    }
    if (changed != 1 || crossed[selected] == current[selected]) {
        report = "one-mutant-dimension crossover fixture failed";
        return false;
    }
    for (int repetition = 0; repetition < 100; ++repetition) {
        const double mutated =
            polynomial_mutation(0.25, -1.0, 1.0, rng);
        if (mutated < -1.0 || mutated > 1.0) {
            report = "bounded polynomial mutation fixture failed";
            return false;
        }
    }
    report =
        "latent_fixture_pass differential_weight=0.3 "
        "one_mutant_dimension=pass polynomial_eta=20 bounds=pass";
    return true;
}

bool run_repair_and_duplicate_fixtures(
    const fode::CaseData& problem,
    std::string& report
) {
    DeterministicRng rng(991);
    const std::vector<int> base =
        uniform_feasible_layout(problem, rng);
    std::vector<int> two_conflicts = base;
    two_conflicts[1] = two_conflicts[0];
    two_conflicts[2] = problem.unavailable_cells_1based.front();
    bool reinitialized = false;
    const std::vector<int> repaired = repair_layout(
        two_conflicts,
        problem,
        rng,
        &reinitialized
    );
    if (reinitialized || repaired.size() != base.size() ||
        std::adjacent_find(repaired.begin(), repaired.end()) !=
            repaired.end()) {
        report = "Gaussian repair <=2 conflicts fixture failed";
        return false;
    }
    const std::set<int> unavailable = unavailable_cells(problem);
    for (int cell : repaired) {
        if (unavailable.contains(cell)) {
            report = "Gaussian repair retained monitor overlap";
            return false;
        }
    }
    std::vector<int> many_conflicts = base;
    many_conflicts[1] = many_conflicts[0];
    many_conflicts[2] = many_conflicts[0];
    many_conflicts[3] = many_conflicts[0];
    const std::vector<int> reinitialized_layout = repair_layout(
        many_conflicts,
        problem,
        rng,
        &reinitialized
    );
    if (!reinitialized ||
        reinitialized_layout.size() != base.size()) {
        report = "full reinitialization >2 conflicts fixture failed";
        return false;
    }
    std::set<std::string> keys;
    keys.insert(canonical_solution_key(base));
    std::size_t duplicate_rejections = 0;
    std::vector<std::vector<int>> refill;
    for (int attempt = 0;
         attempt < 100 && refill.size() < 10;
         ++attempt) {
        const std::vector<int> candidate =
            attempt == 0 ? base : uniform_feasible_layout(problem, rng);
        if (!keys.insert(canonical_solution_key(candidate)).second) {
            ++duplicate_rejections;
            continue;
        }
        refill.push_back(candidate);
    }
    if (duplicate_rejections == 0 || refill.size() != 10) {
        report = "duplicate removal/refill fixture failed";
        return false;
    }

    fode::CaseData constrained = problem;
    constrained.unavailable_cells_1based.clear();
    const std::set<int> base_cells(base.begin(), base.end());
    for (int cell = 1; cell <= problem.rows * problem.cols; ++cell) {
        if (!base_cells.contains(cell)) {
            constrained.unavailable_cells_1based.push_back(cell);
        }
    }
    std::set<std::string> no_parents;
    std::set<std::string> raw_decoded_keys;
    std::set<std::string> accepted_repaired_keys;
    DeterministicRng filter_rng(771);
    std::vector<int> first_raw = base;
    first_raw[0] = 0;
    const DecodedProposalResult first_result =
        filter_and_repair_decoded(
            first_raw,
            constrained,
            filter_rng,
            no_parents,
            raw_decoded_keys,
            accepted_repaired_keys
        );
    std::reverse(first_raw.begin(), first_raw.end());
    const std::uint64_t before_raw_duplicate = filter_rng.state;
    const DecodedProposalResult duplicate_raw_result =
        filter_and_repair_decoded(
            first_raw,
            constrained,
            filter_rng,
            no_parents,
            raw_decoded_keys,
            accepted_repaired_keys
        );
    if (first_result.status != DecodedProposalStatus::accepted ||
        duplicate_raw_result.status !=
            DecodedProposalStatus::duplicate_raw_before_repair ||
        filter_rng.state != before_raw_duplicate) {
        report = "pre-repair raw decoded duplicate fixture failed";
        return false;
    }

    std::set<std::string> parent_keys{
        canonical_solution_key(base),
    };
    std::set<std::string> parent_raw_keys;
    std::set<std::string> parent_accepted_keys;
    std::vector<int> shuffled_parent = base;
    std::reverse(shuffled_parent.begin(), shuffled_parent.end());
    const std::uint64_t before_parent = filter_rng.state;
    const DecodedProposalResult parent_result =
        filter_and_repair_decoded(
            shuffled_parent,
            constrained,
            filter_rng,
            parent_keys,
            parent_raw_keys,
            parent_accepted_keys
        );
    if (parent_result.status !=
            DecodedProposalStatus::parent_identical_before_repair ||
        filter_rng.state != before_parent) {
        report = "pre-repair parent identity fixture failed";
        return false;
    }

    std::vector<int> second_raw = base;
    second_raw[0] = problem.rows * problem.cols + 1;
    const DecodedProposalResult post_repair_result =
        filter_and_repair_decoded(
            second_raw,
            constrained,
            filter_rng,
            no_parents,
            raw_decoded_keys,
            accepted_repaired_keys
        );
    if (post_repair_result.status !=
        DecodedProposalStatus::duplicate_after_repair) {
        report = "post-repair duplicate fixture failed";
        return false;
    }
    report =
        "repair_fixture_pass gaussian_conflicts=2 full_reinit_conflicts=3 "
        "nearest_tie=ascending raw_duplicate=pre_repair_no_rng "
        "parent_identity=pre_repair_no_rng "
        "repaired_collision=post_repair duplicate_refill=pass";
    return true;
}

std::string result_to_json(const EvolutionResult& result) {
    std::ostringstream stream;
    stream << std::setprecision(17);
    stream << '{'
           << "\"method_semantic_id\":\""
           << result.method_semantic_id << "\","
           << "\"kernel_semantic_id\":\""
           << result.kernel_semantic_id << "\","
           << "\"problem_semantic_id\":\""
           << result.problem_semantic_id << "\","
           << "\"problem_semantic_hash\":\""
           << result.problem_semantic_hash << "\","
           << "\"case_id\":\"" << result.case_id << "\","
           << "\"seed\":" << result.seed << ','
           << "\"physical_fes\":" << result.physical_fes << ','
           << "\"generations\":" << result.generations << ','
           << "\"requested_workers\":" << result.requested_workers << ','
           << "\"training_state_profile_id\":\""
           << result.training_state_profile_id << "\","
           << "\"model_architecture\":{"
           << "\"vocabulary\":" << result.model_config.vocabulary << ','
           << "\"sequence_length\":"
           << result.model_config.sequence_length << ','
           << "\"model_dimension\":"
           << result.model_config.model_dimension << ','
           << "\"latent_dimension\":"
           << result.model_config.latent_dimension << ','
           << "\"heads\":" << result.model_config.heads << ','
           << "\"encoder_layers\":"
           << result.model_config.encoder_layers << ','
           << "\"decoder_layers\":"
           << result.model_config.decoder_layers << ','
           << "\"ffn_width\":" << result.model_config.ffn_width << ','
           << "\"regression_hidden_width\":"
           << result.model_config.regression_hidden_width << ','
           << "\"dropout\":" << result.model_config.dropout
           << "},"
           << "\"training_work\":{"
           << "\"corpus_samples\":"
           << result.training_work.corpus_samples << ','
           << "\"token_operations\":"
           << result.training_work.token_operations << ','
           << "\"optimizer_steps\":"
           << result.training_work.optimizer_steps << ','
           << "\"pretraining_epochs\":"
           << result.training_work.pretraining_epochs << ','
           << "\"fine_tuning_epochs\":"
           << result.training_work.fine_tuning_epochs << ','
           << "\"training_physical_fes\":"
           << result.training_work.training_physical_fes << ','
           << "\"wall_seconds\":"
           << result.training_work.wall_seconds
           << "},"
           << "\"evaluator_wall_seconds\":"
           << result.evaluator_wall_seconds << ','
           << "\"total_wall_seconds\":"
           << result.total_wall_seconds << ','
           << "\"model_hash\":\"" << result.model_hash << "\","
           << "\"population_layout_hash\":\""
           << result.population_layout_hash << "\","
           << "\"front_hash\":\"" << result.front_hash << "\","
           << "\"front_feasibility\":\""
           << result.front_feasibility << "\","
           << "\"front_minimum_normalized_constraint_violation\":"
           << result.front_minimum_normalized_constraint_violation << ','
           << "\"front\":[";
    for (std::size_t index = 0; index < result.front.size(); ++index) {
        if (index != 0) {
            stream << ',';
        }
        const IndividualRecord& value = result.front[index];
        stream << "{\"layout_1based\":[";
        for (std::size_t cell = 0;
             cell < value.layout_1based.size();
             ++cell) {
            if (cell != 0) {
                stream << ',';
            }
            stream << value.layout_1based[cell];
        }
        stream << "],\"reciprocal_expected_power_per_kw\":"
               << value.evaluation.reciprocal_expected_power_per_kw
               << ",\"average_a_weighted_noise_dba\":"
               << value.evaluation.average_a_weighted_noise_dba
               << ",\"normalized_constraint_violation\":"
               << value.evaluation.normalized_constraint_violation
               << ",\"relative_fitness\":"
               << value.relative_fitness
               << ",\"rank\":" << value.nondomination_rank
               << '}';
    }
    stream << "],\"checkpoint\":";
    if (result.checkpoint.file_sha256.empty()) {
        stream << "null";
    } else {
        stream << '{'
               << "\"method_semantic_id\":\""
               << result.checkpoint.method_semantic_id << "\","
               << "\"problem_semantic_id\":\""
               << result.checkpoint.problem_semantic_id << "\","
               << "\"training_profile_id\":\""
               << result.checkpoint.training_profile_id << "\","
               << "\"initialization_seed\":"
               << result.checkpoint.initialization_seed << ','
               << "\"parameter_fnv1a64\":\""
               << result.checkpoint.parameter_fnv1a64 << "\","
               << "\"file_sha256\":\""
               << result.checkpoint.file_sha256 << "\"}";
    }
    stream << '}';
    return stream.str();
}

}  // namespace taae::evolution
